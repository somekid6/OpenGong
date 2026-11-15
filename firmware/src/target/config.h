// Persistent target configuration (stored in NVS, one key per setting).
#pragma once

#include <Print.h>
#include <stddef.h>
#include <stdint.h>

#include "OpenGongProto.h"

// Coordinates are millimetres in the jig's front-view frame (System-Design
// 4.1): origin at the ring centre, +y up as the plate hangs, x as seen by the
// shooter (x_front = -x_model, since the jig model is drawn from the back).
//
// Adding a field: give it a default in configDefaults() and its own key in
// readKeys()/writeKeys() in config.cpp. Stored settings survive the update.
struct Config {
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

// Where the settings in use came from.
enum class ConfigSource : uint8_t {
  Saved,     // read from flash
  Migrated,  // read from an older firmware's format and converted
  Defaults,  // nothing stored: built-in defaults, not calibrated
  Rejected,  // something was stored but could not be used: defaults instead
};

extern Config g_cfg;
extern ConfigSource g_cfgSource;  // set at boot, becomes Saved after a 'save'

// Channel count the capture hardware was started with (n_ch at boot).
extern int g_activeCh;

void configDefaults(Config& c);
// Always fills c (with defaults when nothing usable is stored). why gets the
// reason for Rejected, or a note for other sources; empty when there is none.
ConfigSource configLoad(Config& c, char* why, size_t whyLen);
bool configSave(const Config& c);
bool configValidate(const Config& c, char* why, size_t whyLen);
// True when the live settings started from flash, not from the built-in defaults.
bool configUsingSaved();
// Prints the settings as console commands that restore them.
void configExport(const Config& c, Print& o);
