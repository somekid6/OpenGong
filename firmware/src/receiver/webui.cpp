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

// Last-heard state of the target, from shots and status heartbeats.
struct {
  uint32_t heardAt = 0;  // millis(); 0 = never
  bool haveStatus = false;
  uint16_t boot = 0, batt_mv = 0, shots = 0, rejects = 0;
  uint8_t n_ch = 0, mode = 0;
  float rssi = 0;
} s_tgt;

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
             "\"mode\":\"%s\",\"n_ch\":%u,\"have_status\":%s}",
             (unsigned long)((now - s_tgt.heardAt) / 1000), s_tgt.boot, s_tgt.batt_mv, s_tgt.rssi, s_tgt.shots,
             s_tgt.rejects, s_tgt.mode ? "cal" : "normal", s_tgt.n_ch, s_tgt.haveStatus ? "true" : "false");
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

void begin(void (*onClear)()) {
  s_onClear = onClear;
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

void clearLog() {
  s_logHead = s_logCount = 0;
  ++s_clears;
}

const char* ssid() { return OG_WIFI_SSID; }
const char* ipAddress() { return s_ip; }

}  // namespace webui
