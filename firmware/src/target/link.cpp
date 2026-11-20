#include "link.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <string.h>

namespace {

using namespace og;

constexpr int kRetries = 3;          // resends after the first attempt
constexpr uint32_t kTextGapMs = 40;  // let the receiver re-enter RX between chunks

struct OutMsg {
  uint8_t  len;
  bool     needAck;
  uint16_t bootId;
  uint16_t shot;
  uint8_t  buf[kMaxPacket];
};

struct CmdMsg {
  uint8_t seq;
  char    text[kMaxText + 1];
};

SPIClass s_spi(HSPI);
SX1262* s_radio = nullptr;
QueueHandle_t s_out = nullptr;
QueueHandle_t s_cmd = nullptr;
portMUX_TYPE s_statsMux = portMUX_INITIALIZER_UNLOCKED;
LinkStats s_stats;
volatile bool s_irq = false;

void IRAM_ATTR onDio1() { s_irq = true; }

void startRx() {
  s_irq = false;
  s_radio->startReceive();
}

bool tx(const uint8_t* buf, size_t len) {
  const int st = s_radio->transmit(const_cast<uint8_t*>(buf), len);
  portENTER_CRITICAL(&s_statsMux);
  s_stats.transmissions++;
  portEXIT_CRITICAL(&s_statsMux);
  startRx();
  return st == RADIOLIB_ERR_NONE;
}

// Reads the pending packet. Returns its length if it is a valid OpenGong packet.
size_t readPacket(uint8_t* buf, size_t cap) {
  s_irq = false;
  const size_t len = s_radio->getPacketLength();
  int st = RADIOLIB_ERR_UNKNOWN;
  if (len > 0 && len <= cap) st = s_radio->readData(buf, len);
  if (st == RADIOLIB_ERR_NONE) {
    portENTER_CRITICAL(&s_statsMux);
    s_stats.lastRssi = s_radio->getRSSI();
    s_stats.lastSnr = s_radio->getSNR();
    portEXIT_CRITICAL(&s_statsMux);
  }
  startRx();
  if (st != RADIOLIB_ERR_NONE || !packetValid(buf, len)) return 0;
  return len;
}

// Handles anything that is not the ACK we are waiting for.
void handleIncoming(const uint8_t* buf, size_t len) {
  if (packetType(buf) != PKT_CMD) return;
  CmdMsg m;
  uint8_t part;
  if (parseText(buf, len, m.seq, part, m.text, sizeof m.text)) xQueueSend(s_cmd, &m, 0);
}

bool isAckFor(const uint8_t* buf, size_t len, uint16_t bootId, uint16_t shot) {
  if (packetType(buf) != PKT_ACK || len != sizeof(AckPacket)) return false;
  AckPacket a;
  memcpy(&a, buf, sizeof a);
  return a.boot_id == bootId && a.shot == shot;
}

// Listens for up to ms for the ACK of m, handling anything else that arrives.
bool waitForAck(const OutMsg& m, uint32_t ms) {
  const uint32_t start = millis();
  while (millis() - start < ms) {
    if (s_irq) {
      uint8_t b[kMaxPacket + 8];
      const size_t n = readPacket(b, sizeof b);
      if (n && isAckFor(b, n, m.bootId, m.shot)) return true;
      if (n) handleIncoming(b, n);
    }
    vTaskDelay(1);
  }
  return false;
}

void sendShot(const OutMsg& m) {
  const uint32_t ackWaitMs = s_radio->getTimeOnAir(sizeof(AckPacket)) / 1000 + 150;
  portENTER_CRITICAL(&s_statsMux);
  s_stats.shotsSent++;
  portEXIT_CRITICAL(&s_statsMux);

  for (int attempt = 0; attempt <= kRetries; ++attempt) {
    tx(m.buf, m.len);
    // The random back-off (to de-synchronise retries) is spent listening, so a
    // late ACK still counts instead of being lost when the radio goes back to TX.
    if (waitForAck(m, ackWaitMs + random(30, 150))) {
      portENTER_CRITICAL(&s_statsMux);
      s_stats.shotsAcked++;
      portEXIT_CRITICAL(&s_statsMux);
      return;
    }
  }
  portENTER_CRITICAL(&s_statsMux);
  s_stats.shotsFailed++;
  portEXIT_CRITICAL(&s_statsMux);
  Serial.printf("{\"type\":\"link\",\"shot\":%u,\"acked\":false}\n", m.shot);
}

void linkTask(void*) {
  for (;;) {
    OutMsg m;
    if (xQueueReceive(s_out, &m, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (m.needAck) {
        sendShot(m);
      } else {
        tx(m.buf, m.len);
        vTaskDelay(pdMS_TO_TICKS(kTextGapMs));
      }
    }
    if (s_irq) {
      uint8_t b[kMaxPacket + 8];
      const size_t n = readPacket(b, sizeof b);
      if (n) handleIncoming(b, n);
    }
  }
}

void enqueue(const void* data, size_t len, bool needAck, uint16_t bootId, uint16_t shot) {
  if (!s_out) return;
  OutMsg m;
  m.len = static_cast<uint8_t>(len);
  m.needAck = needAck;
  m.bootId = bootId;
  m.shot = shot;
  memcpy(m.buf, data, len);
  if (xQueueSend(s_out, &m, 0) != pdTRUE) Serial.println("link: transmit queue full, dropped packet");
}

}  // namespace

bool linkBegin() {
  using namespace og::heltec;
  s_spi.begin(kLoraSck, kLoraMiso, kLoraMosi, kLoraNss);
  s_radio = new SX1262(new Module(kLoraNss, kLoraDio1, kLoraRst, kLoraBusy, s_spi));

  const int st = s_radio->begin(kLoraFreqMHz, kLoraBwKHz, kLoraSF, kLoraCR, kLoraSyncWord, kLoraPowerDbm,
                                kLoraPreamble, kLoraTcxoV, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("link: SX1262 init failed (%d). Is the antenna/radio OK?\n", st);
    return false;
  }
  s_radio->setDio2AsRfSwitch(true);
  s_radio->setCRC(true);
  s_radio->setDio1Action(onDio1);

  s_out = xQueueCreate(16, sizeof(OutMsg));
  s_cmd = xQueueCreate(4, sizeof(CmdMsg));
  startRx();
  s_stats.up = true;
  xTaskCreatePinnedToCore(linkTask, "link", 6144, nullptr, 3, nullptr, 0);
  return true;
}

void linkSendShot(const og::ShotPacket& pkt) { enqueue(&pkt, sizeof pkt, true, pkt.boot_id, pkt.shot); }

void linkSendStatus(const og::StatusPacket& pkt) { enqueue(&pkt, sizeof pkt, false, 0, 0); }

void linkSendReject(const og::RejectPacket& pkt) { enqueue(&pkt, sizeof pkt, false, 0, 0); }

void linkSendResponse(uint8_t seq, const char* text, size_t len) {
  if (!s_out) return;
  uint8_t part = 0;
  size_t off = 0;
  do {
    const size_t n = (len - off > kMaxText) ? kMaxText : len - off;
    const bool last = off + n >= len;
    uint8_t buf[kMaxPacket];
    const size_t plen = buildText(buf, PKT_RESP, seq, static_cast<uint8_t>(part | (last ? kPartLast : 0)),
                                  text + off, n);
    enqueue(buf, plen, false, 0, 0);
    off += n;
    ++part;
  } while (off < len && part < 0x7F);
}

bool linkNextCommand(char* buf, size_t cap, uint8_t& seq) {
  if (!s_cmd) return false;
  CmdMsg m;
  if (xQueueReceive(s_cmd, &m, 0) != pdTRUE) return false;
  strncpy(buf, m.text, cap - 1);
  buf[cap - 1] = '\0';
  seq = m.seq;
  return true;
}

LinkStats linkStats() {
  portENTER_CRITICAL(&s_statsMux);
  LinkStats s = s_stats;
  portEXIT_CRITICAL(&s_statsMux);
  return s;
}
