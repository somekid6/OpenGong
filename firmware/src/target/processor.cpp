#include "processor.h"

#include <math.h>

#include "Solver.h"
#include "board.h"
#include "config.h"
#include "link.h"
#include "ui.h"

namespace {

constexpr int kMaxTaps = 80;

struct CalState {
  bool active = false;
  bool backFace = true;
  bool havePoint = false;
  float px = 0, py = 0;
  int pointNo = 0;
  int ntaps = 0;
  og::CalTap taps[kMaxTaps];
  int tapPoint[kMaxTaps];
  og::CalResult result;
  bool haveResult = false;
};

CalState s_cal;
uint16_t s_bootId = 0;
uint16_t s_shots = 0;
uint16_t s_rejects = 0;
// Reject notices to the receiver. At most one per kRejectGapMs: fragments on the
// stand or shots on nearby steel can reject over and over, and each notice costs
// air time (the EU band allows 1% duty cycle). A held-back notice goes out when
// the gap has passed, carrying the running count, so none are lost.
constexpr uint32_t kRejectGapMs = 2000;
bool s_rejectPending = false;
uint8_t s_rejectFired = 0, s_rejectNch = 0, s_rejectNeed = 0;
uint32_t s_rejectSentAt = 0;
bool s_rejectSentAny = false;
ShotView s_last;
bool s_haveLast = false;
bool s_check = false;
float s_checkX = 0, s_checkY = 0;
double s_checkSum = 0;
int s_checkN = 0;

og::Geometry geometryFrom(const Config& c, int nch, double h) {
  og::Geometry g;
  g.n = nch;
  for (int i = 0; i < nch; ++i) {
    g.x[i] = c.sx[i];
    g.y[i] = c.sy[i];
  }
  g.h = h;
  g.v = c.speed_mps / 1000.0;  // m/s -> mm/us
  return g;
}

void toShooterFrame(const Config& c, double x, double y, float& dx, float& dy) {
  dx = static_cast<float>(x - c.aim_x);
  dy = static_cast<float>(y - c.aim_y);
  if (c.flip_x) dx = -dx;
  if (c.flip_y) dy = -dy;
}

// Radius of the sensor ring around its centroid (accuracy falls off outside).
double ringRadius(const og::Geometry& g, double& cx, double& cy) {
  cx = cy = 0;
  for (int i = 0; i < g.n; ++i) {
    cx += g.x[i];
    cy += g.y[i];
  }
  cx /= g.n;
  cy /= g.n;
  double r = 0;
  for (int i = 0; i < g.n; ++i) r = fmax(r, hypot(g.x[i] - cx, g.y[i] - cy));
  return r;
}

int16_t toDmm(float mm) {
  const float v = roundf(mm * 10.0f);
  return static_cast<int16_t>(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

void printMask(Print& o, uint8_t m, int n) {
  o.print('"');
  for (int i = 0; i < n; ++i) o.print((m & (1u << i)) ? static_cast<char>('1' + i) : '-');
  o.print('"');
}

void printFlags(Print& o, uint8_t f) {
  static const char* names[] = {"missing_ch", "high_resid", "dropped_ch", "outside_ring",
                                "no_fix",     "cal",        "sim",        "check"};
  o.print('[');
  bool first = true;
  for (int b = 0; b < 8; ++b) {
    if (!(f & (1u << b))) continue;
    if (!first) o.print(',');
    o.printf("\"%s\"", names[b]);
    first = false;
  }
  o.print(']');
}

// Plate thickness to solve with. Calibration and check taps use the calibration
// face (back face by default: the jig's punch guides strike the same face as the
// sensors). Shared by the solver and `sim` so a simulated hit is generated with
// the same geometry it is solved with.
double solveThickness(const Config& c) {
  const bool calFace = (s_cal.active && s_cal.havePoint) || s_check;
  return (calFace && s_cal.backFace) ? 0.0 : c.thick_mm;
}

void flushReject() {
  if (!s_rejectPending) return;
  if (s_rejectSentAny && millis() - s_rejectSentAt < kRejectGapMs) return;
  og::RejectPacket pkt = {};
  og::initHeader(pkt.hdr, og::PKT_REJECT);
  pkt.boot_id = s_bootId;
  pkt.rejects = s_rejects;
  pkt.fired = s_rejectFired;
  pkt.n_ch = s_rejectNch;
  pkt.need = s_rejectNeed;
  og::seal(pkt);
  linkSendReject(pkt);
  s_rejectPending = false;
  s_rejectSentAny = true;
  s_rejectSentAt = millis();
}

}  // namespace

void processorBegin(uint16_t bootId) { s_bootId = bootId; }

void processorPoll() { flushReject(); }

void processEvent(const RawEvent& ev, uint8_t extraFlags) {
  const Config c = g_cfg;
  const int nch = g_activeCh;
  const double ticksPerUs = captureResolutionHz() / 1e6;
  const uint32_t winTicks = static_cast<uint32_t>(c.window_us * ticksPerUs);
  const uint8_t fired = captureWindowMask(ev, nch, winTicks);
  const int nf = og::popcount8(fired);

  // Arrival times relative to the earliest channel, in us.
  double t_us[og::kSolverMaxCh] = {0};
  int ref = -1;
  for (int i = 0; i < nch && ref < 0; ++i)
    if (fired & (1u << i)) ref = i;
  double minT = 0;
  for (int i = 0; i < nch; ++i) {
    if (!(fired & (1u << i))) continue;
    t_us[i] = static_cast<int32_t>(ev.ticks[i] - ev.ticks[ref]) / ticksPerUs;
    if (t_us[i] < minT) minT = t_us[i];
  }
  for (int i = 0; i < nch; ++i)
    if (fired & (1u << i)) t_us[i] -= minT;

  if (nf < c.min_ch) {
    ++s_rejects;
    Serial.printf("{\"type\":\"reject\",\"fired\":");
    printMask(Serial, fired, nch);
    Serial.printf(",\"need\":%d}\n", c.min_ch);
    // Tell the receiver, so the shooter isn't left wondering where the shot went.
    s_rejectPending = true;
    s_rejectFired = fired;
    s_rejectNch = static_cast<uint8_t>(nch);
    s_rejectNeed = c.min_ch;
    flushReject();
    return;
  }

  const uint16_t shotNo = ++s_shots;
  uint8_t flags = extraFlags;
  if (nf < nch) flags |= og::SHOT_MISSING_CH;

  // Solve with channel delays removed.
  // A `sim` hit is synthetic, so it must never be stored as a calibration tap.
  const bool calTap = s_cal.active && s_cal.havePoint && !(extraFlags & og::SHOT_SIM);
  const double h = solveThickness(c);
  const og::Geometry g = geometryFrom(c, nch, h);
  double tc[og::kSolverMaxCh] = {0};
  for (int i = 0; i < nch; ++i) tc[i] = t_us[i] - c.delay_ns[i] / 1000.0;
  bool dropped = false;
  const og::Fix f = og::solveRobust(g, tc, fired, c.resid_limit_ns, &dropped);

  double cx, cy;
  const double ringR = ringRadius(g, cx, cy);
  bool fix = f.ok;
  if (fix && hypot(f.x - cx, f.y - cy) > 3 * ringR) fix = false;  // diverged
  if (!fix) flags |= og::SHOT_NO_FIX;
  if (dropped) flags |= og::SHOT_DROPPED_CH;
  if (fix && f.rms_ns > c.resid_limit_ns) flags |= og::SHOT_HIGH_RESID;
  if (fix && hypot(f.x - cx, f.y - cy) > ringR + 2.0) flags |= og::SHOT_OUTSIDE_RING;

  // A check tap is not a real shot, so the receiver must keep it out of its group.
  if (s_check && !calTap) flags |= og::SHOT_CHECK;

  // Calibration: store the raw (uncorrected) times at the current known point.
  if (calTap) {
    flags |= og::SHOT_CAL;
    if (s_cal.ntaps < kMaxTaps) {
      og::CalTap& tp = s_cal.taps[s_cal.ntaps];
      tp.x = s_cal.px;
      tp.y = s_cal.py;
      tp.h = h;
      tp.mask = fired;
      for (int i = 0; i < og::kSolverMaxCh; ++i) tp.t_us[i] = t_us[i];
      s_cal.tapPoint[s_cal.ntaps] = s_cal.pointNo;
      s_cal.ntaps++;
    } else {
      Serial.println("cal: tap buffer full, run 'cal fit'");
    }
  }

  float dx = 0, dy = 0;
  if (fix) toShooterFrame(c, f.x, f.y, dx, dy);

  // ---- Serial JSON event ----
  Serial.printf("{\"type\":\"shot\",\"n\":%u", shotNo);
  if (fix) {
    Serial.printf(",\"x\":%.1f,\"y\":%.1f,\"px\":%.1f,\"py\":%.1f,\"rms_ns\":%.0f", dx, dy, f.x, f.y, f.rms_ns);
  }
  Serial.print(",\"fired\":");
  printMask(Serial, fired, nch);
  Serial.print(",\"used\":");
  printMask(Serial, fix ? f.used : 0, nch);
  Serial.print(",\"flags\":");
  printFlags(Serial, flags);
  Serial.print(",\"t_ns\":[");
  for (int i = 0; i < nch; ++i) {
    if (i) Serial.print(',');
    if (fired & (1u << i)) Serial.printf("%.1f", t_us[i] * 1000.0);
    else Serial.print("null");
  }
  Serial.print(']');
  if (calTap) {
    Serial.printf(",\"cal_point\":%d,\"cal_taps\":%d", s_cal.pointNo, s_cal.ntaps);
    if (fix) Serial.printf(",\"err_mm\":%.2f", hypot(f.x - s_cal.px, f.y - s_cal.py));
  } else if (s_check && fix) {
    const double e = hypot(f.x - s_checkX, f.y - s_checkY);
    s_checkSum += e;
    s_checkN++;
    Serial.printf(",\"err_mm\":%.2f,\"ex\":%.2f,\"ey\":%.2f,\"err_mean_mm\":%.2f,\"check_taps\":%d", e,
                  f.x - s_checkX, f.y - s_checkY, s_checkSum / s_checkN, s_checkN);
  }
  Serial.println('}');

  // ---- LoRa ----
  og::ShotPacket pkt = {};
  og::initHeader(pkt.hdr, og::PKT_SHOT);
  pkt.boot_id = s_bootId;
  pkt.shot = shotNo;
  pkt.x_dmm = toDmm(dx);
  pkt.y_dmm = toDmm(dy);
  pkt.rms_ns = static_cast<uint16_t>(fmin(f.ok ? f.rms_ns : 65535.0, 65535.0));
  pkt.used = fix ? f.used : 0;
  pkt.fired = fired;
  pkt.flags = flags;
  pkt.batt_mv = uiBatteryMv();
  og::seal(pkt);
  linkSendShot(pkt);

  // ---- Display ----
  s_last.shot = shotNo;
  s_last.fix = fix;
  s_last.dx = dx;
  s_last.dy = dy;
  s_last.rms_ns = static_cast<float>(f.rms_ns);
  s_last.used = pkt.used;
  s_last.fired = fired;
  s_last.flags = flags;
  s_last.at_ms = millis();
  s_haveLast = true;
  uiShotEvent();
}

bool processorLastShot(ShotView& out) {
  out = s_last;
  return s_haveLast;
}
uint16_t processorShotCount() { return s_shots; }
uint16_t processorRejectCount() { return s_rejects; }
bool processorCalActive() { return s_cal.active; }

void processorSimulate(float x, float y, float noiseNs, Print& out) {
  const Config& c = g_cfg;
  const int nch = g_activeCh;
  const og::Geometry g = geometryFrom(c, nch, solveThickness(c));
  double t[og::kSolverMaxCh];
  og::predictArrivals(g, x, y, 10.0, t);

  const double ticksPerUs = captureResolutionHz() / 1e6;
  RawEvent ev = {};
  ev.mask = static_cast<uint8_t>((1u << nch) - 1);
  for (int i = 0; i < nch; ++i) {
    // Box-Muller Gaussian noise
    const double u1 = (esp_random() + 1.0) / 4294967297.0, u2 = esp_random() / 4294967296.0;
    const double n = sqrt(-2 * log(u1)) * cos(2 * M_PI * u2) * noiseNs / 1000.0;
    const double tt = t[i] + c.delay_ns[i] / 1000.0 + n;
    ev.ticks[i] = 1000000u + static_cast<uint32_t>(lround(tt * ticksPerUs));
  }
  out.printf("sim: impact at (%.1f, %.1f) mm, noise %.0f ns\n", x, y, noiseNs);
  processEvent(ev, og::SHOT_SIM);
}

void processorCheck(bool on, float x, float y, Print& out) {
  s_check = on;
  s_checkX = x;
  s_checkY = y;
  s_checkSum = 0;
  s_checkN = 0;
  if (on)
    out.printf("check: each shot reports its error from (%.1f, %.1f), %s-face taps\n", x, y,
               s_cal.backFace ? "back" : "front");
  else out.println("check: off");
}

// ---------------------------------------------------------------------------
// Calibration (System-Design section 8)
// ---------------------------------------------------------------------------
void calStart(bool backFace, Print& out) {
  s_cal.active = true;
  s_cal.backFace = backFace;
  s_cal.havePoint = false;
  s_cal.pointNo = 0;
  s_cal.ntaps = 0;
  s_cal.haveResult = false;
  out.printf("cal: started, taps on the %s face (thickness %s). Next: 'cal at P0', then tap 3-5 times.\n",
             backFace ? "back" : "front", backFace ? "ignored" : "included");
}

void calAt(float x, float y, Print& out) {
  if (!s_cal.active) {
    out.println("cal: not active, use 'cal start' first");
    return;
  }
  s_cal.havePoint = true;
  s_cal.px = x;
  s_cal.py = y;
  s_cal.pointNo++;
  out.printf("cal: point %d at (%.2f, %.2f) mm. Tap now.\n", s_cal.pointNo, x, y);
}

void calUndo(Print& out) {
  if (s_cal.ntaps > 0) {
    s_cal.ntaps--;
    out.printf("cal: removed last tap, %d left\n", s_cal.ntaps);
  } else {
    out.println("cal: no taps");
  }
}

void calList(Print& out) {
  out.printf("cal: %s, %s face, %d taps\n", s_cal.active ? "active" : "stopped",
             s_cal.backFace ? "back" : "front", s_cal.ntaps);
  int i = 0;
  while (i < s_cal.ntaps) {
    const int p = s_cal.tapPoint[i];
    int n = 0;
    while (i + n < s_cal.ntaps && s_cal.tapPoint[i + n] == p) ++n;
    out.printf("  point %d (%.2f, %.2f): %d taps\n", p, s_cal.taps[i].x, s_cal.taps[i].y, n);
    i += n;
  }
}

void calFit(bool fitSpeed, Print& out) {
  const int nch = g_activeCh;
  const og::Geometry g = geometryFrom(g_cfg, nch, g_cfg.thick_mm);
  const og::CalResult r = og::fitCalibration(g, s_cal.taps, s_cal.ntaps, fitSpeed);
  if (!r.ok) {
    out.printf("cal: fit failed (%d usable taps). Need taps at several spread-out points.\n", r.tapsUsed);
    return;
  }
  s_cal.result = r;
  s_cal.haveResult = true;

  out.printf("cal: fit from %d taps, rms %.0f ns\n", r.tapsUsed, r.rms_ns);
  out.printf("  v = %.0f m/s%s\n", r.v * 1000.0, fitSpeed ? "" : " (fixed)");
  if (r.v < 2.5 || r.v > 7.0) out.println("  WARNING: wave speed is implausible for steel; check sensor positions");
  for (int i = 0; i < nch; ++i) out.printf("  ch%d delay %+.0f ns\n", i + 1, r.delay_us[i] * 1000.0);

  // Re-solve every tap with the new parameters and report the position error per point.
  og::Geometry gc = g;
  gc.v = r.v;
  out.println("  per-point error with new values:");
  int i = 0;
  double worst = 0;
  while (i < s_cal.ntaps) {
    const int p = s_cal.tapPoint[i];
    double sum = 0;
    int n = 0;
    for (; i < s_cal.ntaps && s_cal.tapPoint[i] == p; ++i) {
      const og::CalTap& tp = s_cal.taps[i];
      gc.h = tp.h;
      double tc[og::kSolverMaxCh];
      for (int k = 0; k < nch; ++k) tc[k] = tp.t_us[k] - r.delay_us[k];
      const og::Fix f = og::solveFix(gc, tc, tp.mask);
      if (!f.ok) continue;
      const double e = hypot(f.x - tp.x, f.y - tp.y);
      sum += e;
      worst = fmax(worst, e);
      ++n;
    }
    if (n) out.printf("    point %d: mean %.2f mm (%d taps)\n", p, sum / n, n);
  }
  out.printf("  worst tap %.2f mm. 'cal apply' then 'save' to keep.\n", worst);
}

void calApply(Print& out) {
  if (!s_cal.haveResult) {
    out.println("cal: nothing to apply, run 'cal fit' first");
    return;
  }
  g_cfg.speed_mps = static_cast<float>(s_cal.result.v * 1000.0);
  for (int i = 0; i < g_activeCh; ++i) g_cfg.delay_ns[i] = static_cast<float>(s_cal.result.delay_us[i] * 1000.0);
  out.println("cal: applied (not yet saved). Check held-out points with 'check <x> <y>', then 'save'.");
}

void calStop(Print& out) {
  s_cal.active = false;
  s_cal.havePoint = false;
  out.printf("cal: stopped (%d taps kept until next 'cal start')\n", s_cal.ntaps);
}
