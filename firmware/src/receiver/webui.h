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
//   GET  /api/shots?since=<seq>  shots with seq > since, target status, counters
//   POST /api/clear              drop the ring buffer, bump the clear counter
#pragma once

#include <stdint.h>

#include "OpenGongProto.h"

namespace webui {

// Starts the access point, DNS, mDNS and HTTP server. onClear is called when a
// browser clears the group, so the caller can reset its own group statistics.
void begin(void (*onClear)());

// Call from loop(). Serves HTTP and DNS requests.
void poll();

// Store a de-duplicated shot / the latest target status for the browsers.
void recordShot(const og::ShotPacket& p, float rssi, float snr);
void recordStatus(const og::StatusPacket& st, float rssi);

// Drop the stored shots (for the serial "rx clear" command). Browsers notice
// the clear counter change on their next poll and clear their own copies.
void clearLog();

const char* ssid();
const char* ipAddress();

}  // namespace webui
