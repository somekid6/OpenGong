#include "config.h"

#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

Config g_cfg;
int g_activeCh = 5;

namespace {
constexpr uint32_t kMagic = 0x4F474332;  // "OGC2": ring-centred front-view frame
constexpr const char* kNamespace = "opengong";
constexpr const char* kKey = "cfg";
}  // namespace

void configDefaults(Config& c) {
  memset(&c, 0, sizeof c);
  c.magic = kMagic;
  c.size = sizeof(Config);
  c.n_ch = 5;
  c.min_ch = 4;

  // Default 120 mm jig ring (System-Design 8, step 1): front-view coordinates,
  // origin at the ring centre, +y up. Channel n = sensor Sn.
  static const float kRing[5][2] = {
      {-70.53f, 97.08f}, {70.53f, 97.08f}, {114.13f, -37.08f}, {0.0f, -120.0f}, {-114.13f, -37.08f}};
  for (int i = 0; i < 5; ++i) {
    c.sx[i] = kRing[i][0];
    c.sy[i] = kRing[i][1];
  }
  c.sx[5] = 0.0f;  // optional 6th sensor: set its real position before use
  c.sy[5] = 0.0f;

  c.speed_mps = 5900.0f;
  c.thick_mm = 9.5f;  // 3/8 in AR500
  c.aim_x = 0.0f;
  c.aim_y = 0.0f;
  c.flip_x = 0;  // coordinates are already front view
  c.flip_y = 0;
  c.window_us = 200;
  c.holdoff_ms = 250;
  c.resid_limit_ns = 500;
}

bool configValidate(const Config& c, char* why, size_t whyLen) {
  auto fail = [&](const char* msg) {
    if (why && whyLen) snprintf(why, whyLen, "%s", msg);
    return false;
  };
  if (c.magic != kMagic || c.size != sizeof(Config)) return fail("bad header");
  if (c.n_ch < 3 || c.n_ch > og::kMaxChannels) return fail("n must be 3-6");
  if (c.min_ch < 3 || c.min_ch > c.n_ch) return fail("minch must be 3..n");
  if (!(c.speed_mps > 500 && c.speed_mps < 20000)) return fail("v out of range");
  if (!(c.thick_mm >= 0 && c.thick_mm < 100)) return fail("h out of range");
  if (c.window_us < 20 || c.window_us > 5000) return fail("win must be 20-5000 us");
  if (c.holdoff_ms > 5000) return fail("hold must be <= 5000 ms");
  for (int i = 0; i < c.n_ch; ++i) {
    if (!isfinite(c.sx[i]) || !isfinite(c.sy[i]) || !isfinite(c.delay_ns[i]))
      return fail("non-finite sensor value");
    for (int j = 0; j < i; ++j) {
      if (hypotf(c.sx[i] - c.sx[j], c.sy[i] - c.sy[j]) < 5.0f) return fail("two sensors at the same position");
    }
  }
  return true;
}

bool configLoad(Config& c) {
  Preferences prefs;
  bool ok = false;
  if (prefs.begin(kNamespace, true)) {
    if (prefs.getBytesLength(kKey) == sizeof(Config)) {
      Config tmp;
      prefs.getBytes(kKey, &tmp, sizeof tmp);
      if (configValidate(tmp, nullptr, 0)) {
        c = tmp;
        ok = true;
      }
    }
    prefs.end();
  }
  if (!ok) configDefaults(c);
  return ok;
}

bool configSave(const Config& c) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const size_t n = prefs.putBytes(kKey, &c, sizeof c);
  prefs.end();
  return n == sizeof c;
}
