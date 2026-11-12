#include "Solver.h"

#include <math.h>
#include <string.h>

namespace og {

int popcount8(uint8_t m) {
  int c = 0;
  while (m) {
    m &= static_cast<uint8_t>(m - 1);
    ++c;
  }
  return c;
}

static inline double rangeTo(double sx, double sy, double h, double x, double y) {
  const double dx = x - sx, dy = y - sy;
  const double d = sqrt(dx * dx + dy * dy + h * h);
  return d < 1e-6 ? 1e-6 : d;
}

double slantRange(const Geometry& g, int ch, double x, double y) {
  return rangeTo(g.x[ch], g.y[ch], g.h, x, y);
}

void predictArrivals(const Geometry& g, double x, double y, double t0, double out_us[]) {
  for (int i = 0; i < g.n; ++i) out_us[i] = t0 + slantRange(g, i, x, y) / g.v;
}

bool solveLinearSystem(double* A, double* b, int n) {
  double scale = 0;
  for (int i = 0; i < n * n; ++i) scale = fmax(scale, fabs(A[i]));
  if (!(scale > 0)) return false;
  const double tol = scale * 1e-13;

  for (int c = 0; c < n; ++c) {
    int piv = c;
    double best = fabs(A[c * n + c]);
    for (int r = c + 1; r < n; ++r) {
      const double v = fabs(A[r * n + c]);
      if (v > best) {
        best = v;
        piv = r;
      }
    }
    if (best < tol) return false;
    if (piv != c) {
      for (int k = 0; k < n; ++k) {
        const double t = A[c * n + k];
        A[c * n + k] = A[piv * n + k];
        A[piv * n + k] = t;
      }
      const double t = b[c];
      b[c] = b[piv];
      b[piv] = t;
    }
    for (int r = c + 1; r < n; ++r) {
      const double f = A[r * n + c] / A[c * n + c];
      if (f == 0) continue;
      for (int k = c; k < n; ++k) A[r * n + k] -= f * A[c * n + k];
      b[r] -= f * b[c];
    }
  }
  for (int r = n - 1; r >= 0; --r) {
    double s = b[r];
    for (int k = r + 1; k < n; ++k) s -= A[r * n + k] * b[k];
    b[r] = s / A[r * n + r];
  }
  return true;
}

namespace {

struct Problem {
  const Geometry* g;
  const double* t;
  int idx[kSolverMaxCh];
  int m;
};

double cost(const Problem& p, double x, double y, double t0) {
  double c = 0;
  for (int j = 0; j < p.m; ++j) {
    const int i = p.idx[j];
    const double r = t0 + slantRange(*p.g, i, x, y) / p.g->v - p.t[i];
    c += r * r;
  }
  return c;
}

// Best impact time for a given position (mean of back-projected times).
double bestT0(const Problem& p, double x, double y) {
  double s = 0;
  for (int j = 0; j < p.m; ++j) {
    const int i = p.idx[j];
    s += p.t[i] - slantRange(*p.g, i, x, y) / p.g->v;
  }
  return s / p.m;
}

Fix runLM(const Problem& p, double x, double y) {
  const Geometry& g = *p.g;
  double t0 = bestT0(p, x, y);
  double c = cost(p, x, y, t0);
  double lambda = 1e-3;
  int it = 0;

  for (; it < 100; ++it) {
    double JtJ[9] = {0}, Jtr[3] = {0};
    for (int j = 0; j < p.m; ++j) {
      const int i = p.idx[j];
      const double D = slantRange(g, i, x, y);
      const double r = t0 + D / g.v - p.t[i];
      const double J[3] = {(x - g.x[i]) / (D * g.v), (y - g.y[i]) / (D * g.v), 1.0};
      for (int a = 0; a < 3; ++a) {
        Jtr[a] += J[a] * r;
        for (int b = 0; b < 3; ++b) JtJ[a * 3 + b] += J[a] * J[b];
      }
    }

    bool accepted = false;
    double step = 0;
    while (lambda < 1e10) {
      double A[9];
      memcpy(A, JtJ, sizeof A);
      for (int a = 0; a < 3; ++a) A[a * 3 + a] += lambda * JtJ[a * 3 + a] + 1e-12;
      double d[3] = {-Jtr[0], -Jtr[1], -Jtr[2]};
      if (!solveLinearSystem(A, d, 3)) {
        lambda *= 10;
        continue;
      }
      const double nx = x + d[0], ny = y + d[1], nt = t0 + d[2];
      const double nc = cost(p, nx, ny, nt);
      if (nc < c) {
        step = fabs(d[0]) + fabs(d[1]);
        x = nx;
        y = ny;
        t0 = nt;
        c = nc;
        lambda = fmax(lambda * 0.1, 1e-9);
        accepted = true;
        break;
      }
      lambda *= 10;
    }
    if (!accepted || step < 1e-4) break;  // converged to 0.1 um
  }

  Fix f;
  f.ok = isfinite(x) && isfinite(y) && isfinite(c);
  f.x = x;
  f.y = y;
  f.t0 = t0;
  f.rms_ns = sqrt(c / p.m) * 1000.0;
  f.iters = it;
  return f;
}

}  // namespace

Fix solveFix(const Geometry& g, const double t_us[], uint8_t mask) {
  Problem p;
  p.g = &g;
  p.t = t_us;
  p.m = 0;
  const uint8_t all = static_cast<uint8_t>((1u << g.n) - 1);
  mask &= all;
  for (int i = 0; i < g.n; ++i)
    if (mask & (1u << i)) p.idx[p.m++] = i;

  Fix best;
  best.used = mask;
  if (p.m < 3 || !(g.v > 0)) return best;

  // Starting points: ring centroid, near the first-hit sensor, and between
  // the two earliest sensors. LM from any of these converges inside the ring.
  double cx = 0, cy = 0;
  int e1 = p.idx[0], e2 = -1;
  for (int j = 0; j < p.m; ++j) {
    const int i = p.idx[j];
    cx += g.x[i];
    cy += g.y[i];
    if (t_us[i] < t_us[e1]) e1 = i;
  }
  cx /= p.m;
  cy /= p.m;
  for (int j = 0; j < p.m; ++j) {
    const int i = p.idx[j];
    if (i != e1 && (e2 < 0 || t_us[i] < t_us[e2])) e2 = i;
  }
  const double starts[3][2] = {
      {cx, cy},
      {0.7 * g.x[e1] + 0.3 * cx, 0.7 * g.y[e1] + 0.3 * cy},
      {0.35 * (g.x[e1] + g.x[e2]) + 0.3 * cx, 0.35 * (g.y[e1] + g.y[e2]) + 0.3 * cy},
  };

  for (const auto& s : starts) {
    Fix f = runLM(p, s[0], s[1]);
    if (f.ok && (!best.ok || f.rms_ns < best.rms_ns)) best = f;
  }
  best.used = mask;
  return best;
}

Fix solveRobust(const Geometry& g, const double t_us[], uint8_t mask,
                double rmsLimitNs, bool* dropped) {
  if (dropped) *dropped = false;
  mask &= static_cast<uint8_t>((1u << g.n) - 1);
  Fix full = solveFix(g, t_us, mask);
  if (full.ok && full.rms_ns <= rmsLimitNs) return full;
  // With 4 channels left after dropping one there is still 1 spare
  // equation, so the residual remains meaningful.
  if (popcount8(mask) < 5) return full;

  Fix best;
  for (int i = 0; i < g.n; ++i) {
    if (!(mask & (1u << i))) continue;
    Fix f = solveFix(g, t_us, static_cast<uint8_t>(mask & ~(1u << i)));
    if (f.ok && (!best.ok || f.rms_ns < best.rms_ns)) best = f;
  }
  if (best.ok && (!full.ok || best.rms_ns < 0.5 * full.rms_ns)) {
    if (dropped) *dropped = true;
    return best;
  }
  return full;
}

CalResult fitCalibration(const Geometry& g, const CalTap* taps, int ntaps, bool fitSpeed) {
  CalResult res;
  const int n = g.n;
  if (n < 2 || !(g.v > 0)) return res;
  const int off = fitSpeed ? 1 : 0;
  const int P = off + (n - 1);
  const uint8_t all = static_cast<uint8_t>((1u << n) - 1);
  const double sFixed = 1.0 / g.v;

  double N[(1 + kSolverMaxCh) * (1 + kSolverMaxCh)] = {0};
  double rhs[1 + kSolverMaxCh] = {0};

  for (int k = 0; k < ntaps; ++k) {
    const CalTap& tp = taps[k];
    const uint8_t mask = tp.mask & all;
    const int m = popcount8(mask);
    if (m < 3) continue;
    res.tapsUsed++;

    double D[kSolverMaxCh] = {0}, Dbar = 0, tbar = 0;
    for (int i = 0; i < n; ++i) {
      if (!(mask & (1u << i))) continue;
      D[i] = rangeTo(g.x[i], g.y[i], tp.h, tp.x, tp.y);
      Dbar += D[i];
      tbar += tp.t_us[i];
    }
    Dbar /= m;
    tbar /= m;

    for (int i = 0; i < n; ++i) {
      if (!(mask & (1u << i))) continue;
      double a[1 + kSolverMaxCh] = {0};
      if (fitSpeed) a[0] = D[i] - Dbar;
      for (int c = 1; c < n; ++c) {
        a[off + c - 1] = (c == i ? 1.0 : 0.0) - ((mask & (1u << c)) ? 1.0 / m : 0.0);
      }
      const double b = (tp.t_us[i] - tbar) - (fitSpeed ? 0.0 : sFixed * (D[i] - Dbar));
      for (int r = 0; r < P; ++r) {
        rhs[r] += a[r] * b;
        for (int c = 0; c < P; ++c) N[r * P + c] += a[r] * a[c];
      }
    }
  }
  if (res.tapsUsed < 2) return res;
  if (!solveLinearSystem(N, rhs, P)) return res;

  const double s = fitSpeed ? rhs[0] : sFixed;
  if (!(s > 0)) return res;
  res.v = 1.0 / s;
  res.delay_us[0] = 0;
  for (int c = 1; c < n; ++c) res.delay_us[c] = rhs[off + c - 1];

  double sse = 0;
  int cnt = 0;
  for (int k = 0; k < ntaps; ++k) {
    const CalTap& tp = taps[k];
    const uint8_t mask = tp.mask & all;
    const int m = popcount8(mask);
    if (m < 3) continue;
    double D[kSolverMaxCh] = {0}, Dbar = 0, tbar = 0, dbar = 0;
    for (int i = 0; i < n; ++i) {
      if (!(mask & (1u << i))) continue;
      D[i] = rangeTo(g.x[i], g.y[i], tp.h, tp.x, tp.y);
      Dbar += D[i];
      tbar += tp.t_us[i];
      dbar += res.delay_us[i];
    }
    Dbar /= m;
    tbar /= m;
    dbar /= m;
    for (int i = 0; i < n; ++i) {
      if (!(mask & (1u << i))) continue;
      const double e = (tp.t_us[i] - tbar) - ((res.delay_us[i] - dbar) + s * (D[i] - Dbar));
      sse += e * e;
      ++cnt;
    }
  }
  res.rms_ns = cnt ? sqrt(sse / cnt) * 1000.0 : 0;
  res.ok = true;
  return res;
}

}  // namespace og
