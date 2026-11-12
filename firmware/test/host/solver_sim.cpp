// Host-side check of the OpenGong solver and calibration fit.
//
//   g++ -O2 -std=c++17 -I lib/OpenGongSolver test/host/solver_sim.cpp lib/OpenGongSolver/Solver.cpp -o solver_sim
//   ./solver_sim
//
// 1. Reproduces the accuracy table in docs/System-Design.md section 2
//    (5 sensors, 140 mm pentagon, 9.5 mm plate, impacts within 130 mm).
// 2. Simulates a punch-tap calibration with unknown delays and wave speed
//    and checks that the fit recovers them.
// 3. Checks that one channel triggering on a late wave is caught.
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <random>
#include <vector>

#include "Solver.h"

using namespace og;

static Geometry pentagon() {
  Geometry g;
  g.n = 5;
  g.h = 9.5;
  g.v = 5.9;
  for (int i = 0; i < 5; ++i) {
    const double a = M_PI / 2 + i * 2 * M_PI / 5;
    g.x[i] = 152.4 + 140 * cos(a);
    g.y[i] = 152.4 + 140 * sin(a);
  }
  return g;
}

static int failures = 0;
static void expect(bool ok, const char* what) {
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

int main() {
  std::mt19937 rng(12345);
  std::uniform_real_distribution<double> uni(0, 1);
  const Geometry g = pentagon();

  // ---- 1. Accuracy vs timing scatter ------------------------------------
  printf("Accuracy (2000 random impacts within 130 mm of centre):\n");
  printf("  sigma_ns   mean_mm   p95_mm   fail\n");
  const double sigmas[] = {0, 100, 200, 500, 1000};
  double mean200 = 0, p95_200 = 0;
  for (double sig : sigmas) {
    std::normal_distribution<double> noise(0, sig / 1000.0);
    std::vector<double> err;
    int fail = 0;
    for (int k = 0; k < 2000; ++k) {
      const double r = 130 * sqrt(uni(rng)), a = 2 * M_PI * uni(rng);
      const double x = 152.4 + r * cos(a), y = 152.4 + r * sin(a);
      double t[kSolverMaxCh];
      predictArrivals(g, x, y, 37.0, t);
      for (int i = 0; i < g.n; ++i) t[i] += noise(rng);
      Fix f = solveFix(g, t, 0x1F);
      if (!f.ok) {
        ++fail;
        continue;
      }
      err.push_back(hypot(f.x - x, f.y - y));
    }
    std::sort(err.begin(), err.end());
    double mean = 0;
    for (double e : err) mean += e;
    mean /= err.size();
    const double p95 = err[static_cast<size_t>(err.size() * 0.95)];
    printf("  %8.0f  %8.2f  %7.2f  %5d\n", sig, mean, p95, fail);
    if (sig == 0) expect(p95 < 0.01, "noise-free solve is exact");
    if (sig == 200) {
      mean200 = mean;
      p95_200 = p95;
    }
  }
  expect(mean200 < 2.0 && p95_200 < 4.0, "200 ns scatter meets the doc's ~1.3 mm mean / 2.4 mm p95");

  // ---- 2. Calibration fit -------------------------------------------------
  printf("\nCalibration (true v = 5.65 mm/us, delays 0/+120/-80/+300/+40 ns, 150 ns noise):\n");
  Geometry truth = g;
  truth.v = 5.65;
  const double trueDelay[5] = {0, 0.120, -0.080, 0.300, 0.040};
  std::normal_distribution<double> noise(0, 0.150);
  const double pts[8][2] = {{152.4, 152.4}, {200, 160}, {110, 120}, {150, 230},
                            {90, 190},     {210, 90},  {160, 80},  {230, 210}};
  std::vector<CalTap> taps;
  for (const auto& pt : pts) {
    for (int rep = 0; rep < 4; ++rep) {
      CalTap tp;
      tp.x = pt[0];
      tp.y = pt[1];
      tp.h = truth.h;  // front-face taps
      tp.mask = 0x1F;
      predictArrivals(truth, tp.x, tp.y, 1000.0 + rep, tp.t_us);
      for (int i = 0; i < 5; ++i) tp.t_us[i] += trueDelay[i] + noise(rng);
      taps.push_back(tp);
    }
  }
  Geometry guess = g;  // starts at the default 5.9 mm/us
  CalResult cr = fitCalibration(guess, taps.data(), static_cast<int>(taps.size()), true);
  printf("  fitted v = %.4f mm/us, rms %.0f ns, taps %d\n", cr.v, cr.rms_ns, cr.tapsUsed);
  double worstDelay = 0;
  for (int i = 0; i < 5; ++i) {
    printf("  ch%d delay %+7.1f ns (true %+6.1f)\n", i + 1, cr.delay_us[i] * 1000, trueDelay[i] * 1000);
    worstDelay = fmax(worstDelay, fabs(cr.delay_us[i] - trueDelay[i]));
  }
  expect(cr.ok, "calibration fit succeeds");
  expect(fabs(cr.v - truth.v) < 0.05, "wave speed within 0.05 mm/us");
  expect(worstDelay < 0.08, "delays within 80 ns");

  // Apply the calibration and check held-out impacts.
  Geometry cal = g;
  cal.v = cr.v;
  double worst = 0;
  std::normal_distribution<double> n200(0, 0.200);
  for (int k = 0; k < 500; ++k) {
    const double r = 120 * sqrt(uni(rng)), a = 2 * M_PI * uni(rng);
    const double x = 152.4 + r * cos(a), y = 152.4 + r * sin(a);
    double t[kSolverMaxCh];
    predictArrivals(truth, x, y, 5.0, t);
    for (int i = 0; i < 5; ++i) t[i] += trueDelay[i] + n200(rng) - cr.delay_us[i];
    Fix f = solveFix(cal, t, 0x1F);
    worst = fmax(worst, hypot(f.x - x, f.y - y));
  }
  printf("  worst error after calibration, 500 shots @200 ns: %.2f mm\n", worst);
  expect(worst < 6.0, "calibrated shots stay inside ~5 mm");

  // Uncalibrated (wrong speed and delays ignored) for comparison.
  double worstUncal = 0;
  for (int k = 0; k < 200; ++k) {
    const double r = 120 * sqrt(uni(rng)), a = 2 * M_PI * uni(rng);
    const double x = 152.4 + r * cos(a), y = 152.4 + r * sin(a);
    double t[kSolverMaxCh];
    predictArrivals(truth, x, y, 5.0, t);
    for (int i = 0; i < 5; ++i) t[i] += trueDelay[i];
    Fix f = solveFix(g, t, 0x1F);
    worstUncal = fmax(worstUncal, hypot(f.x - x, f.y - y));
  }
  printf("  worst error without calibration (no noise): %.2f mm\n", worstUncal);

  // ---- 3. Bad trigger handling ------------------------------------------
  printf("\nBad trigger (one channel 8 us late, as if it caught the slow wave):\n");
  {
    Geometry g6 = g;
    g6.n = 6;
    g6.x[5] = 152.4;  // 6th sensor near the centre-bottom of the ring
    g6.y[5] = 40;
    double t[kSolverMaxCh];
    predictArrivals(g6, 130, 170, 3.0, t);
    t[2] += 8.0;
    bool dropped5 = false, dropped6 = false;
    Fix f5 = solveRobust(g, t, 0x1F, 500, &dropped5);
    Fix f6 = solveRobust(g6, t, 0x3F, 500, &dropped6);
    printf("  5 ch: err %.1f mm, rms %.0f ns, dropped=%d\n", hypot(f5.x - 130, f5.y - 170), f5.rms_ns, dropped5);
    printf("  6 ch: err %.1f mm, rms %.0f ns, dropped=%d used=0x%02X\n", hypot(f6.x - 130, f6.y - 170), f6.rms_ns,
           dropped6, f6.used);
    expect(dropped5 || f5.rms_ns > 500, "5 ch: bad shot is either repaired or flagged by its residual");
    expect(dropped6 && f6.used == (0x3F & ~0x04) && hypot(f6.x - 130, f6.y - 170) < 0.1,
           "6 ch: bad channel 3 dropped and position recovered");
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
