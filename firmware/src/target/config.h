// Persistent target configuration (stored in NVS).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "OpenGongProto.h"

// Coordinates are millimetres in the jig's front-view frame (System-Design
// 4.1): origin at the ring centre, +y up as the plate hangs, x as seen by the
// shooter (x_front = -x_model, since the jig model is drawn from the back).
struct Config {
  uint32_t magic;
  uint16_t size;
  uint8_t  n_ch;            // configured sensor channels (3-5)
  uint8_t  min_ch;          // channels required to accept a shot
  float    sx[og::kMaxChannels];        // sensor positions, mm
  float    sy[og::kMaxChannels];
  float    delay_ns[og::kMaxChannels];  // fixed per-channel delay from calibration
  float    speed_mps;       // plate wave speed from calibration
  float    thick_mm;        // plate thickness
  float    aim_x, aim_y;    // point reported as (0, 0)
  uint8_t  flip_x, flip_y;  // optionally mirror reported axes (default off)
  uint16_t window_us;       // capture window after the first trigger
  uint16_t holdoff_ms;      // ignore the plate while it rings
  uint16_t resid_limit_ns;  // flag shots above this RMS residual
};

extern Config g_cfg;

// Channel count the capture hardware was started with (n_ch at boot).
extern int g_activeCh;

void configDefaults(Config& c);
bool configLoad(Config& c);  // false if nothing valid was stored (defaults applied)
bool configSave(const Config& c);
bool configValidate(const Config& c, char* why, size_t whyLen);
