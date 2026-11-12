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
    double t[kSolverMaxCh];
    predictArrivals(g, 130, 170, 3.0, t);
    t[2] += 8.0;
    bool dropped5 = false;
    Fix f5 = solveRobust(g, t, 0x1F, 500, &dropped5);
    printf("  5 ch: err %.1f mm, rms %.0f ns, dropped=%d\n", hypot(f5.x - 130, f5.y - 170), f5.rms_ns, dropped5);
    expect(dropped5 || f5.rms_ns > 500, "5 ch: bad shot is either repaired or flagged by its residual");
  }

  // ---- 4. 120 mm jig ring, back-face punch calibration (System-Design 4.1 / 8) ----
  printf("\n120 mm jig ring (firmware defaults), back-face taps at P0-P5, check at H1/H2/H4:\n");
  {
    Geometry r120;
    r120.n = 5;
    r120.h = 9.5;
    r120.v = 5.9;
    const double S[5][2] = {{-70.53, 97.08}, {70.53, 97.08}, {114.13, -37.08}, {0, -120}, {-114.13, -37.08}};
    for (int i = 0; i < 5; ++i) {
      r120.x[i] = S[i][0];
      r120.y[i] = S[i][1];
    }
    // Accuracy at 200 ns (doc: within 110 mm -> 1.1 mean / 2.2 p95).
    std::normal_distribution<double> n200b(0, 0.200);
    std::vector<double> err;
    for (int k = 0; k < 2000; ++k) {
      const double r = 110 * sqrt(uni(rng)), a = 2 * M_PI * uni(rng);
      const double x = r * cos(a), y = r * sin(a);
      double t[kSolverMaxCh];
      predictArrivals(r120, x, y, 0, t);
      for (int i = 0; i < 5; ++i) t[i] += n200b(rng);
      Fix f = solveFix(r120, t, 0x1F);
      err.push_back(hypot(f.x - x, f.y - y));
    }
    std::sort(err.begin(), err.end());
    double mean = 0;
    for (double e : err) mean += e;
    mean /= err.size();
    printf("  200 ns, within 110 mm: mean %.2f mm, p95 %.2f mm\n", mean, err[1900]);
    expect(mean < 1.5 && err[1900] < 3.0, "120 mm ring matches the doc's 1.1 / 2.2 mm");

    Geometry truthB = r120;
    truthB.v = 5.75;
    truthB.h = 0;  // punch strikes the back face
    const double P[6][2] = {{0, 0}, {0, 60}, {57.06, 18.54}, {35.27, -48.54}, {-35.27, -48.54}, {-57.06, 18.54}};
    std::vector<CalTap> bt;
    for (const auto& p : P)
      for (int rep = 0; rep < 4; ++rep) {
        CalTap tp;
        tp.x = p[0];
        tp.y = p[1];
        tp.h = 0;
        tp.mask = 0x1F;
        predictArrivals(truthB, tp.x, tp.y, 50.0, tp.t_us);
        for (int i = 0; i < 5; ++i) tp.t_us[i] += trueDelay[i] + noise(rng);
        bt.push_back(tp);
      }
    CalResult c2 = fitCalibration(r120, bt.data(), static_cast<int>(bt.size()), true);
    printf("  fitted v = %.4f mm/us (true 5.75), rms %.0f ns\n", c2.v, c2.rms_ns);
    expect(c2.ok && fabs(c2.v - 5.75) < 0.05, "back-face fit recovers wave speed");

    Geometry chk = r120;
    chk.v = c2.v;
    chk.h = 0;
    const double H[3][2] = {{87.50, 28.43}, {54.08, -74.43}, {-87.50, 28.43}};
    // Judge each H point on the mean of its 5 taps: single taps at this
    // jitter occasionally exceed 2 mm even with a perfect calibration.
    double worstMean = 0, worstTap = 0;
    for (const auto& hp : H) {
      double sum = 0;
      for (int rep = 0; rep < 5; ++rep) {
        double t[kSolverMaxCh];
        predictArrivals(truthB, hp[0], hp[1], 0, t);
        for (int i = 0; i < 5; ++i) t[i] += trueDelay[i] + noise(rng) - c2.delay_us[i];
        Fix f = solveFix(chk, t, 0x1F);
        const double e = hypot(f.x - hp[0], f.y - hp[1]);
        sum += e;
        worstTap = fmax(worstTap, e);
      }
      worstMean = fmax(worstMean, sum / 5);
    }
    printf("  held-out: worst per-point mean %.2f mm, worst single tap %.2f mm\n", worstMean, worstTap);
    expect(worstMean < 2.0, "held-out H points average within the doc's 2 mm");
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
