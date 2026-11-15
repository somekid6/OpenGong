// OpenGong target firmware: Heltec WiFi LoRa 32 V3 behind the plate.
//
//   comparator TRIG lines -> MCPWM hardware capture (capture.cpp, core 1)
//   -> windowing + LM solver (processor.cpp, loop task)
//   -> USB serial JSON + LoRa to the receiver (link.cpp, core 0)
#include <Arduino.h>

#include "OpenGongProto.h"
#include "board.h"
#include "capture.h"
#include "config.h"
#include "console.h"
#include "link.h"
#include "processor.h"
#include "ui.h"

namespace {

constexpr uint32_t kStatusPeriodMs = 30000;

uint16_t s_bootId = 0;
uint32_t s_lastStatus = 0;

// Collects console output for a LoRa response.
class BufferPrint : public Print {
 public:
  size_t write(uint8_t c) override {
    if (len_ < sizeof(buf_) - 1) buf_[len_++] = static_cast<char>(c);
    return 1;
  }
  void clear() { len_ = 0; }
  const char* data() const { return buf_; }
  size_t size() const { return len_; }

 private:
  char buf_[1600];
  size_t len_ = 0;
};
BufferPrint s_resp;

void sendStatus() {
  og::StatusPacket st = {};
  og::initHeader(st.hdr, og::PKT_STATUS);
  st.boot_id = s_bootId;
  st.shots = processorShotCount();
  st.rejects = processorRejectCount();
  st.batt_mv = uiBatteryMv();
  st.uptime_min = static_cast<uint16_t>(millis() / 60000);
  st.n_ch = static_cast<uint8_t>(g_activeCh);
  st.mode = (processorCalActive() ? og::STATUS_CAL : 0) | (configUsingSaved() ? 0 : og::STATUS_DEFAULTS);
  og::seal(st);
  linkSendStatus(st);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nOpenGong target");

  s_bootId = static_cast<uint16_t>(esp_random());
  char why[64];
  g_cfgSource = configLoad(g_cfg, why, sizeof why);
  switch (g_cfgSource) {
    case ConfigSource::Saved:
      Serial.println("config: loaded from flash");
      break;
    case ConfigSource::Migrated:
      Serial.println("config: converted settings from older firmware; calibration kept");
      break;
    case ConfigSource::Defaults:
      Serial.println("WARNING: config: nothing saved, using built-in defaults. Calibrate, then 'save'.");
      break;
    case ConfigSource::Rejected:
      Serial.println("WARNING: config: saved settings could not be used, running on built-in defaults.");
      Serial.println("         Calibrate again, or paste a backup from 'export', then 'save'.");
      break;
  }
  if (why[0]) Serial.printf("config: %s\n", why);
  g_activeCh = g_cfg.n_ch;

  uiBegin();
  processorBegin(s_bootId);

  if (!linkBegin()) Serial.println("WARNING: LoRa unavailable, results on USB serial only");

  if (!captureBegin(kTrigPins, g_activeCh, kSyncPin)) {
    Serial.println("FATAL: capture setup failed");
  } else {
    Serial.printf("capture: %d channels, %lu Hz timer (%.1f ns/tick)\n", g_activeCh,
                  (unsigned long)captureResolutionHz(), 1e9 / captureResolutionHz());
  }
  Serial.printf("{\"type\":\"boot\",\"boot\":%u,\"n_ch\":%d,\"net\":%u,\"freq_mhz\":%.1f,\"pwr_dbm\":%d}\n", s_bootId,
                g_activeCh, og::kNetId, og::kLoraFreqMHz, og::kLoraPowerDbm);
  Serial.println("type 'help' for commands");
  sendStatus();
  s_lastStatus = millis();
}

void loop() {
  RawEvent ev;
  while (captureNext(ev, 0)) processEvent(ev, 0);

  consolePollSerial();

  char cmd[og::kMaxText + 1];
  uint8_t seq;
  while (linkNextCommand(cmd, sizeof cmd, seq)) {
    Serial.printf("> [lora] %s\n", cmd);
    s_resp.clear();
    consoleExec(cmd, s_resp);
    Serial.write(reinterpret_cast<const uint8_t*>(s_resp.data()), s_resp.size());
    if (s_resp.size() == 0) s_resp.print("ok\n");
    linkSendResponse(seq, s_resp.data(), s_resp.size());
  }

  // Tell the receiver straight away when settings are saved or reset.
  static bool s_wasSaved = configUsingSaved();
  const bool saved = configUsingSaved();
  if (saved != s_wasSaved || millis() - s_lastStatus > kStatusPeriodMs) {
    s_wasSaved = saved;
    sendStatus();
    s_lastStatus = millis();
  }

  uiUpdate();

  if (consoleRebootRequested()) {
    delay(1500);  // let a LoRa response go out
    ESP.restart();
  }
  vTaskDelay(1);
}
