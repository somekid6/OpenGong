#include "console.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"
#include "config.h"
#include "link.h"
#include "processor.h"

namespace {

bool s_reboot = false;
char s_line[160];
size_t s_len = 0;

const char kHelp[] =
    "Commands (mm, front view, origin = ring centre; channels 1-5):\n"
    "<pt> = '<x> <y>' or a punch guide P0-P5, H1, H2, H4\n"
    "  show                     print configuration\n"
    "  export                   print settings as commands (a backup)\n"
    "  set s <ch> <x> <y>       sensor position\n"
    "  set d <ch> <ns>          channel delay\n"
    "  set v <m/s>              wave speed\n"
    "  set h <mm>               plate thickness\n"
    "  set aim <x> <y>          point reported as 0,0\n"
    "  set flip <fx> <fy>       mirror reported x/y (0|1)\n"
    "  set n <4-5>              sensor count (save + reboot)\n"
    "  set minch <n>            channels needed per shot\n"
    "  set win <us> | hold <ms> | rlim <ns>\n"
    "  save | load | defaults | reboot\n"
    "  stats                    trigger and link counters\n"
    "  sim <pt> [noise_ns]      inject a synthetic hit\n"
    "  check <pt> | check off\n"
    "  cal start [back|front] | cal at <pt> | cal undo\n"
    "  cal list | cal fit [fixv] | cal apply | cal stop\n";

bool parseCh(const char* s, int& ch, Print& out) {
  ch = s ? atoi(s) : 0;
  if (ch < 1 || ch > og::kMaxChannels) {
    out.println("error: channel must be 1-5");
    return false;
  }
  ch -= 1;
  return true;
}

// Punch guides on the 120 mm jig ring (System-Design 8, step 2), front view.
struct NamedPoint {
  const char* name;
  float x, y;
};
const NamedPoint kPoints[] = {
    {"P0", 0.0f, 0.0f},      {"P1", 0.0f, 60.0f},      {"P2", 57.06f, 18.54f},
    {"P3", 35.27f, -48.54f}, {"P4", -35.27f, -48.54f}, {"P5", -57.06f, 18.54f},
    {"H1", 87.50f, 28.43f},  {"H2", 54.08f, -74.43f},  {"H4", -87.50f, 28.43f},
};

bool parseF(const char* s, float& v, Print& out);

// Reads either a named punch point ("P2", "h1") or two numbers "<x> <y>".
bool parsePoint(char*& save, float& x, float& y, Print& out) {
  const char* a = strtok_r(nullptr, " \t", &save);
  if (a) {
    for (const NamedPoint& p : kPoints) {
      if (!strcasecmp(a, p.name)) {
        x = p.x;
        y = p.y;
        return true;
      }
    }
  }
  const char* b = strtok_r(nullptr, " \t", &save);
  return parseF(a, x, out) && parseF(b, y, out);
}

bool parseF(const char* s, float& v, Print& out) {
  if (!s) {
    out.println("error: missing number");
    return false;
  }
  char* end;
  v = strtof(s, &end);
  if (end == s) {
    out.printf("error: '%s' is not a number\n", s);
    return false;
  }
  return true;
}

// Parses a whole number and range-checks it before it is narrowed to the
// config field's type, so "set hold 70000" can't wrap to 4464 and pass.
bool parseU(const char* s, uint32_t lo, uint32_t hi, uint32_t& v, const char* name, Print& out) {
  float f;
  if (!parseF(s, f, out)) return false;
  if (!(f >= lo && f <= hi) || f != floorf(f)) {
    out.printf("error: %s must be a whole number %lu-%lu\n", name, (unsigned long)lo, (unsigned long)hi);
    return false;
  }
  v = static_cast<uint32_t>(f);
  return true;
}

void show(Print& o) {
  const Config& c = g_cfg;
  o.printf("channels %u (active %d), minch %u\n", c.n_ch, g_activeCh, c.min_ch);
  for (int i = 0; i < c.n_ch; ++i)
    o.printf("  s%d  x %7.2f  y %7.2f  delay %+6.0f ns\n", i + 1, c.sx[i], c.sy[i], c.delay_ns[i]);
  o.printf("v %.0f m/s  h %.2f mm\n", c.speed_mps, c.thick_mm);
  o.printf("aim %.2f %.2f  flip %u %u\n", c.aim_x, c.aim_y, c.flip_x, c.flip_y);
  o.printf("win %u us  hold %u ms  rlim %u ns\n", c.window_us, c.holdoff_ms, c.resid_limit_ns);
  o.printf("timer %lu Hz\n", (unsigned long)captureResolutionHz());
  if (!configUsingSaved()) o.println("WARNING: built-in defaults, nothing saved. Calibrate, then 'save'.");
}

void stats(Print& o) {
  uint32_t edges[og::kMaxChannels];
  captureEdgeCounts(edges);
  o.printf("shots %u  rejected %u  armed %d\n", processorShotCount(), processorRejectCount(), captureArmed());
  o.print("edges while armed:");
  for (int i = 0; i < g_activeCh; ++i) o.printf(" ch%d=%lu", i + 1, (unsigned long)edges[i]);
  o.println();
  const LinkStats ls = linkStats();
  o.printf("lora %s  sent %lu  acked %lu  failed %lu  tx %lu  rssi %.0f  snr %.1f\n", ls.up ? "up" : "DOWN",
           (unsigned long)ls.shotsSent, (unsigned long)ls.shotsAcked, (unsigned long)ls.shotsFailed,
           (unsigned long)ls.transmissions, ls.lastRssi, ls.lastSnr);
}

void cmdSet(char* save, Print& out) {
  const char* what = strtok_r(nullptr, " \t", &save);
  const char* a = strtok_r(nullptr, " \t", &save);
  const char* b = strtok_r(nullptr, " \t", &save);
  const char* c = strtok_r(nullptr, " \t", &save);
  if (!what) {
    out.println("error: set what? (see help)");
    return;
  }
  Config next = g_cfg;
  float f1, f2;
  uint32_t u;
  int ch;

  if (!strcmp(what, "s")) {
    if (!parseCh(a, ch, out) || !parseF(b, f1, out) || !parseF(c, f2, out)) return;
    next.sx[ch] = f1;
    next.sy[ch] = f2;
  } else if (!strcmp(what, "d")) {
    if (!parseCh(a, ch, out) || !parseF(b, f1, out)) return;
    next.delay_ns[ch] = f1;
  } else if (!strcmp(what, "v")) {
    if (!parseF(a, f1, out)) return;
    next.speed_mps = f1;
  } else if (!strcmp(what, "h")) {
    if (!parseF(a, f1, out)) return;
    next.thick_mm = f1;
  } else if (!strcmp(what, "aim")) {
    if (!parseF(a, f1, out) || !parseF(b, f2, out)) return;
    next.aim_x = f1;
    next.aim_y = f2;
  } else if (!strcmp(what, "flip")) {
    if (!parseF(a, f1, out) || !parseF(b, f2, out)) return;
    next.flip_x = f1 != 0;
    next.flip_y = f2 != 0;
  } else if (!strcmp(what, "n")) {
    if (!parseU(a, 4, og::kMaxChannels, u, "n", out)) return;
    next.n_ch = static_cast<uint8_t>(u);
    if (next.min_ch > next.n_ch) next.min_ch = next.n_ch;
  } else if (!strcmp(what, "minch")) {
    if (!parseU(a, 3, og::kMaxChannels, u, "minch", out)) return;
    next.min_ch = static_cast<uint8_t>(u);
  } else if (!strcmp(what, "win")) {
    if (!parseU(a, 20, 5000, u, "win", out)) return;
    next.window_us = static_cast<uint16_t>(u);
  } else if (!strcmp(what, "hold")) {
    if (!parseU(a, 0, 5000, u, "hold", out)) return;
    next.holdoff_ms = static_cast<uint16_t>(u);
  } else if (!strcmp(what, "rlim")) {
    if (!parseU(a, 50, 10000, u, "rlim", out)) return;
    next.resid_limit_ns = static_cast<uint16_t>(u);
  } else {
    out.printf("error: unknown setting '%s'\n", what);
    return;
  }

  char why[48];
  if (!configValidate(next, why, sizeof why)) {
    out.printf("error: %s\n", why);
    return;
  }
  if (next.min_ch > g_activeCh) {
    out.printf("error: minch exceeds the %d active channels\n", g_activeCh);
    return;
  }
  g_cfg = next;
  out.println("ok (not saved)");
  if (g_cfg.n_ch != g_activeCh) out.println("channel count changes after 'save' and 'reboot'");
}

void cmdCal(char* save, Print& out) {
  const char* sub = strtok_r(nullptr, " \t", &save);
  float x, y;
  if (sub && !strcmp(sub, "at")) {
    if (parsePoint(save, x, y, out)) calAt(x, y, out);
    return;
  }
  const char* a = strtok_r(nullptr, " \t", &save);
  if (!sub) {
    calList(out);
  } else if (!strcmp(sub, "start")) {
    // Jig punch guides strike the back face (System-Design 8, step 3).
    calStart(!(a && !strcmp(a, "front")), out);
  } else if (!strcmp(sub, "undo")) {
    calUndo(out);
  } else if (!strcmp(sub, "list")) {
    calList(out);
  } else if (!strcmp(sub, "fit")) {
    calFit(!(a && !strcmp(a, "fixv")), out);
  } else if (!strcmp(sub, "apply")) {
    calApply(out);
  } else if (!strcmp(sub, "stop")) {
    calStop(out);
  } else {
    out.printf("error: unknown cal command '%s'\n", sub);
  }
}

}  // namespace

void consoleExec(char* line, Print& out) {
  char* save = nullptr;
  const char* cmd = strtok_r(line, " \t\r\n", &save);
  if (!cmd || cmd[0] == '#') return;  // blank line, or a comment from pasted 'export' output

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    out.print(kHelp);
  } else if (!strcmp(cmd, "show")) {
    show(out);
  } else if (!strcmp(cmd, "set")) {
    cmdSet(save, out);
  } else if (!strcmp(cmd, "save")) {
    out.println(configSave(g_cfg) ? "saved" : "error: save failed");
  } else if (!strcmp(cmd, "export")) {
    configExport(g_cfg, out);
  } else if (!strcmp(cmd, "load")) {
    // Only replaces the live settings when flash holds usable ones, so an
    // unsaved calibration isn't thrown away for the defaults.
    Config c;
    char why[96];
    const ConfigSource src = configLoad(c, why, sizeof why);
    if (src == ConfigSource::Defaults) {
      out.println("error: nothing saved, live settings unchanged");
    } else if (src == ConfigSource::Rejected) {
      out.printf("error: saved settings unusable (%s), live settings unchanged\n", why);
    } else {
      g_cfg = c;
      g_cfgSource = src;
      out.println("loaded");
      if (why[0]) out.printf("note: %s\n", why);
    }
  } else if (!strcmp(cmd, "defaults")) {
    configDefaults(g_cfg);
    g_cfgSource = ConfigSource::Defaults;
    out.println("defaults loaded (not saved)");
  } else if (!strcmp(cmd, "stats")) {
    stats(out);
  } else if (!strcmp(cmd, "sim")) {
    float x, y, n = 0;
    if (!parsePoint(save, x, y, out)) return;
    const char* c = strtok_r(nullptr, " \t", &save);
    if (c && !parseF(c, n, out)) return;
    processorSimulate(x, y, n, out);
  } else if (!strcmp(cmd, "check")) {
    float x, y;
    while (save && (*save == ' ' || *save == '\t')) ++save;
    if (save && !strncmp(save, "off", 3)) processorCheck(false, 0, 0, out);
    else if (parsePoint(save, x, y, out)) processorCheck(true, x, y, out);
  } else if (!strcmp(cmd, "cal")) {
    cmdCal(save, out);
  } else if (!strcmp(cmd, "reboot")) {
    out.println("rebooting");
    s_reboot = true;
  } else {
    out.printf("error: unknown command '%s' (try 'help')\n", cmd);
  }
}

void consolePollSerial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (s_len == 0) continue;
      s_line[s_len] = '\0';
      Serial.printf("> %s\n", s_line);
      consoleExec(s_line, Serial);
      s_len = 0;
    } else if (s_len < sizeof(s_line) - 1) {
      s_line[s_len++] = static_cast<char>(c);
    }
  }
}

bool consoleRebootRequested() { return s_reboot; }
