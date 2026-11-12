#include "OpenGongProto.h"

#include <string.h>

namespace og {

uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void initHeader(PacketHeader& h, PacketType type) {
  h.magic = kMagic;
  h.net = kNetId;
  h.type = type;
}

bool packetValid(const uint8_t* buf, size_t len) {
  if (len < sizeof(PacketHeader) + 2) return false;
  if (buf[0] != kMagic || buf[1] != kNetId) return false;
  const uint16_t got = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
  if (got != crc16(buf, len - 2)) return false;

  switch (packetType(buf)) {
    case PKT_SHOT:   return len == sizeof(ShotPacket);
    case PKT_ACK:    return len == sizeof(AckPacket);
    case PKT_STATUS: return len == sizeof(StatusPacket);
    case PKT_CMD:
    case PKT_RESP:   return len >= kTextOverhead && len <= kMaxPacket;
    default:         return false;
  }
}

size_t buildText(uint8_t* out, PacketType type, uint8_t seq, uint8_t part,
                 const char* text, size_t len) {
  if (len > kMaxText) len = kMaxText;
  PacketHeader h;
  initHeader(h, type);
  size_t n = 0;
  memcpy(out, &h, sizeof h);
  n += sizeof h;
  out[n++] = seq;
  out[n++] = part;
  memcpy(out + n, text, len);
  n += len;
  const uint16_t crc = crc16(out, n);
  out[n++] = static_cast<uint8_t>(crc & 0xFF);
  out[n++] = static_cast<uint8_t>(crc >> 8);
  return n;
}

bool parseText(const uint8_t* buf, size_t len, uint8_t& seq, uint8_t& part,
               char* text, size_t cap) {
  if (len < kTextOverhead || cap == 0) return false;
  seq = buf[sizeof(PacketHeader)];
  part = buf[sizeof(PacketHeader) + 1];
  size_t tlen = len - kTextOverhead;
  if (tlen > cap - 1) tlen = cap - 1;
  memcpy(text, buf + sizeof(PacketHeader) + 2, tlen);
  text[tlen] = '\0';
  return true;
}

}  // namespace og
