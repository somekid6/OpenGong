// Receiver Wi-Fi access point and web UI. See webui.h.
#include "webui.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

#include "web_page.h"  // generated from web/index.html by tools/embed_web.py

// Override with build flags, e.g.  -DOG_WIFI_SSID='"MyGong"' -DOG_WIFI_PASS='"secret123"'
#ifndef OG_WIFI_SSID
#define OG_WIFI_SSID "OpenGong"
#endif
#ifndef OG_WIFI_PASS
#define OG_WIFI_PASS "opengong"  // WPA2 needs 8+ characters; "" makes the network open
#endif
#ifndef OG_WIFI_HOST
#define OG_WIFI_HOST "opengong"  // http://opengong.local/
#endif

namespace webui {
namespace {

constexpr int kLogSize = 64;      // shots kept in RAM for browsers that reconnect
constexpr uint8_t kMaxClients = 4;
constexpr uint8_t kChannel = 1;

struct Rec {
  uint32_t seq;
  uint32_t t_ms;
  uint16_t boot, shot, rms_ns, batt_mv;
  int16_t x_dmm, y_dmm;
  uint8_t fired, used, flags;
  float rssi, snr;
};

Rec s_log[kLogSize];
int s_logHead = 0;  // next slot to write
int s_logCount = 0;
uint32_t s_seq = 0;
uint32_t s_clears = 0;
uint32_t s_rxBoot = 1;
void (*s_onClear)() = nullptr;
uint8_t (*s_sendCmd)(const char*, int) = nullptr;

// Console commands sent from browsers and the target's replies. A reply comes
// back as one or more PKT_RESP chunks carrying the command's seq.
constexpr int kCmdSlots = 4;
constexpr size_t kReplyCap = 1700;          // the target buffers at most 1600 bytes of output
constexpr uint32_t kReplyMatchMs = 30000;   // ignore chunks for commands older than this
struct Cmd {
  uint32_t id = 0;  // 0 = empty
  uint32_t sentAt = 0;
  uint8_t seq = 0;
  uint8_t nextPart = 0;
  bool done = false;
  bool gap = false;  // a chunk went missing
  uint16_t len = 0;
  char cmd[og::kMaxText + 1];  // the command line, kept so a retry can resend it
  char text[kReplyCap];        // the reply
};
Cmd s_cmds[kCmdSlots];
int s_cmdNext = 0;
uint32_t s_cmdId = 0;

// Last-heard state of the target, from shots and status heartbeats.
struct {
  uint32_t heardAt = 0;  // millis(); 0 = never
  bool haveStatus = false;
  uint16_t boot = 0, batt_mv = 0, shots = 0, rejects = 0;
  uint8_t n_ch = 0, mode = 0;
  float rssi = 0;
} s_tgt;

// The latest reject notice: an impact heard but not placed. id counts notices
// since this receiver started, so a browser can tell a new one from the last.
struct {
  uint32_t id = 0;  // 0 = none yet
  uint32_t t_ms = 0;
  uint16_t boot = 0, rejects = 0;
  uint8_t fired = 0, n_ch = 0, need = 0;
} s_rej;

WebServer s_http(80);
DNSServer s_dns;
char s_ip[16] = "0.0.0.0";
char s_buf[320];

void noCache() { s_http.sendHeader("Cache-Control", "no-cache, no-store"); }

void handleIndex() {
  noCache();
  s_http.sendHeader("Content-Encoding", "gzip");
  s_http.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kIndexHtmlGz), kIndexHtmlGzLen);
}

void handleShots() {
  uint32_t since = 0;
  if (s_http.hasArg("since")) since = strtoul(s_http.arg("since").c_str(), nullptr, 10);
  const uint32_t now = millis();

  noCache();
  s_http.setContentLength(CONTENT_LENGTH_UNKNOWN);  // chunked: no big String on the heap
  s_http.send(200, "application/json", "");

  snprintf(s_buf, sizeof s_buf, "{\"rxboot\":%lu,\"seq\":%lu,\"clears\":%lu,\"now_ms\":%lu,\"target\":",
           (unsigned long)s_rxBoot, (unsigned long)s_seq, (unsigned long)s_clears, (unsigned long)now);
  s_http.sendContent(s_buf);

  if (!s_tgt.heardAt) {
    s_http.sendContent("{\"seen\":false}");
  } else {
    snprintf(s_buf, sizeof s_buf,
             "{\"seen\":true,\"age_s\":%lu,\"boot\":%u,\"batt_mv\":%u,\"rssi\":%.0f,\"shots\":%u,\"rejects\":%u,"
             "\"mode\":\"%s\",\"defaults\":%s,\"n_ch\":%u,\"have_status\":%s}",
             (unsigned long)((now - s_tgt.heardAt) / 1000), s_tgt.boot, s_tgt.batt_mv, s_tgt.rssi, s_tgt.shots,
             s_tgt.rejects, (s_tgt.mode & og::STATUS_CAL) ? "cal" : "normal",
             (s_tgt.mode & og::STATUS_DEFAULTS) ? "true" : "false", s_tgt.n_ch, s_tgt.haveStatus ? "true" : "false");
    s_http.sendContent(s_buf);
  }

  if (s_rej.id) {
    snprintf(s_buf, sizeof s_buf,
             ",\"reject\":{\"id\":%lu,\"boot\":%u,\"rejects\":%u,\"fired\":%u,\"n_ch\":%u,\"need\":%u,\"t_ms\":%lu}",
             (unsigned long)s_rej.id, s_rej.boot, s_rej.rejects, s_rej.fired, s_rej.n_ch, s_rej.need,
             (unsigned long)s_rej.t_ms);
    s_http.sendContent(s_buf);
  }

  s_http.sendContent(",\"shots\":[");
  bool first = true;
  for (int i = 0; i < s_logCount; ++i) {  // oldest first
    const Rec& r = s_log[(s_logHead - s_logCount + i + kLogSize) % kLogSize];
    if (r.seq <= since) continue;
    snprintf(s_buf, sizeof s_buf,
             "%s{\"seq\":%lu,\"boot\":%u,\"n\":%u,\"x\":%.1f,\"y\":%.1f,\"rms\":%u,\"fired\":%u,\"used\":%u,"
             "\"flags\":%u,\"batt_mv\":%u,\"rssi\":%.0f,\"snr\":%.1f,\"t_ms\":%lu}",
             first ? "" : ",", (unsigned long)r.seq, r.boot, r.shot, r.x_dmm / 10.0f, r.y_dmm / 10.0f, r.rms_ns,
             r.fired, r.used, r.flags, r.batt_mv, r.rssi, r.snr, (unsigned long)r.t_ms);
    s_http.sendContent(s_buf);
    first = false;
  }
  s_http.sendContent("]}");
  s_http.sendContent("");  // end of chunked body
}

void handleClear() {
  clearLog();
  if (s_onClear) s_onClear();
  snprintf(s_buf, sizeof s_buf, "{\"ok\":true,\"clears\":%lu}", (unsigned long)s_clears);
  noCache();
  s_http.send(200, "application/json", s_buf);
}

void sendJson(int code, const char* body) {
  noCache();
  s_http.send(code, "application/json", body);
}

void resetReply(Cmd& m) {
  m.done = m.gap = false;
  m.len = 0;
  m.nextPart = 0;
  m.text[0] = '\0';
}

// POST /api/cmd with retry=<id>: resend that command with its original seq.
// The target recognises the repeat and resends its earlier reply instead of
// running the command a second time.
void handleCmdRetry() {
  const uint32_t id = strtoul(s_http.arg("retry").c_str(), nullptr, 10);
  Cmd* m = nullptr;
  for (Cmd& c : s_cmds)
    if (id && c.id == id) m = &c;
  if (!m || !s_sendCmd) {
    sendJson(400, "{\"ok\":false,\"error\":\"unknown command id\"}");
    return;
  }
  if (!m->done) {  // a finished reply is already there to read
    resetReply(*m);
    s_sendCmd(m->cmd, m->seq);
    m->sentAt = millis();
  }
  snprintf(s_buf, sizeof s_buf, "{\"ok\":true,\"id\":%lu}", (unsigned long)m->id);
  sendJson(200, s_buf);
}

void handleCmdPost() {
  if (s_http.hasArg("retry")) {
    handleCmdRetry();
    return;
  }
  const String c = s_http.arg("c");
  const char* why = nullptr;
  if (!s_sendCmd) why = "relay unavailable";
  else if (c.length() == 0 || c.length() > og::kMaxText) why = "command must be 1-200 characters";
  else if (c.startsWith("rx")) why = "receiver commands are serial-only";
  for (size_t i = 0; !why && i < c.length(); ++i)
    if (c[i] < 32 || c[i] > 126) why = "command must be printable ASCII on one line";
  if (why) {
    snprintf(s_buf, sizeof s_buf, "{\"ok\":false,\"error\":\"%s\"}", why);
    sendJson(400, s_buf);
    return;
  }
  Cmd& m = s_cmds[s_cmdNext];
  s_cmdNext = (s_cmdNext + 1) % kCmdSlots;
  m.id = ++s_cmdId;
  resetReply(m);
  strlcpy(m.cmd, c.c_str(), sizeof m.cmd);
  m.seq = s_sendCmd(m.cmd, -1);  // transmits now; the reply arrives through recordResponse()
  m.sentAt = millis();
  snprintf(s_buf, sizeof s_buf, "{\"ok\":true,\"id\":%lu}", (unsigned long)m.id);
  sendJson(200, s_buf);
}

void handleCmdGet() {
  const uint32_t id = s_http.hasArg("id") ? strtoul(s_http.arg("id").c_str(), nullptr, 10) : 0;
  const Cmd* m = nullptr;
  for (const Cmd& c : s_cmds)
    if (id && c.id == id) m = &c;
  if (!m) {
    snprintf(s_buf, sizeof s_buf, "{\"id\":%lu,\"state\":\"unknown\"}", (unsigned long)id);
    sendJson(200, s_buf);
    return;
  }
  noCache();
  s_http.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_http.send(200, "application/json", "");
  snprintf(s_buf, sizeof s_buf, "{\"id\":%lu,\"state\":\"%s\",\"gap\":%s,\"age_ms\":%lu,\"text\":\"",
           (unsigned long)m->id, m->done ? "done" : "pending", m->gap ? "true" : "false",
           (unsigned long)(millis() - m->sentAt));
  s_http.sendContent(s_buf);
  // JSON-escape the reply in pieces that fit s_buf.
  size_t o = 0;
  for (size_t i = 0; i < m->len; ++i) {
    const char ch = m->text[i];
    if (o > sizeof s_buf - 8) {
      s_buf[o] = '\0';
      s_http.sendContent(s_buf);
      o = 0;
    }
    if (ch == '"' || ch == '\\') {
      s_buf[o++] = '\\';
      s_buf[o++] = ch;
    } else if (ch == '\n') {
      s_buf[o++] = '\\';
      s_buf[o++] = 'n';
    } else if (static_cast<uint8_t>(ch) >= 32 && static_cast<uint8_t>(ch) < 127) {
      s_buf[o++] = ch;
    }  // drop \r, tabs and anything else unprintable
  }
  s_buf[o] = '\0';
  s_http.sendContent(s_buf);
  s_http.sendContent("\"}");
  s_http.sendContent("");
}

// Connectivity probes. Answering them the way a real internet connection would
// keeps phones and laptops from nagging about "no internet" or dropping the
// network, and stops the captive-portal sheet from opening. While connected to
// the receiver, other apps on the device won't reach the internet.
void handleNoContent() { s_http.send(204, "text/plain", ""); }
void handleSuccessHtml() {
  s_http.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}
void handleConnectTest() { s_http.send(200, "text/plain", "Microsoft Connect Test"); }
void handleNcsi() { s_http.send(200, "text/plain", "Microsoft NCSI"); }
void handleSuccessText() { s_http.send(200, "text/plain", "success"); }

// DNS answers every name with our address, so any URL typed into the browser
// lands on the app.
void handleNotFound() {
  if (s_http.uri().startsWith("/api/")) {
    s_http.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }
  s_http.sendHeader("Location", String("http://") + s_ip + "/", true);
  s_http.send(302, "text/plain", "");
}

}  // namespace

void begin(void (*onClear)(), uint8_t (*sendCmd)(const char* text, int seq)) {
  s_onClear = onClear;
  s_sendCmd = sendCmd;
  s_rxBoot = (esp_random() & 0x00ffffffu) | 1u;

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  const char* pass = OG_WIFI_PASS;
  const bool open = strlen(pass) < 8;
  if (open && strlen(pass)) Serial.println("wifi: password shorter than 8 characters, starting an open network");
  WiFi.softAP(OG_WIFI_SSID, open ? nullptr : pass, kChannel, 0, kMaxClients);
  delay(100);
  strlcpy(s_ip, WiFi.softAPIP().toString().c_str(), sizeof s_ip);

  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());
  if (MDNS.begin(OG_WIFI_HOST)) MDNS.addService("http", "tcp", 80);

  s_http.on("/", HTTP_GET, handleIndex);
  s_http.on("/index.html", HTTP_GET, handleIndex);
  s_http.on("/api/shots", HTTP_GET, handleShots);
  s_http.on("/api/clear", HTTP_POST, handleClear);
  s_http.on("/api/cmd", HTTP_POST, handleCmdPost);
  s_http.on("/api/cmd", HTTP_GET, handleCmdGet);
  s_http.on("/generate_204", handleNoContent);               // Android
  s_http.on("/gen_204", handleNoContent);
  s_http.on("/hotspot-detect.html", handleSuccessHtml);      // iOS / macOS
  s_http.on("/library/test/success.html", handleSuccessHtml);
  s_http.on("/connecttest.txt", handleConnectTest);          // Windows
  s_http.on("/ncsi.txt", handleNcsi);
  s_http.on("/canonical.html", handleSuccessText);           // Firefox
  s_http.on("/success.txt", handleSuccessText);
  s_http.onNotFound(handleNotFound);
  s_http.begin();

  Serial.printf("wifi: network \"%s\" (%s), open http://%s/ or http://%s.local/\n", OG_WIFI_SSID,
                open ? "open" : "WPA2", s_ip, OG_WIFI_HOST);
  Serial.printf("{\"type\":\"wifi\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rxboot\":%lu}\n", OG_WIFI_SSID, s_ip,
                (unsigned long)s_rxBoot);
}

void poll() {
  s_dns.processNextRequest();
  s_http.handleClient();
}

void recordShot(const og::ShotPacket& p, float rssi, float snr) {
  Rec& r = s_log[s_logHead];
  r.seq = ++s_seq;
  r.t_ms = millis();
  r.boot = p.boot_id;
  r.shot = p.shot;
  r.rms_ns = p.rms_ns;
  r.batt_mv = p.batt_mv;
  r.x_dmm = p.x_dmm;
  r.y_dmm = p.y_dmm;
  r.fired = p.fired;
  r.used = p.used;
  r.flags = p.flags;
  r.rssi = rssi;
  r.snr = snr;
  s_logHead = (s_logHead + 1) % kLogSize;
  if (s_logCount < kLogSize) ++s_logCount;

  s_tgt.heardAt = millis();
  s_tgt.boot = p.boot_id;
  s_tgt.batt_mv = p.batt_mv;
  s_tgt.rssi = rssi;
}

void recordStatus(const og::StatusPacket& st, float rssi) {
  s_tgt.heardAt = millis();
  s_tgt.haveStatus = true;
  s_tgt.boot = st.boot_id;
  s_tgt.batt_mv = st.batt_mv;
  s_tgt.shots = st.shots;
  s_tgt.rejects = st.rejects;
  s_tgt.n_ch = st.n_ch;
  s_tgt.mode = st.mode;
  s_tgt.rssi = rssi;
}

void recordReject(const og::RejectPacket& r, float rssi) {
  s_rej.id++;
  s_rej.t_ms = millis();
  s_rej.boot = r.boot_id;
  s_rej.rejects = r.rejects;
  s_rej.fired = r.fired;
  s_rej.n_ch = r.n_ch;
  s_rej.need = r.need;

  s_tgt.heardAt = millis();
  if (s_tgt.boot != r.boot_id) s_tgt.shots = 0;  // the target restarted since its last status
  s_tgt.boot = r.boot_id;
  s_tgt.rejects = r.rejects;
  if (r.n_ch) s_tgt.n_ch = r.n_ch;
  s_tgt.rssi = rssi;
}

void recordResponse(uint8_t seq, uint8_t part, const char* text) {
  // Newest pending command with this seq (seq is 8 bits, so it wraps).
  Cmd* m = nullptr;
  const uint32_t now = millis();
  for (Cmd& c : s_cmds)
    if (c.id && !c.done && c.seq == seq && now - c.sentAt < kReplyMatchMs && (!m || c.id > m->id)) m = &c;
  if (!m) return;  // a serial-console command, or a reply we gave up on
  const uint8_t n = part & 0x7F;
  if (n < m->nextPart) return;  // repeated chunk
  if (n > m->nextPart) m->gap = true;
  const size_t add = strnlen(text, og::kMaxText);
  const size_t room = kReplyCap - 1 - m->len;
  const size_t k = add < room ? add : room;
  memcpy(m->text + m->len, text, k);
  m->len += k;
  m->text[m->len] = '\0';
  m->nextPart = n + 1;
  if (part & og::kPartLast) m->done = true;
}

void clearLog() {
  s_logHead = s_logCount = 0;
  ++s_clears;
}

const char* ssid() { return OG_WIFI_SSID; }
const char* ipAddress() { return s_ip; }

}  // namespace webui
