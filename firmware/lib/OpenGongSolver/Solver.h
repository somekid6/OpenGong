// OpenGong multilateration solver and calibration fit.
// Plain C++ with no Arduino dependencies, so it can be tested on a PC
// (see test/host/solver_sim.cpp).
//
// Units: millimetres, microseconds, wave speed in mm/us (= km/s).
#pragma once

#include <stdint.h>

namespace og {

constexpr int kSolverMaxCh = 6;

// Sensor layout and plate model.
struct Geometry {
  int    n = 0;                     // number of channels
  double x[kSolverMaxCh] = {};      // sensor positions, mm
  double y[kSolverMaxCh] = {};
  double h = 0;                     // plate thickness, mm (0 for back-face taps)
  double v = 5.9;                   // wave speed, mm/us
};

// One solved impact.
struct Fix {
  bool    ok = false;
  double  x = 0, y = 0;   // mm, same frame as the sensors
  double  t0 = 0;         // impact time, us, same clock as the inputs
  double  rms_ns = 0;     // RMS timing residual
  uint8_t used = 0;       // channels used
  int     iters = 0;
};

int popcount8(uint8_t m);

// Path length from the impact point (on the front face) to sensor ch (on the back).
double slantRange(const Geometry& g, int ch, double x, double y);

// Arrival time at every sensor for an impact at (x, y) at time t0.
void predictArrivals(const Geometry& g, double x, double y, double t0, double out_us[]);

// Levenberg-Marquardt fit of x, y, t0. t_us[i] must already have the
// channel's fixed delay removed. Only channels set in mask are used (>= 3).
Fix solveFix(const Geometry& g, const double t_us[], uint8_t mask);

// solveFix, then if the residual exceeds rmsLimitNs and at least 5 channels
// fired, retries without each channel and keeps a clearly better solution.
Fix solveRobust(const Geometry& g, const double t_us[], uint8_t mask,
                double rmsLimitNs, bool* dropped);

// ---------------------------------------------------------------------------
// Calibration: taps at known positions -> wave speed and per-channel delays.
// Model: t_ik = t0_k + d_i + D_ik / v. With known positions this is linear
// in (1/v, d_i, t0_k); t0_k is removed by centring each tap, and d_0 = 0.
// ---------------------------------------------------------------------------
struct CalTap {
  double  x = 0, y = 0;            // known tap position, mm
  double  h = 0;                   // vertical path: thickness for front taps, 0 for back
  double  t_us[kSolverMaxCh] = {}; // raw arrival times (any common offset)
  uint8_t mask = 0;
};

struct CalResult {
  bool   ok = false;
  double v = 0;                          // mm/us
  double delay_us[kSolverMaxCh] = {};    // channel 1 is the 0 reference
  double rms_ns = 0;
  int    tapsUsed = 0;
};

CalResult fitCalibration(const Geometry& g, const CalTap* taps, int ntaps, bool fitSpeed);

// Solves A x = b in place (b receives x). A is n x n row-major. Partial pivoting.
bool solveLinearSystem(double* A, double* b, int n);

}  // namespace og
