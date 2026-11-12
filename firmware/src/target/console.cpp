#include "console.h"

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
    "Commands (coordinates in mm, jig frame; channels 1-6):\n"
    "  show                     print configuration\n"
    "  set s <ch> <x> <y>       sensor position\n"
    "  set d <ch> <ns>          channel delay\n"
    "  set v <m/s>              wave speed\n"
    "  set h <mm>               plate thickness\n"
    "  set aim <x> <y>          point reported as 0,0\n"
    "  set flip <fx> <fy>       mirror reported x/y (0|1)\n"
    "  set n <3-6>              sensor count (save + reboot)\n"
    "  set minch <n>            channels needed per shot\n"
    "  set win <us> | hold <ms> | rlim <ns>\n"
    "  save | load | defaults | reboot\n"
    "  stats                    trigger and link counters\n"
    "  sim <x> <y> [noise_ns]   inject a synthetic hit\n"
    "  check <x> <y> | check off\n"
    "  cal start [front|back] | cal at <x> <y> | cal undo\n"
    "  cal list | cal fit [fixv] | cal apply | cal stop\n";

bool parseCh(const char* s, int& ch, Print& out) {
  ch = s ? atoi(s) : 0;
  if (ch < 1 || ch > og::kMaxChannels) {
    out.println("error: channel must be 1-6");
    return false;
  }
  ch -= 1;
  return true;
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

void show(Print& o) {
  const Config& c = g_cfg;
  o.printf("channels %u (active %d), minch %u\n", c.n_ch, g_activeCh, c.min_ch);
  for (int i = 0; i < c.n_ch; ++i)
    o.printf("  s%d  x %7.2f  y %7.2f  delay %+6.0f ns\n", i + 1, c.sx[i], c.sy[i], c.delay_ns[i]);
  o.printf("v %.0f m/s  h %.2f mm\n", c.speed_mps, c.thick_mm);
  o.printf("aim %.2f %.2f  flip %u %u\n", c.aim_x, c.aim_y, c.flip_x, c.flip_y);
  o.printf("win %u us  hold %u ms  rlim %u ns\n", c.window_us, c.holdoff_ms, c.resid_limit_ns);
  o.printf("timer %lu Hz\n", (unsigned long)captureResolutionHz());
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
    if (!parseF(a, f1, out)) return;
    next.n_ch = static_cast<uint8_t>(f1);
    if (next.min_ch > next.n_ch) next.min_ch = next.n_ch;
  } else if (!strcmp(what, "minch")) {
    if (!parseF(a, f1, out)) return;
    next.min_ch = static_cast<uint8_t>(f1);
  } else if (!strcmp(what, "win")) {
    if (!parseF(a, f1, out)) return;
    next.window_us = static_cast<uint16_t>(f1);
  } else if (!strcmp(what, "hold")) {
    if (!parseF(a, f1, out)) return;
    next.holdoff_ms = static_cast<uint16_t>(f1);
  } else if (!strcmp(what, "rlim")) {
    if (!parseF(a, f1, out)) return;
    next.resid_limit_ns = static_cast<uint16_t>(f1);
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
  const char* a = strtok_r(nullptr, " \t", &save);
  const char* b = strtok_r(nullptr, " \t", &save);
  float x, y;
  if (!sub) {
    calList(out);
  } else if (!strcmp(sub, "start")) {
    calStart(a && !strcmp(a, "back"), out);
  } else if (!strcmp(sub, "at")) {
    if (parseF(a, x, out) && parseF(b, y, out)) calAt(x, y, out);
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
  if (!cmd) return;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    out.print(kHelp);
  } else if (!strcmp(cmd, "show")) {
    show(out);
  } else if (!strcmp(cmd, "set")) {
    cmdSet(save, out);
  } else if (!strcmp(cmd, "save")) {
    out.println(configSave(g_cfg) ? "saved" : "error: save failed");
  } else if (!strcmp(cmd, "load")) {
    out.println(configLoad(g_cfg) ? "loaded" : "nothing stored, defaults loaded");
  } else if (!strcmp(cmd, "defaults")) {
    configDefaults(g_cfg);
    out.println("defaults loaded (not saved)");
  } else if (!strcmp(cmd, "stats")) {
    stats(out);
  } else if (!strcmp(cmd, "sim")) {
    float x, y, n = 0;
    const char* a = strtok_r(nullptr, " \t", &save);
    const char* b = strtok_r(nullptr, " \t", &save);
    const char* c = strtok_r(nullptr, " \t", &save);
    if (!parseF(a, x, out) || !parseF(b, y, out)) return;
    if (c && !parseF(c, n, out)) return;
    processorSimulate(x, y, n, out);
  } else if (!strcmp(cmd, "check")) {
    const char* a = strtok_r(nullptr, " \t", &save);
    const char* b = strtok_r(nullptr, " \t", &save);
    float x, y;
    if (a && !strcmp(a, "off")) processorCheck(false, 0, 0, out);
    else if (parseF(a, x, out) && parseF(b, y, out)) processorCheck(true, x, y, out);
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
