#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

Config g_cfg;
int g_activeCh = 5;
ConfigSource g_cfgSource = ConfigSource::Defaults;

// Storage: one NVS key per setting, so a firmware that adds, removes or
// reorders fields in Config keeps every stored value it still knows about.
// A key that isn't stored yet takes its default. Bump kFormat only when the
// meaning of an existing key changes, and convert the old values in configLoad.
//
// Format 2 (firmware before this scheme) stored the whole struct as one blob
// under "cfg"; it is converted on first boot and left in place untouched.
namespace {

constexpr const char* kNamespace = "opengong";
constexpr const char* kFormatKey = "fmt";
constexpr uint8_t kFormat = 3;

// Format 2 blob, laid out exactly as the old firmware wrote it.
constexpr uint32_t kLegacyMagic = 0x4F474332;  // "OGC2": ring-centred front-view frame
constexpr const char* kLegacyKey = "cfg";
struct LegacyConfigV2 {
  uint32_t magic;
  uint16_t size;
  uint8_t  n_ch;
  uint8_t  min_ch;
  float    sx[5];
  float    sy[5];
  float    delay_ns[5];
  float    speed_mps;
  float    thick_mm;
  float    aim_x, aim_y;
  uint8_t  flip_x, flip_y;
  uint16_t window_us;
  uint16_t holdoff_ms;
  uint16_t resid_limit_ns;
};

void chKey(char* out, size_t cap, const char* prefix, int ch) { snprintf(out, cap, "%s%d", prefix, ch + 1); }

void readKeys(Preferences& p, Config& c) {
  c.n_ch = p.getUChar("n", c.n_ch);
  c.min_ch = p.getUChar("minch", c.min_ch);
  char k[8];
  for (int i = 0; i < og::kMaxChannels; ++i) {
    chKey(k, sizeof k, "sx", i);
    c.sx[i] = p.getFloat(k, c.sx[i]);
    chKey(k, sizeof k, "sy", i);
    c.sy[i] = p.getFloat(k, c.sy[i]);
    chKey(k, sizeof k, "d", i);
    c.delay_ns[i] = p.getFloat(k, c.delay_ns[i]);
  }
  c.speed_mps = p.getFloat("v", c.speed_mps);
  c.thick_mm = p.getFloat("h", c.thick_mm);
  c.aim_x = p.getFloat("aimx", c.aim_x);
  c.aim_y = p.getFloat("aimy", c.aim_y);
  c.flip_x = p.getUChar("flipx", c.flip_x);
  c.flip_y = p.getUChar("flipy", c.flip_y);
  c.window_us = p.getUShort("win", c.window_us);
  c.holdoff_ms = p.getUShort("hold", c.holdoff_ms);
  c.resid_limit_ns = p.getUShort("rlim", c.resid_limit_ns);
}

bool writeKeys(Preferences& p, const Config& c) {
  bool ok = true;
  ok &= p.putUChar("n", c.n_ch) > 0;
  ok &= p.putUChar("minch", c.min_ch) > 0;
  char k[8];
  for (int i = 0; i < og::kMaxChannels; ++i) {
    chKey(k, sizeof k, "sx", i);
    ok &= p.putFloat(k, c.sx[i]) > 0;
    chKey(k, sizeof k, "sy", i);
    ok &= p.putFloat(k, c.sy[i]) > 0;
    chKey(k, sizeof k, "d", i);
    ok &= p.putFloat(k, c.delay_ns[i]) > 0;
  }
  ok &= p.putFloat("v", c.speed_mps) > 0;
  ok &= p.putFloat("h", c.thick_mm) > 0;
  ok &= p.putFloat("aimx", c.aim_x) > 0;
  ok &= p.putFloat("aimy", c.aim_y) > 0;
  ok &= p.putUChar("flipx", c.flip_x) > 0;
  ok &= p.putUChar("flipy", c.flip_y) > 0;
  ok &= p.putUShort("win", c.window_us) > 0;
  ok &= p.putUShort("hold", c.holdoff_ms) > 0;
  ok &= p.putUShort("rlim", c.resid_limit_ns) > 0;
  // Written last, so a first save cut short by a power loss leaves no format
  // marker and the board doesn't trust half-written settings. Never lowered,
  // so a downgrade and save doesn't hide newer keys from newer firmware.
  if (p.getUChar(kFormatKey, 0) < kFormat) ok &= p.putUChar(kFormatKey, kFormat) > 0;
  return ok;
}

// Converts a format 2 blob. Returns false if there is none, or it is unreadable.
bool readLegacy(Preferences& p, Config& c) {
  if (p.getBytesLength(kLegacyKey) != sizeof(LegacyConfigV2)) return false;
  LegacyConfigV2 o;
  p.getBytes(kLegacyKey, &o, sizeof o);
  if (o.magic != kLegacyMagic || o.size != sizeof(LegacyConfigV2)) return false;
  c.n_ch = o.n_ch;
  c.min_ch = o.min_ch;
  for (int i = 0; i < 5 && i < og::kMaxChannels; ++i) {
    c.sx[i] = o.sx[i];
    c.sy[i] = o.sy[i];
    c.delay_ns[i] = o.delay_ns[i];
  }
  c.speed_mps = o.speed_mps;
  c.thick_mm = o.thick_mm;
  c.aim_x = o.aim_x;
  c.aim_y = o.aim_y;
  c.flip_x = o.flip_x;
  c.flip_y = o.flip_y;
  c.window_us = o.window_us;
  c.holdoff_ms = o.holdoff_ms;
  c.resid_limit_ns = o.resid_limit_ns;
  return true;
}

}  // namespace

void configDefaults(Config& c) {
  memset(&c, 0, sizeof c);
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
  // At least 4: with 3 the fit is exact, so a mistriggered channel can't be
  // detected, and the defaults (minch 4) would reject every shot until reboot.
  if (c.n_ch < 4 || c.n_ch > og::kMaxChannels) return fail("n must be 4-5");
  if (c.min_ch < 3 || c.min_ch > c.n_ch) return fail("minch must be 3..n");
  if (!(c.speed_mps > 500 && c.speed_mps < 20000)) return fail("v out of range");
  if (!(c.thick_mm >= 0 && c.thick_mm < 100)) return fail("h out of range");
  if (c.window_us < 20 || c.window_us > 5000) return fail("win must be 20-5000 us");
  if (c.holdoff_ms > 5000) return fail("hold must be <= 5000 ms");
  if (!isfinite(c.aim_x) || !isfinite(c.aim_y)) return fail("non-finite aim point");
  for (int i = 0; i < c.n_ch; ++i) {
    if (!isfinite(c.sx[i]) || !isfinite(c.sy[i]) || !isfinite(c.delay_ns[i]))
      return fail("non-finite sensor value");
    for (int j = 0; j < i; ++j) {
      if (hypotf(c.sx[i] - c.sx[j], c.sy[i] - c.sy[j]) < 5.0f) return fail("two sensors at the same position");
    }
  }
  return true;
}

ConfigSource configLoad(Config& c, char* why, size_t whyLen) {
  if (why && whyLen) why[0] = '\0';
  Config tmp;
  configDefaults(tmp);
  ConfigSource src = ConfigSource::Defaults;

  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    const uint8_t fmt = prefs.getUChar(kFormatKey, 0);
    if (fmt == kFormat) {
      readKeys(prefs, tmp);
      src = ConfigSource::Saved;
    } else if (fmt > kFormat) {
      // Settings from newer firmware (after a downgrade): read the keys this
      // build knows. Keys it doesn't know are left alone.
      readKeys(prefs, tmp);
      src = ConfigSource::Saved;
      if (why && whyLen) snprintf(why, whyLen, "stored by newer firmware (format %u)", fmt);
    } else if (readLegacy(prefs, tmp)) {
      src = ConfigSource::Migrated;
    } else if (prefs.getBytesLength(kLegacyKey) > 0) {
      src = ConfigSource::Rejected;
      if (why && whyLen) snprintf(why, whyLen, "unrecognised settings layout");
    }
    prefs.end();
  }

  if (src != ConfigSource::Defaults && src != ConfigSource::Rejected) {
    char reason[48];
    if (!configValidate(tmp, reason, sizeof reason)) {
      src = ConfigSource::Rejected;
      if (why && whyLen) snprintf(why, whyLen, "%s", reason);
    }
  }
  if (src == ConfigSource::Rejected || src == ConfigSource::Defaults) configDefaults(tmp);

  if (src == ConfigSource::Migrated) {
    // Write the converted values in the new format. The old blob is kept.
    Preferences w;
    if (!(w.begin(kNamespace, false) && writeKeys(w, tmp))) {
      if (why && whyLen) snprintf(why, whyLen, "converted, but writing the new format failed");
    }
    w.end();
  }

  c = tmp;
  return src;
}

bool configSave(const Config& c) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = writeKeys(prefs, c);
  prefs.end();
  if (ok) g_cfgSource = ConfigSource::Saved;
  return ok;
}

bool configUsingSaved() { return g_cfgSource == ConfigSource::Saved || g_cfgSource == ConfigSource::Migrated; }

void configExport(const Config& c, Print& o) {
  // Every line is a console command, so pasting the block back restores the board.
  o.println("# OpenGong settings: paste these lines back into the console, then 'save'");
  o.printf("set n %u\n", c.n_ch);
  o.printf("set minch %u\n", c.min_ch);
  for (int i = 0; i < c.n_ch; ++i) o.printf("set s %d %.2f %.2f\n", i + 1, c.sx[i], c.sy[i]);
  for (int i = 0; i < c.n_ch; ++i) o.printf("set d %d %.1f\n", i + 1, c.delay_ns[i]);
  o.printf("set v %.1f\n", c.speed_mps);
  o.printf("set h %.2f\n", c.thick_mm);
  o.printf("set aim %.2f %.2f\n", c.aim_x, c.aim_y);
  o.printf("set flip %u %u\n", c.flip_x, c.flip_y);
  o.printf("set win %u\n", c.window_us);
  o.printf("set hold %u\n", c.holdoff_ms);
  o.printf("set rlim %u\n", c.resid_limit_ns);
}
