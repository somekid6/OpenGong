"""OpenGong analog front-end board: parts and netlist (single source of truth).

Both gen_schematic.py and gen_board.py read this file. The circuit is the one in
docs/Wiring-Diagram.md; reference designators follow the board numbering
scheme described in hardware/analog-board/README.md:

  Channel n (1-5):  Jn sensor terminal, Dn clamp diode,
                    Rn1 series 10k, Rn2 bias 1M, Rn3 high-pass 3.3k,
                    Rn4 output 100R, Rn5 Rf 9.1k, Rn6 Rg 1k, Cn1 high-pass 1n,
                    TPn amplifier-output test point.
  Shared:           R1/R2 VMID divider, C1 VMID filter, RV1 + R3 + C2 threshold,
                    FB1 + C3 + C4 analog supply, C101-C106 decoupling for U1-U6,
                    TP6-TP9 VMID / VTH / +3V3A / GND test points.
"""

FP = {
    "R": "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
    "C": "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
    "FB": "Inductor_SMD:L_0805_2012Metric_Pad1.05x1.20mm_HandSolder",
    "SOIC8": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "SOT23": "Package_TO_SOT_SMD:SOT-23_Handsoldering",
    "TRIM": "Potentiometer_THT:Potentiometer_Bourns_3296W_Vertical",
    "TERM": "TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-2-3.5-H_1x02_P3.50mm_Horizontal",
    "SOCK18": "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical",
    "TP": "TestPoint:TestPoint_THTPad_D1.5mm_Drill0.7mm",
    "TPLOOP": "TestPoint:TestPoint_Loop_D2.50mm_Drill1.0mm",
    "HOLE": "MountingHole:MountingHole_3.2mm_M3",
}

SYM = {
    "R": "Device:R",
    "C": "Device:C",
    "FB": "Device:FerriteBead",
    "OPAMP": "Amplifier_Operational:LM2904",   # generic dual op-amp, OPA2350 pinout
    "CMP": "Comparator:LM2903",                # generic dual comparator, TLV3502 pinout
    "BAV99": "Diode:BAV99",
    "TRIM": "Device:R_Potentiometer_Trim",
    "TERM": "Connector:Screw_Terminal_01x02",
    "CONN18": "Connector_Generic:Conn_01x18",
    "TP": "Connector:TestPoint",
    "HOLE": "Mechanical:MountingHole",
}

# Suggested manufacturer part numbers (any equivalent works).
MPN = {
    "OPA2350": "TI OPA2350UA (SOIC-8)",
    "TLV3502": "TI TLV3502AID (SOIC-8)",
    "BAV99": "Nexperia BAV99,215 (SOT-23)",
    "FB": "Murata BLM21PG221SN1D (0805, 220R@100MHz)",
    "TRIM": "Bourns 3296W-1-103LF (10k, 25-turn)",
    "TERM": "Phoenix Contact 1984617 (PT 1,5/2-3,5-H)",
    "SOCK18": "1x18 female header, 2.54 mm, 8.5 mm tall (e.g. Sullins PPTC181LFBN-RC)",
}

PARTS = []      # dicts: ref, kind, value, fp, sym, pins{pad: net}, note
NC = None       # marker for an unconnected pin


def part(ref, kind, value, pins, fp=None, sym=None, note=""):
    PARTS.append(dict(ref=ref, kind=kind, value=value, pins=pins,
                      fp=FP[fp or kind], sym=SYM[sym or kind], note=note))


# Channel -> (op-amp ref, half) and (comparator ref, half), per Wiring-Diagram.md section 5.
CHANNELS = range(1, 6)
AMP = {1: ("U1", "A"), 2: ("U1", "B"), 3: ("U2", "A"), 4: ("U2", "B"), 5: ("U3", "A")}
CMP = {1: ("U4", "A"), 2: ("U4", "B"), 3: ("U5", "A"), 4: ("U5", "B"), 5: ("U6", "A")}
HALF = {"A": {"out": "1", "inv": "2", "non": "3"}, "B": {"out": "7", "inv": "6", "non": "5"}}

# Heltec header pin carrying each channel's trigger (Heltec J3 = board J8, Heltec J2 = board J7).
TRIG_PIN = {1: ("J8", "13"), 2: ("J8", "15"), 3: ("J8", "16"), 4: ("J8", "17"), 5: ("J8", "18")}
GPIO = {1: 2, 2: 4, 3: 5, 4: 6, 5: 7}

ICS = ("U1", "U2", "U3", "U4", "U5", "U6")
ic_pins = {u: {"4": "GND", "8": "+3V3A"} for u in ICS}

for n in CHANNELS:
    sig, clamp, hp, fb, amp, cmp_, trig = (f"SIG{n}", f"CLAMP{n}", f"HP{n}", f"INV{n}",
                                           f"AMP{n}", f"CMP{n}", f"TRIG{n}")
    part(f"J{n}", "TERM", "Screw_Terminal_2P_3.5mm", {"1": sig, "2": "GND"},
         note=f"Piezo PZ{n}: pin 1 = ceramic/centre (signal), pin 2 = shield/brass (GND)")
    part(f"R{n}2", "R", "1M", {"1": sig, "2": "GND"})
    part(f"R{n}1", "R", "10k", {"1": sig, "2": clamp})
    part(f"D{n}", "BAV99", "BAV99", {"1": "GND", "2": "+3V3A", "3": clamp}, fp="SOT23",
         note="Pad 1 anode D1 = GND, pad 2 cathode D2 = +3V3A, pad 3 common")
    part(f"C{n}1", "C", "1n", {"1": clamp, "2": hp}, note="C0G/NP0")
    part(f"R{n}3", "R", "3.3k", {"1": hp, "2": "VMID"})
    part(f"R{n}5", "R", "9.1k", {"1": amp, "2": fb}, note="Rf; 4.7k for gain 5.7")
    part(f"R{n}6", "R", "1k", {"1": fb, "2": "VMID"}, note="Rg")
    part(f"R{n}4", "R", "100", {"1": cmp_, "2": trig})
    part(f"TP{n}", "TP", f"AMP{n}", {"1": amp})
    u, h = AMP[n]
    ic_pins[u].update({HALF[h]["non"]: hp, HALF[h]["inv"]: fb, HALF[h]["out"]: amp})
    u, h = CMP[n]
    ic_pins[u].update({HALF[h]["non"]: amp, HALF[h]["inv"]: "VTH", HALF[h]["out"]: cmp_})

# U3B: VMID buffer (voltage follower on the divider).
# U6B: unused comparator half, inputs tied off (+ to GND, - to +3V3A) so its output stays low.
ic_pins["U3"].update({"5": "VDIV", "6": "VMID", "7": "VMID"})
ic_pins["U6"].update({"5": "GND", "6": "+3V3A", "7": NC})

for u in ("U1", "U2", "U3"):
    part(u, "SOIC8", "OPA2350", ic_pins[u], sym="OPAMP",
         note={"U3": "A = channel 5, B = VMID buffer"}.get(u, ""))
for u in ("U4", "U5", "U6"):
    part(u, "SOIC8", "TLV3502", ic_pins[u], sym="CMP",
         note={"U6": "A = channel 5, B unused (inputs tied off)"}.get(u, ""))

for i, u in enumerate(ICS):
    part(f"C10{i + 1}", "C", "100n", {"1": "+3V3A", "2": "GND"}, note=f"Decouples {u}")

# Shared bias, threshold and supply (Wiring-Diagram.md section 3).
part("R1", "R", "10k", {"1": "+3V3A", "2": "VDIV"}, note="VMID divider top")
part("R2", "R", "10k", {"1": "VDIV", "2": "GND"}, note="VMID divider bottom")
part("C1", "C", "1u", {"1": "VDIV", "2": "GND"}, note="VMID filter")
part("RV1", "TRIM", "10k", {"1": "+3V3A", "2": "VTH_SET", "3": "GND"}, note="Threshold adjust")
part("R3", "R", "10k", {"1": "VTH_SET", "2": "VTH"})
part("C2", "C", "1u", {"1": "VTH", "2": "GND"}, note="VTH filter")
part("FB1", "FB", "220R@100MHz", {"1": "+3V3", "2": "+3V3A"})
part("C3", "C", "10u", {"1": "+3V3A", "2": "GND"}, note="X5R/X7R, 10 V or more")
part("C4", "C", "100n", {"1": "+3V3A", "2": "GND"})

part("TP6", "TP", "VMID", {"1": "VMID"})
part("TP7", "TP", "VTH", {"1": "VTH"})
part("TP8", "TP", "+3V3A", {"1": "+3V3A"})
part("TP9", "TP", "GND", {"1": "GND"}, fp="TPLOOP")

# Heltec WiFi LoRa 32 V3 sockets. J7 = Heltec header J2, J8 = Heltec header J3.
j7 = {str(i): NC for i in range(1, 19)}
j7.update({"1": "GND"})
j8 = {str(i): NC for i in range(1, 19)}
j8.update({"1": "GND", "2": "+3V3", "3": "+3V3", "13": "TRIG1", "15": "TRIG2", "16": "TRIG3",
           "17": "TRIG4", "18": "TRIG5"})
part("J7", "SOCK18", "Heltec_J2", j7, sym="CONN18", note="Heltec V3 header J2 (GND only; holds the module)")
part("J8", "SOCK18", "Heltec_J3", j8, sym="CONN18", note="Heltec V3 header J3 (3V3, GPIO2-7...)")

HELTEC_J2 = ["GND", "5V", "Ve", "Ve", "GPIO44/RX", "GPIO43/TX", "RST", "GPIO0", "GPIO36",
             "GPIO35", "GPIO34", "GPIO33", "GPIO47", "GPIO48", "GPIO26", "GPIO21", "GPIO20", "GPIO19"]
HELTEC_J3 = ["GND", "3V3", "3V3", "GPIO37", "GPIO46", "GPIO45", "GPIO42", "GPIO41", "GPIO40",
             "GPIO39", "GPIO38", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7"]

for i in range(1, 5):
    part(f"H{i}", "HOLE", "M3", {}, note="Mounting hole")

POWER_NETS = {"GND", "+3V3", "+3V3A"}

# Per-part BOM fields shared by the schematic, board and BOM.
for _p in PARTS:
    _p["mpn"] = MPN.get(_p["value"]) or MPN.get(_p["kind"]) or MPN.get(
        {"BAV99": "BAV99"}.get(_p["value"], ""), "")
    _p["in_bom"] = _p["kind"] not in ("HOLE", "TP", "TPLOOP")
    if not _p["mpn"] and _p["kind"] == "R":
        _p["mpn"] = "0805 thick film, 1 %"
    if not _p["mpn"] and _p["kind"] == "C":
        _p["mpn"] = {"1n": "0805 C0G/NP0, 50 V", "10u": "0805 X5R/X7R, 10 V or more"}.get(
            _p["value"], "0805 X7R, 16 V or more")


def board_net(name):
    """Net name as KiCad derives it: power symbols are global, labels are sheet-local."""
    return name if name in POWER_NETS else "/" + name


def nets():
    out = {}
    for p in PARTS:
        for pad, net in p["pins"].items():
            if net:
                out.setdefault(net, []).append((p["ref"], pad))
    return out


if __name__ == "__main__":
    for net, pins in sorted(nets().items()):
        print(f"{net:10s} {len(pins):3d}  {' '.join(r + '.' + p for r, p in pins)}")
    print(len(PARTS), "parts")
