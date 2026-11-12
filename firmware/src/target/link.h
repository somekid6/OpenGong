// Target-side LoRa link (System-Design section 9). Runs in its own task on
// core 0 so radio traffic never delays capture or solving.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "OpenGongProto.h"

struct LinkStats {
  uint32_t shotsSent = 0;
  uint32_t shotsAcked = 0;
  uint32_t shotsFailed = 0;  // no ACK after all retries
  uint32_t transmissions = 0;
  float    lastRssi = 0;
  float    lastSnr = 0;
  bool     up = false;
};

bool linkBegin();
void linkSendShot(const og::ShotPacket& pkt);    // acknowledged, up to 3 resends
void linkSendStatus(const og::StatusPacket& pkt);
void linkSendResponse(uint8_t seq, const char* text, size_t len);
bool linkNextCommand(char* buf, size_t cap, uint8_t& seq);
LinkStats linkStats();
