"""Rebuild the OpenGong analog board and its fabrication outputs.

    "<KiCad 10>/bin/python.exe" -I tools/build.py            # full rebuild (re-routes the board)
    "<KiCad 10>/bin/python.exe" -I tools/build.py --export   # outputs only, from the current files

Steps: schematic + board generation, DRC (zone refill, schematic parity) and ERC,
both of which must have zero errors, then gerbers + drill (zipped for upload),
BOM, schematic and assembly PDFs and 3D renders.
"""
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT = os.path.dirname(HERE)
NAME = "opengong-analog"
CLI = os.path.join(os.path.dirname(sys.executable), "kicad-cli.exe" if os.name == "nt" else "kicad-cli")
PCB = os.path.join(OUT, NAME + ".kicad_pcb")
SCH = os.path.join(OUT, NAME + ".kicad_sch")
FAB = os.path.join(OUT, "fab")
DOCS = os.path.join(OUT, "docs")
GERBER_LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"


def cli(*args):
    print(">", "kicad-cli", " ".join(args))
    r = subprocess.run([CLI, *args], capture_output=True, text=True)
    sys.stdout.write(r.stdout[-2000:])
    if r.returncode:
        sys.stdout.write(r.stderr[-2000:])
        raise SystemExit(f"kicad-cli failed ({r.returncode}): {' '.join(args)}")


def main():
    if "--export" not in sys.argv:
        import gen_board
        import gen_schematic
        gen_schematic.write()
        gen_board.build()

    os.makedirs(FAB, exist_ok=True)
    os.makedirs(DOCS, exist_ok=True)
    tmp = os.path.join(FAB, "_reports")
    os.makedirs(tmp, exist_ok=True)

    # Zone refill is saved into the board; any error-severity violation stops the build.
    cli("pcb", "drc", "--refill-zones", "--save-board", "--schematic-parity", "--severity-error",
        "--exit-code-violations", "-o", os.path.join(tmp, "drc.rpt"), PCB)
    cli("sch", "erc", "--severity-error", "--exit-code-violations", "-o", os.path.join(tmp, "erc.rpt"), SCH)

    gdir = os.path.join(FAB, "_gerbers")
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(gdir)
    cli("pcb", "export", "gerbers", "--layers", GERBER_LAYERS, "--subtract-soldermask", "-o", gdir + os.sep, PCB)
    cli("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
        "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2", "-o", gdir + os.sep, PCB)
    zpath = os.path.join(FAB, NAME + "-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    print("wrote", zpath, sorted(os.listdir(gdir)))
    shutil.rmtree(gdir)
    shutil.rmtree(tmp)

    cli("sch", "export", "bom", "-o", os.path.join(FAB, NAME + "-bom.csv"),
        "--fields", "Reference,Value,Footprint,${QUANTITY},MPN",
        "--labels", "Refs,Value,Footprint,Qty,Suggested part",
        "--group-by", "Value,Footprint", "--sort-field", "Value", SCH)
    cli("sch", "export", "pdf", "-o", os.path.join(DOCS, NAME + "-schematic.pdf"), SCH)
    cli("pcb", "export", "pdf", "--layers", "F.Fab,F.Silkscreen,Edge.Cuts", "--mode-single", "--sp",
        "--ibt", "-o", os.path.join(DOCS, NAME + "-assembly.pdf"), PCB)
    for side in ("top", "bottom"):
        cli("pcb", "render", "--side", side, "--width", "1600", "--height", "1600", "--quality", "high",
            "-o", os.path.join(DOCS, f"{NAME}-{side}.png"), PCB)


if __name__ == "__main__":
    main()
