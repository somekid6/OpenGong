"""Generate opengong-analog.kicad_pcb (placement, routing, pours, silkscreen).

Run with KiCad's bundled Python:
    "<KiCad>/bin/python.exe" -I tools/gen_board.py
"""
import json
import math
import os
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pcbnew  # noqa: E402

import router as R  # noqa: E402
from design import PARTS, POWER_NETS, board_net, nets as design_nets  # noqa: E402

OUT = os.path.dirname(HERE)
NAME = "opengong-analog"
BOARD_PATH = os.path.join(OUT, NAME + ".kicad_pcb")
PRO_PATH = os.path.join(OUT, NAME + ".kicad_pro")
FPDIR = os.environ.get("KICAD10_FOOTPRINT_DIR") or os.path.join(
    os.path.dirname(sys.executable), "..", "share", "kicad", "footprints")

W, H = 100.0, 100.0          # board size, mm
OX, OY = 50.0, 50.0          # board origin on the KiCad page
REV = "1.0"

mm = pcbnew.FromMM


def P(x, y):
    return pcbnew.VECTOR2I(mm(x + OX), mm(y + OY))


def sym_uuid(ref):
    """Deterministic symbol UUID shared with gen_schematic.py."""
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "opengong-analog/" + ref))


# ---------------------------------------------------------------- placement
COL = {1: 12.0, 2: 27.0, 3: 42.0, 4: 57.0, 5: 72.0}
PAIR = {1: 19.5, 2: 49.5, 3: 79.5}
Y_TERM, Y_IN, Y_CLAMP, Y_HP, Y_AMP, Y_TP, Y_CMP, Y_OUT = 93.0, 86.6, 80.6, 75.0, 64.0, 57.0, 50.0, 45.6

# Heltec V3 (datasheet HTIT-WB32LA_V3 Rev 1.1): 50.2 x 25.5 mm, 2 x 18 pins at 2.54 mm,
# rows 22.86 mm apart, pin 1 of both rows at the USB-C end. The module sits with its
# USB-C end flush with the right board edge, so Heltec J3 (3V3, GPIO2-7) is the lower row.
# The datasheet drawing gives the 25.5 mm width and 2.54 mm pitch but not the row spacing;
# 22.86 mm (0.9 in) puts both rows 1.32 mm from the module edges. Measure your module's
# row centres with calipers before ordering; if they differ, change this and run build.py.
HELTEC_ROW_SPACING = 22.86
HELTEC_X0, HELTEC_Y0 = W - 50.2, 1.0
ROW_J2_Y = HELTEC_Y0 + (25.5 - HELTEC_ROW_SPACING) / 2
ROW_J3_Y = ROW_J2_Y + HELTEC_ROW_SPACING
PIN1_X = W - 2.8

PLACE = {}
for n, c in COL.items():
    PLACE[f"J{n}"] = (c - 1.75, Y_TERM, 0)
    PLACE[f"R{n}2"] = (c - 2.0, Y_IN, 90)
    PLACE[f"R{n}1"] = (c + 2.0, Y_IN, 90)
    PLACE[f"D{n}"] = (c, Y_CLAMP, 0)
    PLACE[f"C{n}1"] = (c + 2.0, Y_HP, 90)
    PLACE[f"R{n}3"] = (c - 2.0, Y_HP, 90)
    PLACE[f"R{n}5"] = (c, Y_AMP - 2.0, 0)
    PLACE[f"R{n}6"] = (c, Y_AMP + 2.0, 0)
    PLACE[f"TP{n}"] = (c, Y_TP, 0)
    PLACE[f"R{n}4"] = (c, Y_OUT, 90)
for k, (amp, cmp_, dc_a, dc_c) in enumerate([("U1", "U4", "C101", "C104"), ("U2", "U5", "C102", "C105"),
                                             ("U3", "U6", "C103", "C106")], start=1):
    px = PAIR[k]
    PLACE[amp] = (px, Y_AMP, 0)
    PLACE[cmp_] = (px, Y_CMP, 0)
    PLACE[dc_a] = (px + 2.2, Y_AMP - 4.4, 0)
    PLACE[dc_c] = (px + 2.2, Y_CMP - 4.4, 0)
# VMID divider beside the U3B buffer.
PLACE["R1"] = (85.2, Y_AMP - 3.0, 90)
PLACE["R2"] = (85.2, Y_AMP + 2.0, 90)
PLACE["C1"] = (85.2, Y_AMP + 6.0, 90)
# Analog supply filter next to the Heltec 3V3 pins.
PLACE["FB1"] = (93.0, 30.0, 180)
PLACE["C3"] = (88.5, 30.0, 0)
PLACE["C4"] = (84.5, 30.0, 0)
PLACE["TP8"] = (80.5, 30.0, 0)
# Threshold trimmer and test points, top left (not covered by the Heltec).
PLACE["RV1"] = (24.0, 14.0, 0)
PLACE["R3"] = (22.0, 20.0, 0)
PLACE["C2"] = (27.0, 20.0, 90)
PLACE["TP6"] = (32.0, 12.0, 0)
PLACE["TP7"] = (36.0, 12.0, 0)
PLACE["TP9"] = (40.5, 12.0, 0)
PLACE["H1"] = (4.0, 4.0, 0)
PLACE["H2"] = (44.5, 4.0, 0)
PLACE["H3"] = (4.0, 96.0, 0)
PLACE["H4"] = (96.0, 96.0, 0)
PLACE["J7"] = (PIN1_X, ROW_J2_Y, -90)
PLACE["J8"] = (PIN1_X, ROW_J3_Y, -90)


# ---------------------------------------------------------------- project file
def write_project():
    pro = {
        "board": {
            "design_settings": {
                "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2,
                             "silk_line_width": 0.15, "silk_text_size_h": 0.8,
                             "silk_text_size_v": 0.8, "silk_text_thickness": 0.12},
                "rules": {"min_clearance": 0.2, "min_track_width": 0.2, "min_via_diameter": 0.6,
                          "min_via_annular_width": 0.13, "min_through_hole_diameter": 0.3,
                          "min_hole_to_hole": 0.25, "min_copper_edge_clearance": 0.3,
                          "min_hole_clearance": 0.25, "min_silk_clearance": 0.0,
                          "min_text_height": 0.8, "min_text_thickness": 0.12},
                "track_widths": [0.0, 0.25, 0.4],
                "via_dimensions": [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.6, "drill": 0.3}],
            },
        },
        "meta": {"filename": NAME + ".kicad_pro", "version": 3},
        "net_settings": {
            "classes": [
                {"name": "Default", "clearance": 0.2, "track_width": 0.25, "via_diameter": 0.6,
                 "via_drill": 0.3, "microvia_diameter": 0.3, "microvia_drill": 0.1,
                 "diff_pair_width": 0.2, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                 "wire_width": 6, "bus_width": 12, "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
                 "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 2147483647},
                {"name": "Power", "clearance": 0.2, "track_width": 0.4, "via_diameter": 0.6,
                 "via_drill": 0.3, "microvia_diameter": 0.3, "microvia_drill": 0.1,
                 "diff_pair_width": 0.2, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                 "wire_width": 6, "bus_width": 12, "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
                 "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 0},
            ],
            "meta": {"version": 4},
            "netclass_patterns": [{"netclass": "Power", "pattern": n} for n in sorted(POWER_NETS)],
        },
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[sym_uuid("__root_sheet__"), "Root"]],
    }
    with open(PRO_PATH, "w", encoding="utf8") as f:
        json.dump(pro, f, indent=2)


# ---------------------------------------------------------------- board build
def load_fp(fpid):
    lib, name = fpid.split(":")
    fp = pcbnew.FootprintLoad(os.path.join(FPDIR, lib + ".pretty"), name)
    if fp is None:
        raise SystemExit("footprint not found: " + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def add_text(board, text, x, y, size=1.0, layer=pcbnew.F_SilkS, bold=False, angle=0, just=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(P(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(max(0.12, size * (0.2 if bold else 0.15))))
    if angle:
        t.SetTextAngleDegrees(angle)
    if just == "left":
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)


def add_rect(board, x0, y0, x1, y1, layer, width):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_RECT)
    s.SetStart(P(x0, y0))
    s.SetEnd(P(x1, y1))
    s.SetLayer(layer)
    s.SetWidth(mm(width))
    board.Add(s)


def add_line(board, x0, y0, x1, y1, layer, width):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(x0, y0))
    s.SetEnd(P(x1, y1))
    s.SetLayer(layer)
    s.SetWidth(mm(width))
    board.Add(s)


def build():
    write_project()
    board = pcbnew.NewBoard(BOARD_PATH)
    board.SetCopperLayerCount(2)
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(0.2)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_ViasMinAnnularWidth = mm(0.13)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_HoleToHoleMin = mm(0.25)
    ds.m_HoleClearance = mm(0.25)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_SilkClearance = mm(0.0)
    ds.m_MinResolvedSpokes = 1   # every SMD ground pad also has its own via
    add_rect(board, 0, 0, W, H, pcbnew.Edge_Cuts, 0.1)

    netinfo = {}
    for name in sorted(design_nets()):
        ni = pcbnew.NETINFO_ITEM(board, board_net(name))
        board.Add(ni)
        netinfo[name] = ni

    fps = {}
    for p in PARTS:
        fp = load_fp(p["fp"])
        fp.SetReference(p["ref"])
        fp.SetValue(p["value"])
        x, y, rot = PLACE[p["ref"]]
        fp.SetPosition(P(x, y))
        fp.SetOrientationDegrees(rot)
        fp.SetPath(pcbnew.KIID_PATH("/" + sym_uuid(p["ref"])))
        fp.SetField("MPN", p["mpn"])
        fp.GetField("Description").SetText(p["note"]) if fp.HasField("Description") else None
        for fname in ("MPN",):
            fp.GetField(fname).SetVisible(False)
        if hasattr(fp, "SetSheetfile"):
            fp.SetSheetfile(NAME + ".kicad_sch")
            fp.SetSheetname("Root")
        board.Add(fp)
        fp.SetExcludedFromBOM(not p["in_bom"])
        for pad in fp.Pads():
            num = pad.GetNumber()
            net = p["pins"].get(num)
            if net:
                pad.SetNet(netinfo[net])
            elif num in p["pins"]:
                # KiCad names a no-connect pin's net after the symbol pin
                nc = pcbnew.NETINFO_ITEM(board, f"unconnected-({p['ref']}-Pin_{num}-Pad{num})")
                board.Add(nc)
                pad.SetNet(nc)
        ref = fp.Reference()
        ref.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        ref.SetTextThickness(mm(0.12))
        fps[p["ref"]] = fp
        if p["ref"].startswith(("H", "J", "TP", "RV")):
            ref.SetVisible(False)
        if p["ref"].startswith("U"):
            # IC references go on the package body, clear of the decoupling caps
            ref.SetPosition(P(x, y))
            ref.SetTextAngleDegrees(0)

    # sanity: socket pins must run right-to-left from pin 1
    for j in ("J7", "J8"):
        a = fps[j].FindPadByNumber("1").GetPosition()
        b = fps[j].FindPadByNumber("18").GetPosition()
        assert pcbnew.ToMM(a.x) > pcbnew.ToMM(b.x) and abs(a.y - b.y) < 10, j
        assert abs(pcbnew.ToMM(a.x - b.x) - 43.18) < 0.01, j

    route(board, fps, netinfo)
    pours_and_stitching(board, netinfo)
    silkscreen(board)

    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    pcbnew.SaveBoard(BOARD_PATH, board)
    print("saved", BOARD_PATH)


# ---------------------------------------------------------------- routing
def pad_geom(pad):
    pos = pad.GetPosition()
    x, y = pcbnew.ToMM(pos.x) - OX, pcbnew.ToMM(pos.y) - OY
    sz = pad.GetSize(pcbnew.F_Cu)
    sx, sy = pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)
    ang = pad.GetOrientation().AsDegrees()
    shape = pad.GetShape(pcbnew.F_Cu)
    if shape == pcbnew.PAD_SHAPE_CIRCLE:
        f, bb = R.circle_dist(x, y, sx / 2)
    else:
        f, bb = R.rect_dist(x, y, sx, sy, ang)
    attr = pad.GetAttribute()
    layers = [0] if attr == pcbnew.PAD_ATTRIB_SMD else [0, 1]
    return x, y, f, bb, layers, attr


def route(board, fps, netinfo):
    grid = R.Grid(W, H)
    names = sorted(netinfo)
    nid = {n: k + 1 for k, n in enumerate(names)}
    pads = {}          # net -> list of pad records
    gnd_smd = []
    for ref, fp in fps.items():
        for pad in fp.Pads():
            x, y, f, bb, layers, attr = pad_geom(pad)
            if attr == pcbnew.PAD_ATTRIB_NPTH:
                drill = pcbnew.ToMM(pad.GetDrillSize().x)
                cf, cbb = R.circle_dist(x, y, drill / 2)
                grid.stamp([0, 1], cf, cbb, -1, extra=0.1)
                grid.keepout(cf, cbb, R.VIA_D / 2 + 0.45)
                continue
            net = pad.GetNetname().lstrip("/")
            if net.startswith("unconnected-"):
                net = ""
            grid.stamp(layers, f, bb, nid[net] if net else -1)
            grid.keepout(f, bb, R.VIA_D / 2 + 0.2)
            if not net:
                continue
            rec = dict(ref=ref, num=pad.GetNumber(), x=x, y=y, f=f, layers=layers,
                       cells=grid.cells_inside(f, bb))
            pads.setdefault(net, []).append(rec)
            if net == "GND" and attr == pcbnew.PAD_ATTRIB_SMD:
                gnd_smd.append(rec)

    tracks, vias = [], []

    def commit(net, runs, vlist, cls, extra_segs=()):
        w = R.WIDTH[cls]
        for layer, pts in runs:
            for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
                tracks.append((layer, x0, y0, x1, y1, w, net))
                f, bb = R.seg_dist(x0, y0, x1, y1, w / 2)
                grid.stamp([layer], f, bb, nid[net])
        for layer, x0, y0, x1, y1 in extra_segs:
            if (x0, y0) != (x1, y1):
                tracks.append((layer, x0, y0, x1, y1, w, net))
                f, bb = R.seg_dist(x0, y0, x1, y1, w / 2)
                grid.stamp([layer], f, bb, nid[net])
        for vx, vy in vlist:
            vias.append((vx, vy, net))
            f, bb = R.circle_dist(vx, vy, R.VIA_D / 2)
            grid.stamp([0, 1], f, bb, nid[net])

    # 1. ground escapes: short stub + via beside each SMD ground pad
    gid = nid["GND"]
    for rec in sorted(gnd_smd, key=lambda r: (r["ref"], r["num"])):
        res = gnd_escape(grid, rec, gid)
        if res is None:
            print("WARN no GND escape for", rec["ref"], rec["num"])
            continue
        path, (vx, vy) = res
        runs, _ = R.simplify(path) if len(path) > 1 else ([], [])
        commit("GND", runs, [(vx, vy)], "pwr", [(0, rec["x"], rec["y"], path[0][1] * R.G, path[0][2] * R.G)])

    base_occ = {c: a.copy() for c, a in grid.occ.items()}
    base_tracks, base_vias = list(tracks), list(vias)

    order = [n for n in names if n != "GND"]

    def hpwl(n):
        xs = [p["x"] for p in pads[n]]
        ys = [p["y"] for p in pads[n]]
        return (max(xs) - min(xs)) + (max(ys) - min(ys))
    late = {"+3V3A": 3, "VMID": 2, "VTH": 1}
    order.sort(key=lambda n: (late.get(n, 0), hpwl(n)))

    for attempt in range(12):
        failed = []
        for net in order:
            ok = route_net(grid, net, nid[net], pads[net], commit)
            if not ok:
                failed.append(net)
        print(f"attempt {attempt}: {len(failed)} failed {failed}")
        if not failed:
            break
        order = failed + [n for n in order if n not in failed]
        for c in grid.occ:
            grid.occ[c][:] = base_occ[c]
        tracks[:] = base_tracks
        vias[:] = base_vias
    else:
        raise SystemExit("routing failed: " + ", ".join(failed))

    for layer, x0, y0, x1, y1, w, net in split_tjunctions(tracks):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(P(x0, y0))
        t.SetEnd(P(x1, y1))
        t.SetWidth(mm(w))
        t.SetLayer(pcbnew.F_Cu if layer == 0 else pcbnew.B_Cu)
        t.SetNet(netinfo[net])
        board.Add(t)
    for vx, vy, net in vias:
        add_via(board, vx, vy, netinfo[net])
    route.grid = grid
    route.nid = nid


def split_tjunctions(tracks):
    """Split a track wherever another same-net track on that layer ends mid-segment."""
    out = list(tracks)
    changed = True
    while changed:
        changed = False
        ends = {(t[0], t[6], round(px, 4), round(py, 4))
                for t in out for px, py in ((t[1], t[2]), (t[3], t[4]))}
        for k, (layer, x0, y0, x1, y1, w, net) in enumerate(out):
            for (l2, n2, px, py) in ends:
                if l2 != layer or n2 != net:
                    continue
                if (px, py) in ((round(x0, 4), round(y0, 4)), (round(x1, 4), round(y1, 4))):
                    continue
                vx, vy = x1 - x0, y1 - y0
                L2 = vx * vx + vy * vy
                t = ((px - x0) * vx + (py - y0) * vy) / L2 if L2 else -1
                if 0 < t < 1 and abs((px - x0) * vy - (py - y0) * vx) / math.sqrt(L2) < 1e-4:
                    out[k] = (layer, x0, y0, px, py, w, net)
                    out.append((layer, px, py, x1, y1, w, net))
                    changed = True
                    break
            if changed:
                break
    return out


def add_via(board, x, y, ni):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(P(x, y))
    v.SetWidth(mm(R.VIA_D))
    v.SetDrill(mm(R.VIA_DRILL))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetNet(ni)
    board.Add(v)


def gnd_escape(grid, rec, gid, max_r=3.0):
    """Dijkstra from the pad on the top layer to the nearest legal via spot off the pad."""
    import heapq
    nx = grid.nx
    occ = grid.occ["pwr"][0]
    vocc = grid.occ["via"]
    srcs = rec["cells"]
    dist = {}
    parent = {}
    q = []
    for (i, j) in srcs:
        dist[(i, j)] = 0.0
        parent[(i, j)] = None
        heapq.heappush(q, (0.0, i, j))
    while q:
        d, i, j = heapq.heappop(q)
        if d > dist.get((i, j), 1e9) or d > max_r / R.G:
            continue
        x, y = i * R.G, j * R.G
        own = float(rec["f"](x, y))
        v0, v1 = vocc[0, j, i], vocc[1, j, i]
        if not grid.novia[j, i] and own > R.VIA_D / 2 + 0.12 and v0 in (0, gid) and v1 in (0, gid):
            path = []
            k = (i, j)
            while k is not None:
                path.append((0, k[0], k[1]))
                k = parent[k]
            return path[::-1], (x, y)
        for di, dj in R.DIRS:
            ni_, nj_ = i + di, j + dj
            if not (0 <= ni_ < nx and 0 <= nj_ < grid.ny):
                continue
            if occ[nj_, ni_] not in (0, gid):
                continue
            nd = d + (1.414 if di and dj else 1.0)
            if nd < dist.get((ni_, nj_), 1e9):
                dist[(ni_, nj_)] = nd
                parent[(ni_, nj_)] = (i, j)
                heapq.heappush(q, (nd, ni_, nj_))
    return None


committed = {}


def route_net(grid, net, id_, prs, commit):
    cls = "pwr" if net in POWER_NETS else "sig"
    # Prim order from the pad nearest the net's centroid
    cx = sum(p["x"] for p in prs) / len(prs)
    cy = sum(p["y"] for p in prs) / len(prs)
    rest = sorted(prs, key=lambda p: (p["x"] - cx) ** 2 + (p["y"] - cy) ** 2)
    tree = [rest.pop(0)]
    tree_cells = set()
    pad_of_cell = {}
    for l in tree[0]["layers"]:
        for c in tree[0]["cells"]:
            tree_cells.add((l, c[0], c[1]))
            pad_of_cell[(l, c[0], c[1])] = tree[0]
    tpts = [(int(round(tree[0]["x"] / R.G)), int(round(tree[0]["y"] / R.G)))]
    while rest:
        rest.sort(key=lambda p: min((p["x"] - t["x"]) ** 2 + (p["y"] - t["y"]) ** 2 for t in tree))
        p = rest.pop(0)
        srcs = {(l, c[0], c[1]) for l in p["layers"] for c in p["cells"]}
        path = R.astar(grid, id_, cls, srcs, tree_cells, tpts)
        if path is None:
            return False
        runs, vlist = R.simplify(path)
        s, e = path[0], path[-1]
        extra = [(s[0], p["x"], p["y"], s[1] * R.G, s[2] * R.G)]
        tp = pad_of_cell.get(e)
        if tp is not None:
            extra.append((e[0], e[1] * R.G, e[2] * R.G, tp["x"], tp["y"]))
        if tp is None and e not in committed.get(net, set()):
            print("WARN path end not on committed copper:", net, e)
        commit(net, runs, vlist, cls, extra)
        committed.setdefault(net, set()).update(path)
        prev = None
        for c in path:
            tree_cells.add(c)
            if prev is not None and prev[0] != c[0]:
                tree_cells.add((prev[0], c[1], c[2]))
            prev = c
        for l in p["layers"]:
            for c in p["cells"]:
                tree_cells.add((l, c[0], c[1]))
                pad_of_cell[(l, c[0], c[1])] = p
        tree.append(p)
        tpts.append((int(round(p["x"] / R.G)), int(round(p["y"] / R.G))))
        tpts.extend((c[1], c[2]) for c in path[:: max(1, len(path) // 6)])
    return True


# ---------------------------------------------------------------- pours
def pours_and_stitching(board, netinfo):
    gnd = netinfo["GND"]
    grid, gid = route.grid, route.nid["GND"]
    # stitching vias on a 5 mm lattice wherever both layers are free
    for yy in range(3, int(H), 5):
        for xx in range(3, int(W), 5):
            i, j = int(round(xx / R.G)), int(round(yy / R.G))
            v = grid.occ["via"][:, j, i]
            if all(o == 0 for o in v) and not grid.novia[j, i]:
                add_via(board, xx, yy, gnd)
                f, bb = R.circle_dist(xx, yy, R.VIA_D / 2)
                grid.stamp([0, 1], f, bb, gid)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in ((0, 0), (W, 0), (W, H), (0, H)):
            ol.Append(mm(x + OX), mm(y + OY))
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetThermalReliefGap(mm(0.4))
        z.SetThermalReliefSpokeWidth(mm(0.45))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetAssignedPriority(0)
        board.Add(z)


# ---------------------------------------------------------------- silkscreen
def silkscreen(board):
    S = pcbnew.F_SilkS
    add_text(board, f"OpenGong analog front end  rev {REV}", 24.0, 4.2, 1.2, bold=True)
    add_text(board, "5 ch piezo TDOA  /  Heltec WiFi LoRa 32 V3", 24.0, 6.4, 0.8)
    # Heltec outline and orientation
    # Module outline, drawn just outside the socket silkscreen.
    add_rect(board, HELTEC_X0, HELTEC_Y0 - 0.5, W - 0.5, HELTEC_Y0 + 26.0, S, 0.15)
    add_text(board, "HELTEC V3 HERE  -  OLED FACING UP", HELTEC_X0 + 25.1, HELTEC_Y0 + 9.6, 1.0, bold=True)
    add_text(board, "USB-C this edge  >", HELTEC_X0 + 33.0, HELTEC_Y0 + 13.6, 0.9)
    add_text(board, "J2 row (GND 5V Ve ...)", HELTEC_X0 + 16.0, HELTEC_Y0 + 4.6, 0.8)
    add_text(board, "J3 row (GND 3V3 3V3 ... GPIO7)", HELTEC_X0 + 18.0, HELTEC_Y0 + 20.8, 0.8)
    # threshold and test points
    add_text(board, "RV1  THRESHOLD (VTH)", 21.5, 9.6, 0.8)
    add_text(board, "VMID", 32.0, 14.4, 0.8)
    add_text(board, "VTH", 36.0, 14.4, 0.8)
    add_text(board, "GND", 40.5, 14.9, 0.8)
    add_text(board, "+3V3A", 80.5, 32.4, 0.8)
    # sensor terminals
    for n, c in COL.items():
        add_text(board, f"S{n}", c + 5.6, Y_TERM - 0.6, 1.2, bold=True)
        add_text(board, f"J{n}", c + 5.6, Y_TERM + 1.4, 0.8)
        add_text(board, f"AMP{n}", PLACE[f"TP{n}"][0], Y_TP - 2.2, 0.8)
    add_text(board, "Sensor terminals: left = signal (piezo centre), right = GND (shield)",
             50.0, 99.0, 0.8)
    # back side
    B = pcbnew.B_SilkS
    add_text(board, f"OpenGong analog front end rev {REV}", 50.0, 50.0, 1.5, layer=B, bold=True)
    add_text(board, "Gain = 1 + Rn5/Rn6   HP = 1/(2pi Rn3 Cn1)", 50.0, 53.0, 1.0, layer=B)


if __name__ == "__main__":
    build()
