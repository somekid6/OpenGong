// Receiver Wi-Fi access point and web UI.
//
// The receiver brings up its own Wi-Fi network and serves a single-page app
// (src/receiver/web/index.html, gzipped into web_page.h at build time) that
// draws the plate to scale, overlays the hits and an MOA/MIL grid, and keeps
// the shot history in the phone's or laptop's browser storage. The receiver
// itself only holds a short RAM ring buffer so a page that was closed for a
// moment can catch up; nothing is written to flash.
//
// HTTP API (JSON):
//   GET  /api/shots?since=<seq>  shots with seq > since, target status, counters, and
//                                the latest reject notice ("reject", once one has arrived)
//   POST /api/clear              drop the ring buffer, bump the clear counter
//   POST /api/cmd   (form c=...) relay one console line to the target -> { ok, id }
//   POST /api/cmd   (retry=<id>) resend that command with its original seq -> { ok, id }
//   GET  /api/cmd?id=<id>        the target's reply -> { id, state, text, gap, age_ms }
//                                state: "pending" | "done" | "unknown" (too old or never sent)
//
// Console commands over LoRa are not acknowledged. If no reply arrives, the
// command may or may not have reached the target, so resend it with retry=<id>:
// the target runs each seq once and answers a repeat with its saved reply.
#pragma once

#include <stdint.h>

#include "OpenGongProto.h"

namespace webui {

// Starts the access point, DNS, mDNS and HTTP server. onClear is called when a
// browser clears the group, so the caller can reset its own group statistics.
// sendCmd transmits a console line to the target and returns the packet seq it
// used: a new seq when seq is -1, otherwise that seq again (a retry).
void begin(void (*onClear)(), uint8_t (*sendCmd)(const char* text, int seq));

// Call from loop(). Serves HTTP and DNS requests.
void poll();

// Store a de-duplicated shot / the latest target status for the browsers.
void recordShot(const og::ShotPacket& p, float rssi, float snr);
void recordStatus(const og::StatusPacket& st, float rssi);

// An impact the target heard but couldn't place (too few sensors fired).
void recordReject(const og::RejectPacket& r, float rssi);

// One chunk of the target's console reply (PKT_RESP), matched to the command by seq.
void recordResponse(uint8_t seq, uint8_t part, const char* text);

// Drop the stored shots (for the serial "rx clear" command). Browsers notice
// the clear counter change on their next poll and clear their own copies.
void clearLog();

const char* ssid();
const char* ipAddress();

}  // namespace webui
