// OpenGong shared definitions: board pins, LoRa settings and the radio packet format.
// Used by both the target and the receiver firmware. No Arduino dependencies.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Network ID: change (build flag -DOG_NET_ID=n) if two OpenGong systems share a range.
#ifndef OG_NET_ID
#define OG_NET_ID 1
#endif

namespace og {

constexpr int kMaxChannels = 6;  // 2 MCPWM groups x 3 capture inputs

// ---------------------------------------------------------------------------
// LoRa PHY settings. Both boards must match.
// ---------------------------------------------------------------------------
constexpr float    kLoraFreqMHz  = 915.0f;  // US ISM band. Use 868.0 in the EU.
constexpr float    kLoraBwKHz    = 125.0f;
constexpr uint8_t  kLoraSF       = 8;       // design range: SF 7-9
constexpr uint8_t  kLoraCR       = 5;       // coding rate 4/5
constexpr uint8_t  kLoraSyncWord = 0x12;    // private LoRa network
constexpr int8_t   kLoraPowerDbm = 17;      // design range: +14 to +20 dBm
constexpr uint16_t kLoraPreamble = 8;
constexpr float    kLoraTcxoV    = 1.6f;    // Heltec V3 radio TCXO supply

// ---------------------------------------------------------------------------
// Heltec WiFi LoRa 32 V3 fixed wiring.
// ---------------------------------------------------------------------------
namespace heltec {
constexpr int kLoraNss   = 8;
constexpr int kLoraSck   = 9;
constexpr int kLoraMosi  = 10;
constexpr int kLoraMiso  = 11;
constexpr int kLoraRst   = 12;
constexpr int kLoraBusy  = 13;
constexpr int kLoraDio1  = 14;
constexpr int kOledSda   = 17;
constexpr int kOledScl   = 18;
constexpr int kOledRst   = 21;
constexpr int kVext      = 36;  // LOW = OLED/Vext power on
constexpr int kLed       = 35;
constexpr int kButton    = 0;
constexpr int kVbatAdc   = 1;
constexpr int kVbatCtrl  = 37;  // enables the battery divider
}  // namespace heltec

// ---------------------------------------------------------------------------
// Packets. All multi-byte fields are little-endian; every packet ends in a
// CRC-16/CCITT-FALSE over all preceding bytes.
// ---------------------------------------------------------------------------
constexpr uint8_t kMagic = 0xA6;
constexpr uint8_t kNetId = OG_NET_ID;

enum PacketType : uint8_t {
  PKT_SHOT   = 1,  // target -> receiver, acknowledged
  PKT_ACK    = 2,  // receiver -> target
  PKT_STATUS = 3,  // target -> receiver heartbeat, not acknowledged
  PKT_CMD    = 4,  // receiver -> target console command (text)
  PKT_RESP   = 5,  // target -> receiver console output (text, may be split)
};

enum ShotFlags : uint8_t {
  SHOT_MISSING_CH   = 0x01,  // fewer channels fired than are configured
  SHOT_HIGH_RESID   = 0x02,  // residual above limit: likely a bad trigger
  SHOT_DROPPED_CH   = 0x04,  // worst channel discarded by the solver
  SHOT_OUTSIDE_RING = 0x08,  // outside the sensor ring: reduced accuracy
  SHOT_NO_FIX       = 0x10,  // solver failed; x/y are meaningless
  SHOT_CAL          = 0x20,  // calibration tap
  SHOT_SIM          = 0x40,  // synthetic shot from the "sim" command
};

struct __attribute__((packed)) PacketHeader {
  uint8_t magic;
  uint8_t net;
  uint8_t type;
};

struct __attribute__((packed)) ShotPacket {
  PacketHeader hdr;
  uint16_t boot_id;  // random per target boot, so shot numbers can restart
  uint16_t shot;     // shot number since boot, starts at 1
  int16_t  x_dmm;    // 0.1 mm, relative to the aim point, shooter's view
  int16_t  y_dmm;
  uint16_t rms_ns;   // RMS timing residual
  uint8_t  used;     // bitmask: channels used in the solution
  uint8_t  fired;    // bitmask: channels that triggered inside the window
  uint8_t  flags;    // ShotFlags
  uint16_t batt_mv;
  uint16_t crc;
};

struct __attribute__((packed)) AckPacket {
  PacketHeader hdr;
  uint16_t boot_id;
  uint16_t shot;
  uint16_t crc;
};

struct __attribute__((packed)) StatusPacket {
  PacketHeader hdr;
  uint16_t boot_id;
  uint16_t shots;
  uint16_t rejects;
  uint16_t batt_mv;
  uint16_t uptime_min;
  uint8_t  n_ch;
  uint8_t  mode;  // 0 = normal, 1 = calibration
  uint16_t crc;
};

// Text packets (CMD / RESP): header, seq, part, text bytes, crc.
constexpr size_t kMaxText = 200;
constexpr uint8_t kPartLast = 0x80;  // set in "part" on the final chunk
constexpr size_t kTextOverhead = sizeof(PacketHeader) + 2 + 2;
constexpr size_t kMaxPacket = kTextOverhead + kMaxText;

uint16_t crc16(const uint8_t* data, size_t len);

void initHeader(PacketHeader& h, PacketType type);

// Fills in the trailing crc field of a fixed-size packet struct.
template <typename T>
inline void seal(T& pkt) {
  pkt.crc = crc16(reinterpret_cast<const uint8_t*>(&pkt), sizeof(T) - 2);
}

// True if the buffer has our magic, our network ID and a valid CRC.
bool packetValid(const uint8_t* buf, size_t len);

inline PacketType packetType(const uint8_t* buf) {
  return static_cast<PacketType>(buf[2]);
}

// Builds a CMD or RESP packet; returns its length. text is truncated to kMaxText.
size_t buildText(uint8_t* out, PacketType type, uint8_t seq, uint8_t part,
                 const char* text, size_t len);

// Parses a validated CMD/RESP packet. text is NUL-terminated.
bool parseText(const uint8_t* buf, size_t len, uint8_t& seq, uint8_t& part,
               char* text, size_t cap);

}  // namespace og
