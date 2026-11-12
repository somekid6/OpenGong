// MCPWM capture: each comparator rising edge latches the 80 MHz capture timer
// in hardware, so interrupt latency does not affect the timestamp.
//
// The ESP32-S3 has two MCPWM groups, each with its own capture timer and three
// capture channels. Before every arm, one GPIO edge (kSyncPin, driven
// internally) resets both timers to zero simultaneously. Both run from the same
// APB clock, so their counts are directly comparable afterwards.
#include "capture.h"

#include "config.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr int kChPerGroup = 3;
constexpr uint16_t kRejectHoldoffMs = 20;  // re-arm quickly after a lone noise trigger
constexpr int kTaskCore = 1;
constexpr UBaseType_t kTaskPrio = configMAX_PRIORITIES - 2;

mcpwm_cap_timer_handle_t s_timer[2] = {};
mcpwm_sync_handle_t s_sync[2] = {};
mcpwm_cap_channel_handle_t s_chan[og::kMaxChannels] = {};
int s_pins[og::kMaxChannels] = {};
int s_nch = 0;
int s_syncPin = -1;
uint32_t s_resHz = 80000000;

TaskHandle_t s_task = nullptr;
QueueHandle_t s_queue = nullptr;
SemaphoreHandle_t s_ready = nullptr;
bool s_ok = false;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_armed = false;
volatile uint8_t s_mask = 0;
volatile uint32_t s_ticks[og::kMaxChannels] = {};
volatile uint32_t s_edges[og::kMaxChannels] = {};

// ISR: keep only the first edge per channel; wake the task on the first one.
bool IRAM_ATTR onCapture(mcpwm_cap_channel_handle_t, const mcpwm_capture_event_data_t* ed, void* ctx) {
  const int ch = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
  const uint8_t bit = static_cast<uint8_t>(1u << ch);
  BaseType_t woken = pdFALSE;
  bool first = false;

  portENTER_CRITICAL_ISR(&s_mux);
  if (s_armed) {
    s_edges[ch] = s_edges[ch] + 1;
    if (!(s_mask & bit)) {
      s_ticks[ch] = ed->cap_value;
      first = (s_mask == 0);
      s_mask = s_mask | bit;
    }
  }
  portEXIT_CRITICAL_ISR(&s_mux);

  if (first) vTaskNotifyGiveFromISR(s_task, &woken);
  return woken == pdTRUE;
}

#define CAP_CHECK(expr)                                                              \
  do {                                                                               \
    esp_err_t e_ = (expr);                                                           \
    if (e_ != ESP_OK) {                                                              \
      Serial.printf("capture: %s failed: %s\n", #expr, esp_err_to_name(e_));         \
      return false;                                                                  \
    }                                                                                \
  } while (0)

bool setupHardware() {
  const int groups = (s_nch + kChPerGroup - 1) / kChPerGroup;

  for (int g = 0; g < groups; ++g) {
    mcpwm_capture_timer_config_t tc = {};
    tc.group_id = g;
    tc.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;
    CAP_CHECK(mcpwm_new_capture_timer(&tc, &s_timer[g]));

    mcpwm_gpio_sync_src_config_t sc = {};
    sc.group_id = g;
    sc.gpio_num = s_syncPin;
    CAP_CHECK(mcpwm_new_gpio_sync_src(&sc, &s_sync[g]));

    mcpwm_capture_timer_sync_phase_config_t pc = {};
    pc.sync_src = s_sync[g];
    pc.count_value = 0;
    pc.direction = MCPWM_TIMER_DIRECTION_UP;
    CAP_CHECK(mcpwm_capture_timer_set_phase_on_sync(s_timer[g], &pc));
  }

  // The sync sources configured the pin as an input; also enable its output so
  // the firmware can generate the sync edge itself (input stays routed to both groups).
  gpio_set_direction(static_cast<gpio_num_t>(s_syncPin), GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(s_syncPin), 0);

  for (int ch = 0; ch < s_nch; ++ch) {
    mcpwm_capture_channel_config_t cc = {};
    cc.gpio_num = s_pins[ch];
    cc.prescale = 1;
    cc.flags.pos_edge = true;   // comparator fires on the rising edge (Wiring-Diagram section 6)
    cc.flags.neg_edge = false;
    cc.flags.pull_down = true;  // quiet when the analog board is unplugged
    CAP_CHECK(mcpwm_new_capture_channel(s_timer[ch / kChPerGroup], &cc, &s_chan[ch]));

    mcpwm_capture_event_callbacks_t cbs = {};
    cbs.on_cap = onCapture;
    CAP_CHECK(mcpwm_capture_channel_register_event_callbacks(
        s_chan[ch], &cbs, reinterpret_cast<void*>(static_cast<intptr_t>(ch))));
    CAP_CHECK(mcpwm_capture_channel_enable(s_chan[ch]));
  }

  for (int g = 0; g < groups; ++g) {
    CAP_CHECK(mcpwm_capture_timer_enable(s_timer[g]));
    CAP_CHECK(mcpwm_capture_timer_start(s_timer[g]));
  }
  CAP_CHECK(mcpwm_capture_timer_get_resolution(s_timer[0], &s_resHz));
  return true;
}

void syncTimers() {
  const gpio_num_t pin = static_cast<gpio_num_t>(s_syncPin);
  gpio_set_level(pin, 0);
  esp_rom_delay_us(1);
  gpio_set_level(pin, 1);  // rising edge resets both capture timers to 0
  esp_rom_delay_us(1);
  gpio_set_level(pin, 0);
}

void setChannelsEnabled(bool on) {
  for (int ch = 0; ch < s_nch; ++ch) {
    if (on) mcpwm_capture_channel_enable(s_chan[ch]);
    else mcpwm_capture_channel_disable(s_chan[ch]);
  }
}

void arm() {
  syncTimers();
  portENTER_CRITICAL(&s_mux);
  s_mask = 0;
  portEXIT_CRITICAL(&s_mux);
  ulTaskNotifyTake(pdTRUE, 0);  // drop any stale notification
  portENTER_CRITICAL(&s_mux);
  s_armed = true;
  portEXIT_CRITICAL(&s_mux);
}

void captureTask(void*) {
  // Interrupts are allocated on the core that registers them: this task's core.
  s_ok = setupHardware();
  xSemaphoreGive(s_ready);
  if (!s_ok) {
    vTaskDelete(nullptr);
    return;
  }

  arm();
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // first edge of a new event

    // Wait out the window so every channel has had time to fire (the wave
    // crosses the ring in well under 100 us; default window 200 us).
    const uint16_t windowUs = g_cfg.window_us;
    esp_rom_delay_us(windowUs + 20);

    RawEvent ev = {};
    portENTER_CRITICAL(&s_mux);
    s_armed = false;
    ev.mask = s_mask;
    for (int i = 0; i < s_nch; ++i) ev.ticks[i] = s_ticks[i];
    portEXIT_CRITICAL(&s_mux);

    // Stop interrupts while the plate rings.
    setChannelsEnabled(false);

    const uint32_t winTicks = static_cast<uint32_t>(static_cast<uint64_t>(s_resHz) * windowUs / 1000000u);
    const int inWindow = __builtin_popcount(captureWindowMask(ev, s_nch, winTicks));
    xQueueSend(s_queue, &ev, 0);

    const uint16_t hold = inWindow >= g_cfg.min_ch ? g_cfg.holdoff_ms : kRejectHoldoffMs;
    vTaskDelay(pdMS_TO_TICKS(hold));

    setChannelsEnabled(true);
    arm();
  }
}

}  // namespace

bool captureBegin(const int* pins, int nch, int syncPin) {
  if (nch < 1 || nch > og::kMaxChannels) return false;
  s_nch = nch;
  s_syncPin = syncPin;
  for (int i = 0; i < nch; ++i) s_pins[i] = pins[i];

  s_queue = xQueueCreate(8, sizeof(RawEvent));
  s_ready = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(captureTask, "capture", 4096, nullptr, kTaskPrio, &s_task, kTaskCore);
  xSemaphoreTake(s_ready, portMAX_DELAY);
  return s_ok;
}

bool captureNext(RawEvent& ev, TickType_t wait) {
  return s_queue && xQueueReceive(s_queue, &ev, wait) == pdTRUE;
}

uint32_t captureResolutionHz() { return s_resHz; }

void captureEdgeCounts(uint32_t out[og::kMaxChannels]) {
  portENTER_CRITICAL(&s_mux);
  for (int i = 0; i < og::kMaxChannels; ++i) out[i] = s_edges[i];
  portEXIT_CRITICAL(&s_mux);
}

bool captureArmed() { return s_armed; }

uint8_t captureWindowMask(const RawEvent& ev, int nch, uint32_t windowTicks) {
  int ref = -1;
  for (int i = 0; i < nch; ++i) {
    if (ev.mask & (1u << i)) {
      ref = i;
      break;
    }
  }
  if (ref < 0) return 0;

  int32_t d[og::kMaxChannels] = {};
  int32_t minD = 0;
  for (int i = 0; i < nch; ++i) {
    if (!(ev.mask & (1u << i))) continue;
    d[i] = static_cast<int32_t>(ev.ticks[i] - ev.ticks[ref]);  // wrap-safe
    if (d[i] < minD) minD = d[i];
  }
  uint8_t out = 0;
  for (int i = 0; i < nch; ++i) {
    if ((ev.mask & (1u << i)) && static_cast<uint32_t>(d[i] - minD) <= windowTicks) out |= (1u << i);
  }
  return out;
}
