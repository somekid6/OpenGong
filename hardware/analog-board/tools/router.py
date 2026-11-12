"""Small two-layer grid router for the OpenGong analog board.

Routes every net except GND with an 8-direction A* search on a 0.25 mm grid.
Clearance is enforced by inflating every obstacle by (clearance + half the new
track width + a discretisation margin), so a track whose centreline stays on
free cells clears everything. GND reaches the copper pours through a short
stub and via next to each SMD ground pad. KiCad's own DRC checks the result.
"""
import heapq
import math

import numpy as np

G = 0.25                 # grid pitch, mm
CLEAR = 0.2              # copper clearance, mm
EDGE_CLEAR = 0.35        # copper to board edge, mm
MARGIN = 0.07            # discretisation allowance, mm
VIA_D, VIA_DRILL = 0.6, 0.3
WIDTH = {"sig": 0.25, "pwr": 0.4}
LAYER_COST = (1.0, 2.5)  # top preferred; keep the bottom ground plane mostly whole
VIA_COST = 6.0
TURN_COST = 0.4
DIRS = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]


class Grid:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.nx, self.ny = int(round(w / G)) + 1, int(round(h / G)) + 1
        xs = np.arange(self.nx) * G
        ys = np.arange(self.ny) * G
        self.X, self.Y = np.meshgrid(xs, ys)          # [j, i]
        # occupancy per class and layer: 0 free, -1 blocked, >0 owning net id
        self.occ = {c: np.zeros((2, self.ny, self.nx), np.int32) for c in ("sig", "pwr", "via")}
        # cells where no via may go, whatever its net (on or next to any pad or hole)
        self.novia = np.zeros((self.ny, self.nx), bool)
        self.infl = {"sig": CLEAR + WIDTH["sig"] / 2 + MARGIN,
                     "pwr": CLEAR + WIDTH["pwr"] / 2 + MARGIN,
                     "via": CLEAR + VIA_D / 2 + MARGIN}
        edge = {c: EDGE_CLEAR + (VIA_D / 2 if c == "via" else WIDTH[c] / 2) + MARGIN for c in self.occ}
        for c, e in edge.items():
            m = (self.X < e) | (self.Y < e) | (self.X > w - e) | (self.Y > h - e)
            self.occ[c][:, m] = -1

    # -- rasterisation -----------------------------------------------------
    def stamp(self, layers, distfn, bbox, net, extra=0.0):
        """Mark cells within inflation of a shape. distfn(X, Y) -> signed distance to copper."""
        x0, y0, x1, y1 = bbox
        pad = max(self.infl.values()) + extra + G
        i0, i1 = max(0, int((x0 - pad) / G)), min(self.nx, int((x1 + pad) / G) + 2)
        j0, j1 = max(0, int((y0 - pad) / G)), min(self.ny, int((y1 + pad) / G) + 2)
        if i0 >= i1 or j0 >= j1:
            return
        d = distfn(self.X[j0:j1, i0:i1], self.Y[j0:j1, i0:i1])
        for c, infl in self.infl.items():
            m = d < infl + extra
            for l in layers:
                g = self.occ[c][l, j0:j1, i0:i1]
                if net < 0:
                    g[m] = -1
                else:
                    g[m & (g == 0)] = net
                    g[m & (g != net)] = -1
        return (i0, j0, d)

    def keepout(self, distfn, bbox, dist):
        x0, y0, x1, y1 = bbox
        i0, i1 = max(0, int((x0 - dist) / G) - 1), min(self.nx, int((x1 + dist) / G) + 2)
        j0, j1 = max(0, int((y0 - dist) / G) - 1), min(self.ny, int((y1 + dist) / G) + 2)
        d = distfn(self.X[j0:j1, i0:i1], self.Y[j0:j1, i0:i1])
        self.novia[j0:j1, i0:i1] |= d < dist

    def cells_inside(self, distfn, bbox, inset=0.02):
        x0, y0, x1, y1 = bbox
        i0, i1 = max(0, int(x0 / G) - 1), min(self.nx, int(x1 / G) + 2)
        j0, j1 = max(0, int(y0 / G) - 1), min(self.ny, int(y1 / G) + 2)
        d = distfn(self.X[j0:j1, i0:i1], self.Y[j0:j1, i0:i1])
        jj, ii = np.nonzero(d < -inset)
        return [(int(i + i0), int(j + j0)) for i, j in zip(ii, jj)]


# -- shape distance functions ---------------------------------------------
def rect_dist(cx, cy, sx, sy, ang_deg):
    a = math.radians(ang_deg)
    ca, sa = math.cos(a), math.sin(a)

    def f(X, Y):
        dx, dy = X - cx, Y - cy
        # KiCad angles are counter-clockwise on screen (y down)
        lx = dx * ca - dy * sa
        ly = dx * sa + dy * ca
        qx, qy = np.abs(lx) - sx / 2, np.abs(ly) - sy / 2
        out = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
        return out + np.minimum(np.maximum(qx, qy), 0)
    r = math.hypot(sx, sy) / 2
    return f, (cx - r, cy - r, cx + r, cy + r)


def circle_dist(cx, cy, r):
    return (lambda X, Y: np.hypot(X - cx, Y - cy) - r), (cx - r, cy - r, cx + r, cy + r)


def seg_dist(x0, y0, x1, y1, hw):
    vx, vy = x1 - x0, y1 - y0
    L2 = vx * vx + vy * vy or 1e-12

    def f(X, Y):
        t = np.clip(((X - x0) * vx + (Y - y0) * vy) / L2, 0, 1)
        return np.hypot(X - (x0 + t * vx), Y - (y0 + t * vy)) - hw
    return f, (min(x0, x1) - hw, min(y0, y1) - hw, max(x0, x1) + hw, max(y0, y1) + hw)


# -- search ------------------------------------------------------------------
def astar(grid, net, cls, sources, targets, target_pts, allow_vias=True, max_expand=1_500_000):
    """sources/targets: sets of (l, i, j). Returns list of (l, i, j) or None."""
    nx, ny = grid.nx, grid.ny
    occ = grid.occ[cls].reshape(2, -1).tolist()
    vocc = grid.occ["via"].reshape(2, -1).tolist()
    novia = grid.novia.reshape(-1).tolist()
    ok0 = [None, None]
    tp = target_pts or [(0, 0)]

    def h(i, j):
        best = 1e18
        for (ti, tj) in tp:
            dx, dy = abs(i - ti), abs(j - tj)
            v = max(dx, dy) + 0.414 * min(dx, dy)
            if v < best:
                best = v
        return best

    openq = []
    gbest = {}
    parent = {}
    for s in sources:
        st = (s[0], s[1], s[2], 8)
        gbest[st] = 0.0
        parent[st] = None
        heapq.heappush(openq, (h(s[1], s[2]), 0.0, st))
    expanded = 0
    while openq:
        f, g, st = heapq.heappop(openq)
        if gbest.get(st, 1e18) < g - 1e-9:
            continue
        l, i, j, d = st
        if (l, i, j) in targets:
            path = []
            while st is not None:
                path.append(st[:3])
                st = parent[st]
            return path[::-1]
        expanded += 1
        if expanded > max_expand:
            return None
        lc = LAYER_COST[l]
        row = occ[l]
        for nd, (di, dj) in enumerate(DIRS):
            ni, nj = i + di, j + dj
            if ni < 0 or nj < 0 or ni >= nx or nj >= ny:
                continue
            o = row[nj * nx + ni]
            if o != 0 and o != net:
                continue
            if di and dj:  # diagonal: both orthogonal neighbours must be free too
                o1 = row[j * nx + ni]
                o2 = row[nj * nx + i]
                if (o1 != 0 and o1 != net) or (o2 != 0 and o2 != net):
                    continue
            step = (1.414 if (di and dj) else 1.0) * lc
            if d != 8 and d != nd:
                step += TURN_COST
            ns = (l, ni, nj, nd)
            ng = g + step
            if ng < gbest.get(ns, 1e18):
                gbest[ns] = ng
                parent[ns] = st
                heapq.heappush(openq, (ng + h(ni, nj), ng, ns))
        if allow_vias and not novia[j * nx + i]:
            k = j * nx + i
            v0, v1 = vocc[0][k], vocc[1][k]
            if (v0 == 0 or v0 == net) and (v1 == 0 or v1 == net):
                ol = 1 - l
                o = occ[ol][k]
                if o == 0 or o == net:
                    ns = (ol, i, j, 8)
                    ng = g + VIA_COST
                    if ng < gbest.get(ns, 1e18):
                        gbest[ns] = ng
                        parent[ns] = st
                        heapq.heappush(openq, (ng + h(i, j), ng, ns))
    return None


def simplify(path):
    """Collapse a cell path into (layer, [points]) runs plus via locations."""
    runs, vias = [], []
    cur = [path[0]]
    for a, b in zip(path, path[1:]):
        if a[0] != b[0]:
            vias.append((a[1], a[2]))
            runs.append(cur)
            cur = [b]
        else:
            cur.append(b)
    runs.append(cur)
    out = []
    for run in runs:
        pts = [run[0]]
        for k in range(1, len(run) - 1):
            p, q, r = pts[-1], run[k], run[k + 1]
            d1 = (q[1] - p[1], q[2] - p[2])
            d2 = (r[1] - q[1], r[2] - q[2])
            if d1[0] * d2[1] - d1[1] * d2[0] != 0:
                pts.append(q)
        if len(run) > 1:
            pts.append(run[-1])
        out.append((run[0][0], [(c[1] * G, c[2] * G) for c in pts]))
    return out, [(i * G, j * G) for i, j in vias]
