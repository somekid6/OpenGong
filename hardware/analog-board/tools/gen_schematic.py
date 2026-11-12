"""Generate opengong-analog.kicad_sch from design.py.

Every pin gets a net label at its connection point, so the drawing is a
label-connected schematic laid out channel by channel. Run with KiCad's Python:
    "<KiCad>/bin/python.exe" -I tools/gen_schematic.py
"""
import math
import os
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from sexpr import Sym, dump, find, find1, parse  # noqa: E402
from design import (AMP, CHANNELS, CMP, GPIO, HELTEC_J2, HELTEC_J3, ICS, PARTS, POWER_NETS)  # noqa: E402
from gen_board import NAME, REV, sym_uuid  # noqa: E402

OUT = os.path.dirname(HERE)
SCH_PATH = os.path.join(OUT, NAME + ".kicad_sch")
SYMDIR = os.environ.get("KICAD10_SYMBOL_DIR") or os.path.join(
    os.path.dirname(sys.executable), "..", "share", "kicad", "symbols")

ROOT = sym_uuid("__root_sheet__")
BY_REF = {p["ref"]: p for p in PARTS}


def uid(*k):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "opengong-analog-sch/" + "/".join(map(str, k))))


def snap(v):
    return round(v / 1.27) * 1.27


# ---------------------------------------------------------------- library symbols
_libcache = {}


def lib_symbol(lib_id):
    lib, name = lib_id.split(":")
    if lib not in _libcache:
        with open(os.path.join(SYMDIR, lib + ".kicad_sym"), encoding="utf8") as f:
            _libcache[lib] = parse(f.read())
    for s in find(_libcache[lib], "symbol"):
        if s[1] == name:
            assert not find1(s, "extends"), lib_id
            return s
    raise SystemExit("symbol not found " + lib_id)


def pins_of(lib_id, unit):
    out = {}
    for u in find(lib_symbol(lib_id), "symbol"):
        parts = u[1].rsplit("_", 2)
        un = int(parts[-2])
        if un not in (0, unit):
            continue
        for p in find(u, "pin"):
            at = find1(p, "at")
            out[find1(p, "number")[1]] = (float(at[1]), float(at[2]), float(at[3]))
    return out


def units_of(lib_id):
    us = set()
    for u in find(lib_symbol(lib_id), "symbol"):
        un = int(u[1].rsplit("_", 2)[-2])
        if un:
            us.add(un)
    return sorted(us) or [1]


# ---------------------------------------------------------------- drawing
items = []
used_libs = set()


def eff(size=1.27, hide=False, justify=None):
    e = [Sym("effects"), [Sym("font"), [Sym("size"), size, size]]]
    if justify:
        e.append([Sym("justify")] + [Sym(j) for j in justify.split()])
    if hide:
        e.append([Sym("hide"), Sym("yes")])
    return e


def prop(name, value, x, y, hide=False, angle=0, justify=None):
    return [Sym("property"), name, value, [Sym("at"), x, y, angle], eff(hide=hide, justify=justify)]


def xform(px, py, X, Y, rot):
    a = math.radians(rot)
    xr = px * math.cos(a) - py * math.sin(a)
    yr = px * math.sin(a) + py * math.cos(a)
    return X + xr, Y - yr


def place(ref, X, Y, rot=0, unit=1, ref_at=None, val_at=None, label_pins=True):
    p = BY_REF[ref]
    lib_id = p["sym"]
    used_libs.add(lib_id)
    X, Y = snap(X), snap(Y)
    u_id = sym_uuid(ref) if unit == units_of(lib_id)[0] else uid(ref, "unit", unit)
    rx, ry = ref_at or (X + 2.54, Y - 3.81)
    vx, vy = val_at or (X + 2.54, Y + 3.81)
    kind_mpn = p["mpn"]
    sym = [Sym("symbol"), [Sym("lib_id"), lib_id], [Sym("at"), X, Y, rot], [Sym("unit"), unit],
           [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes" if p["in_bom"] else "no")],
           [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")], [Sym("uuid"), u_id],
           prop("Reference", ref, rx, ry, justify="left"),
           prop("Value", p["value"], vx, vy, justify="left"),
           prop("Footprint", p["fp"], X, Y, hide=True),
           prop("Datasheet", "", X, Y, hide=True),
           prop("Description", p["note"], X, Y, hide=True),
           prop("MPN", kind_mpn, X, Y, hide=True)]
    pins = pins_of(lib_id, unit)
    for num in sorted(pins):
        sym.append([Sym("pin"), num, [Sym("uuid"), uid(ref, unit, "pin", num)]])
    sym.append([Sym("instances"), [Sym("project"), NAME,
                                   [Sym("path"), "/" + ROOT, [Sym("reference"), ref], [Sym("unit"), unit]]]])
    items.append(sym)
    if label_pins:
        for num, (px, py, pa) in pins.items():
            sx, sy = xform(px, py, X, Y, rot)
            net = p["pins"].get(num)
            out_angle = (pa + 180 + rot) % 360
            if net in POWER_NETS:
                power_symbol(net, sx, sy, out_angle)
            elif net:
                label(net, sx, sy, out_angle)
            else:
                items.append([Sym("no_connect"), [Sym("at"), round(sx, 4), round(sy, 4)],
                              [Sym("uuid"), uid(ref, unit, "nc", num)]])


_lbl = [0]
_pwr = [0]
POWER_LIB = {"GND": ("power:GND", 270), "+3V3": ("power:+3V3", 90), "+3V3A": ("power:+3.3VA", 90)}


def power_symbol(net, x, y, out_angle):
    """Power port at a pin end, its graphic pointing away from the part."""
    lib_id, graphic_dir = POWER_LIB[net]
    used_libs.add(lib_id)
    _pwr[0] += 1
    ref = "#PWR%03d" % _pwr[0]
    rot = (out_angle - graphic_dir) % 360
    x, y = round(x, 4), round(y, 4)
    tx, ty = xform(0, 3.2 if graphic_dir == 90 else -3.2, x, y, rot)
    items.append([Sym("symbol"), [Sym("lib_id"), lib_id], [Sym("at"), x, y, rot], [Sym("unit"), 1],
                  [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")],
                  [Sym("dnp"), Sym("no")], [Sym("uuid"), uid("pwr", _pwr[0])],
                  prop("Reference", ref, x, y, hide=True),
                  prop("Value", net, round(tx, 4), round(ty, 4)),
                  prop("Footprint", "", x, y, hide=True), prop("Datasheet", "", x, y, hide=True),
                  prop("Description", "", x, y, hide=True),
                  [Sym("pin"), "1", [Sym("uuid"), uid("pwr", _pwr[0], "pin")]],
                  [Sym("instances"), [Sym("project"), NAME, [Sym("path"), "/" + ROOT, [Sym("reference"), ref],
                                                             [Sym("unit"), 1]]]]])


def label(net, x, y, angle):
    _lbl[0] += 1
    just = {0: "left bottom", 90: "left bottom", 180: "right bottom", 270: "right bottom"}[int(angle) % 360]
    items.append([Sym("label"), net, [Sym("at"), round(x, 4), round(y, 4), int(angle) % 360],
                  [Sym("fields_autoplaced"), Sym("yes")], eff(justify=just),
                  [Sym("uuid"), uid("label", _lbl[0])]])


def text(s, x, y, size=1.27, bold=False):
    e = [Sym("effects"), [Sym("font"), [Sym("size"), size, size]] + ([[Sym("bold"), Sym("yes")]] if bold else []),
         [Sym("justify"), Sym("left"), Sym("bottom")]]
    items.append([Sym("text"), s, [Sym("exclude_from_sim"), Sym("no")], [Sym("at"), x, y, 0], e,
                  [Sym("uuid"), uid("text", s[:40], x, y)]])


def power_flag(net, x, y, idx):
    flagref = "#FLG%02d" % idx
    used_libs.add("power:PWR_FLAG")
    X, Y = snap(x), snap(y)
    items.append([Sym("symbol"), [Sym("lib_id"), "power:PWR_FLAG"], [Sym("at"), X, Y, 0], [Sym("unit"), 1],
                  [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")],
                  [Sym("dnp"), Sym("no")], [Sym("uuid"), uid("flag", net)],
                  prop("Reference", flagref, X, Y - 2.54, hide=True),
                  prop("Value", "PWR_FLAG", X, Y - 3.81),
                  prop("Footprint", "", X, Y, hide=True), prop("Datasheet", "", X, Y, hide=True),
                  prop("Description", "", X, Y, hide=True),
                  [Sym("pin"), "1", [Sym("uuid"), uid("flag", net, "pin")]],
                  [Sym("instances"), [Sym("project"), NAME, [Sym("path"), "/" + ROOT,
                                                             [Sym("reference"), flagref],
                                                             [Sym("unit"), 1]]]]])
    power_symbol(net, X, Y, 270)


# ---------------------------------------------------------------- layout
def build():
    text("OpenGong analog front end, rev " + REV, 20, 18, 3.0, bold=True)
    text("Five piezo channels: clamp -> 48 kHz high-pass on VMID -> x10 amplifier -> "
         "threshold comparator -> Heltec V3 MCPWM capture pin. See docs/Wiring-Diagram.md.", 20, 24, 1.5)
    text("Nets are joined by name: every pin carries a label. OPA2350 and TLV3502 use the generic dual "
         "SOIC-8 symbols (same pinout).",
         20, 28, 1.5)

    y0, dy = 45.0, 30.0
    x = {"J": 25, "R2": 45, "R1": 62, "D": 82, "C1": 102, "R3": 120, "AMP": 145, "R5": 172, "R6": 190,
         "TP": 208, "CMP": 230, "R4": 258}
    for n in CHANNELS:
        y = y0 + (n - 1) * dy
        text(f"Channel {n}  ->  TRIG{n} = GPIO{GPIO[n]}",
             12, y - 10, 1.8, bold=True)
        place(f"J{n}", x["J"], y, 180, ref_at=(x["J"] - 6, y - 5), val_at=(x["J"] - 6, y + 5))
        place(f"R{n}2", x["R2"], y, 0)
        place(f"R{n}1", x["R1"], y, 90, ref_at=(x["R1"] - 3, y - 3.5), val_at=(x["R1"] - 3, y + 4.5))
        place(f"D{n}", x["D"], y, 0, ref_at=(x["D"] - 3, y - 5), val_at=(x["D"] - 3, y + 5))
        place(f"C{n}1", x["C1"], y, 90, ref_at=(x["C1"] - 3, y - 3.5), val_at=(x["C1"] - 3, y + 4.5))
        place(f"R{n}3", x["R3"], y, 0)
        u, h = AMP[n]
        place(u, x["AMP"], y, 0, unit=1 if h == "A" else 2, ref_at=(x["AMP"] + 2, y - 6),
              val_at=(x["AMP"] + 2, y + 6))
        place(f"R{n}5", x["R5"], y, 90, ref_at=(x["R5"] - 3, y - 3.5), val_at=(x["R5"] - 3, y + 4.5))
        place(f"R{n}6", x["R6"], y, 90, ref_at=(x["R6"] - 3, y - 3.5), val_at=(x["R6"] - 3, y + 4.5))
        place(f"TP{n}", x["TP"], y, 0, ref_at=(x["TP"] + 2, y - 4), val_at=(x["TP"] + 2, y - 6.5))
        u, h = CMP[n]
        place(u, x["CMP"], y, 0, unit=1 if h == "A" else 2, ref_at=(x["CMP"] + 2, y - 6),
              val_at=(x["CMP"] + 2, y + 6))
        place(f"R{n}4", x["R4"], y, 90, ref_at=(x["R4"] - 3, y - 3.5), val_at=(x["R4"] - 3, y + 4.5))

    # shared: VMID, VTH, supply, IC power units, test points
    yb = y0 + len(CHANNELS) * dy + 5
    text("Shared bias, threshold and analog supply", 12, yb - 10, 1.8, bold=True)
    place("U3", 40, yb + 2, 0, unit=2, ref_at=(42, yb - 4), val_at=(42, yb + 8))
    text("U3B: VMID buffer", 34, yb + 12, 1.27)
    place("R1", 20, yb - 2, 0)
    place("R2", 20, yb + 10, 0)
    place("C1", 28, yb + 10, 0)
    place("RV1", 70, yb + 2, 0, ref_at=(66, yb - 4), val_at=(66, yb + 7))
    place("R3", 85, yb + 2, 90, ref_at=(82, yb - 1.5), val_at=(82, yb + 6.5))
    place("C2", 98, yb + 6, 0)
    place("U6", 112, yb + 2, 0, unit=2, ref_at=(114, yb - 4), val_at=(114, yb + 8))
    text("U6B: unused, inputs tied off", 104, yb + 12, 1.27)
    place("FB1", 140, yb + 2, 90, ref_at=(137, yb - 1.5), val_at=(137, yb + 6.5))
    place("C3", 156, yb + 6, 0)
    place("C4", 166, yb + 6, 0)
    for k, tp in enumerate(("TP6", "TP7", "TP8", "TP9")):
        place(tp, 215 + k * 12, yb + 2, 0, ref_at=(217 + k * 12, yb - 2), val_at=(217 + k * 12, yb - 4.5))
    yp = yb + 26
    text("IC supply pins and decoupling", 12, yp - 10, 1.8, bold=True)
    for k, u in enumerate(ICS):
        place(u, 25 + k * 30, yp, 0, unit=3, ref_at=(28 + k * 30, yp - 3), val_at=(28 + k * 30, yp + 3))
        place(f"C10{k + 1}", 37 + k * 30, yp, 0)
    for k, net in enumerate(("+3V3", "+3V3A", "GND")):
        power_flag(net, 245 + k * 15, yp - 2, k + 1)

    # Heltec headers
    xh = 300
    text("Heltec WiFi LoRa 32 V3 sockets", xh - 10, 35, 1.8, bold=True)
    for k, (ref, names) in enumerate((("J7", HELTEC_J2), ("J8", HELTEC_J3))):
        X = xh + k * 55
        place(ref, X, 70, 0, ref_at=(X - 2, 45), val_at=(X - 2, 98))
        for i, nm in enumerate(names):
            text(nm, X + 2.5, 70 - 20.32 + i * 2.54 + 0.6, 1.0)
    text("J7 = Heltec header J2,  J8 = Heltec header J3 (pin 1 at the USB-C end)", xh - 10, 104, 1.27)
    text("GPIO48 is the firmware's internal capture-sync pin: leave it unconnected.", xh - 10, 108, 1.27)

    for h in ("H1", "H2", "H3", "H4"):
        place(h, xh + 10 * int(h[1]), 130, 0, ref_at=(xh + 10 * int(h[1]) - 2, 125), val_at=(xh + 10 * int(h[1]) - 2, 136),
              label_pins=False)


def write():
    build()
    libs = []
    for lib_id in sorted(used_libs):
        s = list(lib_symbol(lib_id))
        s[1] = lib_id
        libs.append(s)
    sch = [Sym("kicad_sch"), [Sym("version"), Sym("20250114")], [Sym("generator"), "eeschema"],
           [Sym("generator_version"), "10.0"], [Sym("uuid"), ROOT], [Sym("paper"), "A3"],
           [Sym("title_block"), [Sym("title"), "OpenGong analog front end"], [Sym("date"), ""],
            [Sym("rev"), REV], [Sym("comment"), 1, "Generated by tools/gen_schematic.py from tools/design.py"]],
           [Sym("lib_symbols")] + libs] + items + [
           [Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]],
           [Sym("embedded_fonts"), Sym("no")]]
    with open(SCH_PATH, "w", encoding="utf8", newline="\n") as f:
        f.write(dump(sch) + "\n")
    print("saved", SCH_PATH)


if __name__ == "__main__":
    write()
