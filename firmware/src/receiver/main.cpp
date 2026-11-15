// OpenGong receiver firmware: Heltec WiFi LoRa 32 V3 at the firing line.
//
// - Receives shots, ACKs them, drops duplicates and prints one JSON line per
//   shot on USB serial for a laptop app. Lines starting with '{' are JSON;
//   everything else is human-readable.
// - Shows the last hit, a hit plot and group size on the OLED.
// - Runs a Wi-Fi access point and a web app for phones and laptops (webui.cpp).
// - Any serial line that does not start with "rx " is sent to the target as a
//   console command, and the target's output is printed back. The web app can
//   send commands the same way (POST /api/cmd); that is how its calibration
//   wizard drives the target.
//
// Receiver-local commands: rx help | rx clear | rx stats
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <SSD1306Wire.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include "OpenGongProto.h"
#include "webui.h"

using namespace og;
using namespace og::heltec;

namespace {

constexpr int kMaxHits = 100;
constexpr float kPlotRingMm = 120.0f;  // sensor ring radius drawn on the plot

SPIClass s_spi(HSPI);
SX1262* s_radio = nullptr;
volatile bool s_irq = false;
SSD1306Wire s_oled(0x3c, kOledSda, kOledScl, GEOMETRY_128_64);

struct Hit {
  float x, y;
};
Hit s_hits[kMaxHits];
int s_nhits = 0;

struct Last {
  bool have = false;
  ShotPacket pkt;
  float rssi = 0, snr = 0;
} s_last;

struct Seen {
  uint16_t boot, shot;
};
Seen s_seen[16];
int s_seenPos = 0;

StatusPacket s_status;
bool s_haveStatus = false;
uint32_t s_statusAt = 0;
uint32_t s_rxCount = 0, s_dupCount = 0, s_ackCount = 0;

uint8_t s_cmdSeq = 0;
char s_line[og::kMaxText + 8];
size_t s_len = 0;
bool s_dirty = true;
uint32_t s_lastDraw = 0;

void IRAM_ATTR onDio1() { s_irq = true; }

void startRx() {
  s_irq = false;
  s_radio->startReceive();
}

void tx(const uint8_t* buf, size_t len) {
  s_radio->transmit(const_cast<uint8_t*>(buf), len);
  startRx();
}

// ---------------------------------------------------------------------------
// Group statistics (shooter frame, mm)
// ---------------------------------------------------------------------------
void groupStats(float& mx, float& my, float& es) {
  mx = my = es = 0;
  if (!s_nhits) return;
  for (int i = 0; i < s_nhits; ++i) {
    mx += s_hits[i].x;
    my += s_hits[i].y;
  }
  mx /= s_nhits;
  my /= s_nhits;
  for (int i = 0; i < s_nhits; ++i)
    for (int j = i + 1; j < s_nhits; ++j)
      es = fmaxf(es, hypotf(s_hits[i].x - s_hits[j].x, s_hits[i].y - s_hits[j].y));
}

void addHit(float x, float y) {
  if (s_nhits == kMaxHits) {
    memmove(s_hits, s_hits + 1, sizeof(Hit) * (kMaxHits - 1));
    --s_nhits;
  }
  s_hits[s_nhits++] = {x, y};
}

// Called when a browser presses "Clear shots"; the web module has already
// dropped its own copy.
void onWebClear() {
  s_nhits = 0;
  s_dirty = true;
  Serial.println("rx: group cleared from the web app");
}

// ---------------------------------------------------------------------------
// Display: text on the left, hit plot on the right
// ---------------------------------------------------------------------------
void draw() {
  s_oled.clear();
  s_oled.setTextAlignment(TEXT_ALIGN_LEFT);
  char line[32];

  // Plot: 64 x 64 square, sensor ring as a circle.
  const int cx = 96, cy = 32, rpx = 30;
  const float scale = rpx / kPlotRingMm;
  s_oled.drawCircle(cx, cy, rpx);
  s_oled.setPixel(cx, cy);
  for (int i = 0; i < s_nhits; ++i) {
    const int px = cx + static_cast<int>(lroundf(s_hits[i].x * scale));
    const int py = cy - static_cast<int>(lroundf(s_hits[i].y * scale));
    if (i == s_nhits - 1) s_oled.fillCircle(px, py, 2);
    else s_oled.fillRect(px, py, 2, 2);
  }

  if (!s_last.have) {
    s_oled.setFont(ArialMT_Plain_10);
    s_oled.drawString(0, 0, "OpenGong RX");
    s_oled.drawString(0, 14, s_haveStatus ? "target online" : "no target yet");
    s_oled.drawString(0, 32, "WiFi:");
    s_oled.drawString(0, 42, webui::ssid());
    s_oled.drawString(0, 53, webui::ipAddress());
    s_oled.display();
    return;
  }

  const ShotPacket& p = s_last.pkt;
  s_oled.setFont(ArialMT_Plain_16);
  snprintf(line, sizeof line, "#%u", p.shot);
  s_oled.drawString(0, 0, line);
  s_oled.setFont(ArialMT_Plain_10);
  if (p.flags & SHOT_NO_FIX) {
    s_oled.drawString(0, 18, "no fix");
  } else {
    snprintf(line, sizeof line, "x %+.1f", p.x_dmm / 10.0f);
    s_oled.drawString(0, 17, line);
    snprintf(line, sizeof line, "y %+.1f%s", p.y_dmm / 10.0f, (p.flags & SHOT_HIGH_RESID) ? " ?" : "");
    s_oled.drawString(0, 28, line);
  }
  float mx, my, es;
  groupStats(mx, my, es);
  snprintf(line, sizeof line, "ES %.0f n%d", es, s_nhits);
  s_oled.drawString(0, 40, line);
  snprintf(line, sizeof line, "%.0fdBm", s_last.rssi);
  s_oled.drawString(0, 52, line);
  s_oled.display();
}

// ---------------------------------------------------------------------------
// Packet handling
// ---------------------------------------------------------------------------
bool seenBefore(uint16_t boot, uint16_t shot) {
  for (const Seen& s : s_seen)
    if (s.boot == boot && s.shot == shot && shot != 0) return true;
  s_seen[s_seenPos] = {boot, shot};
  s_seenPos = (s_seenPos + 1) % 16;
  return false;
}

void printFlags(uint8_t f) {
  static const char* names[] = {"missing_ch", "high_resid", "dropped_ch", "outside_ring",
                                "no_fix",     "cal",        "sim"};
  Serial.print('[');
  bool first = true;
  for (int b = 0; b < 7; ++b) {
    if (!(f & (1u << b))) continue;
    Serial.printf("%s\"%s\"", first ? "" : ",", names[b]);
    first = false;
  }
  Serial.print(']');
}

void handleShot(const ShotPacket& p, float rssi, float snr) {
  // ACK first, every time (the previous ACK may have been lost).
  AckPacket ack = {};
  initHeader(ack.hdr, PKT_ACK);
  ack.boot_id = p.boot_id;
  ack.shot = p.shot;
  seal(ack);
  tx(reinterpret_cast<const uint8_t*>(&ack), sizeof ack);
  ++s_ackCount;

  if (seenBefore(p.boot_id, p.shot)) {
    ++s_dupCount;
    return;
  }

  const bool fix = !(p.flags & SHOT_NO_FIX);
  if (fix && !(p.flags & (SHOT_CAL | SHOT_SIM))) addHit(p.x_dmm / 10.0f, p.y_dmm / 10.0f);
  webui::recordShot(p, rssi, snr);
  float mx, my, es;
  groupStats(mx, my, es);

  Serial.printf("{\"type\":\"shot\",\"boot\":%u,\"n\":%u", p.boot_id, p.shot);
  if (fix) Serial.printf(",\"x\":%.1f,\"y\":%.1f,\"rms_ns\":%u", p.x_dmm / 10.0f, p.y_dmm / 10.0f, p.rms_ns);
  Serial.printf(",\"fired\":%u,\"used\":%u,\"flags\":", p.fired, p.used);
  printFlags(p.flags);
  Serial.printf(",\"batt_mv\":%u,\"rssi\":%.0f,\"snr\":%.1f", p.batt_mv, rssi, snr);
  Serial.printf(",\"group\":{\"n\":%d,\"mpi_x\":%.1f,\"mpi_y\":%.1f,\"es_mm\":%.1f}}\n", s_nhits, mx, my, es);

  s_last.have = true;
  s_last.pkt = p;
  s_last.rssi = rssi;
  s_last.snr = snr;
  s_dirty = true;
}

void handleStatus(const StatusPacket& st, float rssi) {
  s_status = st;
  s_haveStatus = true;
  s_statusAt = millis();
  webui::recordStatus(st, rssi);
  Serial.printf(
      "{\"type\":\"status\",\"boot\":%u,\"shots\":%u,\"rejects\":%u,\"batt_mv\":%u,\"uptime_min\":%u,"
      "\"n_ch\":%u,\"mode\":\"%s\",\"rssi\":%.0f}\n",
      st.boot_id, st.shots, st.rejects, st.batt_mv, st.uptime_min, st.n_ch, st.mode ? "cal" : "normal", rssi);
  s_dirty = true;
}

void handleResp(const uint8_t* buf, size_t len) {
  uint8_t seq, part;
  char text[kMaxText + 1];
  if (!parseText(buf, len, seq, part, text, sizeof text)) return;
  webui::recordResponse(seq, part, text);
  // Raw text so a human can follow along; apps ignore non-JSON lines.
  Serial.print(text);
  if ((part & kPartLast) && len > kTextOverhead && text[strlen(text) - 1] != '\n') Serial.println();
}

void pollRadio() {
  if (!s_irq) return;
  s_irq = false;
  uint8_t buf[kMaxPacket + 8];
  const size_t len = s_radio->getPacketLength();
  int st = RADIOLIB_ERR_UNKNOWN;
  if (len > 0 && len <= sizeof buf) st = s_radio->readData(buf, len);
  const float rssi = s_radio->getRSSI(), snr = s_radio->getSNR();
  startRx();
  if (st != RADIOLIB_ERR_NONE || !packetValid(buf, len)) return;
  ++s_rxCount;

  switch (packetType(buf)) {
    case PKT_SHOT: {
      ShotPacket p;
      memcpy(&p, buf, sizeof p);
      handleShot(p, rssi, snr);
      break;
    }
    case PKT_STATUS: {
      StatusPacket p;
      memcpy(&p, buf, sizeof p);
      handleStatus(p, rssi);
      break;
    }
    case PKT_RESP:
      handleResp(buf, len);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------
void localCommand(const char* cmd) {
  if (!strcmp(cmd, "clear")) {
    s_nhits = 0;
    s_dirty = true;
    webui::clearLog();  // connected browsers clear on their next poll
    Serial.println("rx: group cleared");
  } else if (!strcmp(cmd, "stats")) {
    Serial.printf("rx: packets %lu, acks sent %lu, duplicates %lu, hits in group %d\n", (unsigned long)s_rxCount,
                  (unsigned long)s_ackCount, (unsigned long)s_dupCount, s_nhits);
    if (s_haveStatus)
      Serial.printf("rx: last target status %lus ago, boot %u, battery %u mV\n",
                    (unsigned long)((millis() - s_statusAt) / 1000), s_status.boot_id, s_status.batt_mv);
  } else {
    Serial.printf(
        "rx help   this list\n"
        "rx clear  start a new group (also clears the web app)\n"
        "rx stats  link counters\n"
        "Web app:  join Wi-Fi \"%s\" and open http://%s/\n"
        "Any other line is sent to the target (try 'help').\n",
        webui::ssid(), webui::ipAddress());
  }
}

uint8_t transmitCommand(const char* text) {
  uint8_t buf[kMaxPacket];
  const uint8_t seq = ++s_cmdSeq;
  const size_t len = buildText(buf, PKT_CMD, seq, kPartLast, text, strlen(text));
  tx(buf, len);
  return seq;
}

void sendCommand(const char* text) {
  transmitCommand(text);
  Serial.printf("> [to target] %s\n", text);
}

// Commands from the web app (calibration wizard and value editor).
uint8_t sendWebCommand(const char* text) {
  const uint8_t seq = transmitCommand(text);
  Serial.printf("> [web to target] %s\n", text);
  return seq;
}

void pollSerial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (!s_len) continue;
      s_line[s_len] = '\0';
      s_len = 0;
      if (!strncmp(s_line, "rx", 2) && (s_line[2] == ' ' || s_line[2] == '\0')) {
        const char* sub = s_line + 2;
        while (*sub == ' ') ++sub;
        localCommand(sub);
      } else {
        sendCommand(s_line);
      }
    } else if (s_len < kMaxText) {
      s_line[s_len++] = static_cast<char>(c);
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nOpenGong receiver ('rx help' for commands)");

  pinMode(kVext, OUTPUT);
  digitalWrite(kVext, LOW);
  delay(50);
  pinMode(kOledRst, OUTPUT);
  digitalWrite(kOledRst, LOW);
  delay(20);
  digitalWrite(kOledRst, HIGH);
  delay(20);
  s_oled.init();
  s_oled.flipScreenVertically();

  s_spi.begin(kLoraSck, kLoraMiso, kLoraMosi, kLoraNss);
  s_radio = new SX1262(new Module(kLoraNss, kLoraDio1, kLoraRst, kLoraBusy, s_spi));
  const int st = s_radio->begin(kLoraFreqMHz, kLoraBwKHz, kLoraSF, kLoraCR, kLoraSyncWord, kLoraPowerDbm,
                                kLoraPreamble, kLoraTcxoV, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("FATAL: SX1262 init failed (%d)\n", st);
    s_oled.drawString(0, 0, "Radio init failed");
    s_oled.display();
    for (;;) delay(1000);
  }
  s_radio->setDio2AsRfSwitch(true);
  s_radio->setCRC(true);
  s_radio->setDio1Action(onDio1);
  startRx();
  Serial.printf("{\"type\":\"rx_boot\",\"net\":%u,\"freq_mhz\":%.1f,\"sf\":%u,\"pwr_dbm\":%d}\n", kNetId,
                kLoraFreqMHz, kLoraSF, kLoraPowerDbm);
  webui::begin(onWebClear, sendWebCommand);
  draw();
}

void loop() {
  pollRadio();
  webui::poll();
  pollSerial();
  const uint32_t now = millis();
  if (s_dirty || now - s_lastDraw > 1000) {
    draw();
    s_dirty = false;
    s_lastDraw = now;
  }
  delay(1);
}
