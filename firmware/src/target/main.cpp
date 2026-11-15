// OpenGong target firmware: Heltec WiFi LoRa 32 V3 behind the plate.
//
//   comparator TRIG lines -> MCPWM hardware capture (capture.cpp, core 1)
//   -> windowing + LM solver (processor.cpp, loop task)
//   -> USB serial JSON + LoRa to the receiver (link.cpp, core 0)
#include <Arduino.h>
#include <string.h>

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
BufferPrint s_resp;  // also kept as the reply to resend for a repeated command

// The last command received over LoRa. Commands aren't acknowledged, so when a
// reply is lost the receiver resends the command with the same seq. A repeat
// gets the saved reply again instead of being run twice (a second "cal at"
// would add an extra tap, a second "cal undo" would drop one too many).
constexpr uint32_t kRepeatWindowMs = 60000;  // well past the web app's retries
struct {
  bool     have = false;
  uint8_t  seq = 0;
  uint32_t at = 0;
  char     text[og::kMaxText + 1] = {};
} s_lastCmd;

bool isRepeat(uint8_t seq, const char* text) {
  return s_lastCmd.have && seq == s_lastCmd.seq && !strcmp(text, s_lastCmd.text) &&
         millis() - s_lastCmd.at < kRepeatWindowMs;
}

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
    if (isRepeat(seq, cmd)) {
      Serial.printf("> [lora] %s (repeat, resending the earlier reply)\n", cmd);
      linkSendResponse(seq, s_resp.data(), s_resp.size());
      continue;
    }
    Serial.printf("> [lora] %s\n", cmd);
    s_lastCmd.have = true;
    s_lastCmd.seq = seq;
    s_lastCmd.at = millis();
    strlcpy(s_lastCmd.text, cmd, sizeof s_lastCmd.text);  // consoleExec tokenises cmd in place
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
