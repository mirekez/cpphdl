"""Render actual GDS geometry and package independently checked physical outputs.

Run with the PDK Python and install Pillow/matplotlib in that environment, or
provide their installed location through PYTHONPATH. KLayout is required.
"""
import argparse
from datetime import datetime, timezone
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
import klayout.db as db
import klayout.lay as lay
from PIL import Image, ImageDraw, ImageFont
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon

WORK = ROOT / "build/asic-refresh/images"
DEST = ROOT / "1G/IHPSG13G2/results"
SOURCE = ROOT / "build/asic-refresh"
PDK = ROOT / "pdk/IHPSG13G2"
BACKGROUND = "#101923"
INK = "#eff5fc"
MUTED = "#acc0d1"
ACCENT = "#73e2ce"
FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
LAYERS = {
    "Activ": (1, "#60db7c"), "GatPoly": (5, "#e77773"),
    "Cont": (6, "#d6f3fa"), "Metal1": (8, "#54b9f2"),
    "Via1": (19, "#e6effb"), "Metal2": (10, "#d4cedf"),
    "Via2": (29, "#efbaa0"), "Metal3": (30, "#f57e87"),
    "Via3": (49, "#c5d49a"), "Metal4": (50, "#9bdd63"),
    "Via4": (66, "#e7b57a"), "Metal5": (67, "#e9ce5b"),
    "TopVia1": (125, "#ffe3be"), "TopMetal1": (126, "#ffe3be"),
    "TopVia2": (133, "#f8ad61"), "TopMetal2": (134, "#f8ad61"),
    "EdgeSeal": (39, "#b797f8"), "Passiv": (9, "#698dba"),
}

def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def text(draw, xy, value, size=25, color=INK):
    draw.text(xy, value, fill=color, font=ImageFont.truetype(str(FONT), size))

def render(gds, name, selected, extent, size, fill=False):
    root = ET.Element("layer-properties")
    for layer in selected:
        number, color = LAYERS[layer]
        for datatype in ([22, 0] if fill else [0]):
            prop = ET.SubElement(root, "properties")
            for key, value in {
                "name": layer, "source": f"{number}/{datatype}",
                "frame-color": color, "fill-color": color,
                "visible": "true", "valid": "true", "dither-pattern": "I0",
            }.items():
                ET.SubElement(prop, key).text = value
    lyp = WORK / (name + ".lyp")
    ET.ElementTree(root).write(lyp)
    view = lay.LayoutView()
    view.set_config("background-color", BACKGROUND)
    view.set_config("grid-visible", "false")
    view.set_config("text-visible", "false")
    view.load_layout(str(gds), False)
    view.load_layer_props(str(lyp), 0, False)
    view.max_hier()
    view.zoom_box(db.DBox(*extent))
    path = WORK / (name + ".png")
    view.save_image(str(path), size, size)
    view.destroy()
    return Image.open(path).convert("RGB")

def scale(draw, x, y, pixels, microns):
    draw.line((x, y, x + pixels, y), fill=INK, width=4)
    draw.line((x, y - 5, x, y + 5), fill=INK, width=3)
    draw.line((x + pixels, y - 5, x + pixels, y + 5), fill=INK, width=3)
    text(draw, (x, y + 12), f"{microns} µm", 21, MUTED)

def build_core_image():
    image = Image.new("RGB", (2400, 1860), BACKGROUND)
    draw = ImageDraw.Draw(image)
    text(draw, (60, 35), "1G repeater  /  digital forwarding core", 43)
    text(draw, (60, 98), "IHP SG13G2  ·  RepeaterCore  ·  120 × 120 µm  ·  actual GDSII geometry", 27, ACCENT)
    panels = [
        ("Routing overview", ["Activ", "GatPoly", "Cont", "Metal1", "Metal2", "Metal3", "Metal4", "Metal5"], False),
        ("Devices + Metal1", ["Activ", "GatPoly", "Cont", "Metal1"], False),
        ("Metal2 + Via1", ["Via1", "Metal2"], False),
        ("Metal3 + Via2", ["Via2", "Metal3"], False),
        ("Metal4 / Metal5 + vias", ["Via3", "Metal4", "Via4", "Metal5"], False),
        ("TopMetal1 / TopMetal2 + fill", ["TopVia1", "TopMetal1", "TopVia2", "TopMetal2"], True),
    ]
    for i, (title, layers, fill) in enumerate(panels):
        x, y = 60 + (i % 3) * 780, 170 + (i // 3) * 795
        text(draw, (x, y), f"{i + 1:02}   {title}", 26)
        tile = render(DEST / "repeater_core.gds", f"core-{i}", layers, (-3, -3, 123, 123), 700, fill)
        image.paste(tile, (x, y + 48))
        scale(draw, x + 16, y + 704, 111, 20)
    text(draw, (60, 1780), "Drawing layers shown; filler hidden except panel 06. Colors identify masks, not material appearance.", 24, MUTED)
    text(draw, (60, 1818), "Core block only. Copper PHYs, receive DSP/clock integration and a complete chip layout remain unfinished.", 24, MUTED)
    image.save(DEST / "core_layers.png", optimize=True)

def build_pad_image():
    image = Image.new("RGB", (2300, 1750), BACKGROUND)
    draw = ImageDraw.Draw(image)
    text(draw, (60, 35), "1G repeater  /  standalone padframe prototype", 43)
    text(draw, (60, 98), "IHP SG13G2  ·  RepeaterPadframe  ·  1.28 × 1.28 mm  ·  actual GDSII geometry", 26, ACCENT)
    tile = render(DEST / "repeater_padframe.gds.gz", "padframe", list(LAYERS), (-20, -20, 1300, 1300), 1460)
    image.paste(tile, (45, 170))
    scale(draw, 110, 1570, 221, 200)
    # This annotation describes the real empty opening; it does not insert a block.
    text(draw, (533, 850), "Unpopulated chip interior", 30)
    text(draw, (559, 900), "640 × 640 µm opening", 26, MUTED)
    x = 1570
    lines = [
        ("32 pads + 4 corners", 33, INK),
        ("16 analog MDI pads", 28, MUTED),
        ("4 digital control / status pads", 27, MUTED),
        ("12 supply pads", 28, MUTED),
        ("", 28, MUTED),
        ("1.2 V core / 3.3 V I/O", 31, INK),
        ("Common VSS and I/O ground", 27, MUTED),
        ("32 decoupling cells", 28, MUTED),
        ("PDK seal ring and ESD cells", 27, MUTED),
        ("", 28, MUTED),
        ("Separate from the core", 32, INK),
        ("No PHY or core inserted here", 27, MUTED),
        ("No complete-chip routing", 27, MUTED),
        ("Opening too small for full PHYs", 26, MUTED),
        ("", 28, MUTED),
        ("Layout visualization", 32, INK),
        ("Drawing layers; filler hidden", 27, MUTED),
        ("Metal + via colors match", 27, MUTED),
        ("the core layer views", 27, MUTED),
    ]
    for i, (label, size, color) in enumerate(lines):
        text(draw, (x, 245 + i * 61), label, size, color)
    text(draw, (60, 1685), "Standalone DRC / ideal-tap LVS only. ESD ratings, complete PHY and full-chip signoff are not established.", 24, MUTED)
    image.save(DEST / "padframe.png", optimize=True)

def build_wave_image(rows):
    # CSV records inputs before an edge and registered outputs after that same edge.
    # Use sample indices, not a fabricated continuous-time analog waveform.
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11,
                         "figure.facecolor": BACKGROUND, "axes.facecolor": BACKGROUND,
                         "text.color": INK, "axes.labelcolor": MUTED,
                         "xtick.color": MUTED, "ytick.color": MUTED, "axes.edgecolor": MUTED})
    fig, axes = plt.subplots(2, 1, figsize=(15, 10), gridspec_kw={"height_ratios": [1.2, 1]})
    fig.suptitle("1G repeater / recorded digital forwarding", fontsize=23, x=.08, ha="left", y=.97)
    fig.text(.08, .915, "Fresh C++RTL capture • 125 MHz reference • checked against RTL and routed gates", color=ACCENT, fontsize=13)
    ax = axes[0]
    chosen = rows[:24]
    labels = [("rx_a", "RX A"), ("tx_b", "TX B"), ("rx_b", "RX B"), ("tx_a", "TX A")]
    for j, (key, name) in enumerate(labels):
        y = 3 - j
        color = "#54b9f2" if j < 2 else "#e9ce5b"
        for row in chosen:
            k = row["cycle"]
            value = row[key]
            poly = Polygon([(k+.06, y+.28), (k+.13, y+.54), (k+.87, y+.54),
                            (k+.94, y+.28), (k+.87, y+.02), (k+.13, y+.02)],
                           facecolor=BACKGROUND, edgecolor=color, linewidth=.9)
            ax.add_patch(poly)
            ax.text(k+.5, y+.28, f"{value:03X}", fontsize=8, ha="center", va="center", color=color)
    ax.set(xlim=(0, 24), ylim=(-.3, 4), yticks=[3.28, 2.28, 1.28, .28],
           yticklabels=[x[1] for x in labels], xlabel="Recorded cycle (one sample every 8 ns)")
    ax.set_xticks(range(0, 25, 2))
    ax.set_title("Full-duplex forwarding • packed 10-bit words in hexadecimal", loc="left", pad=18, color=INK)
    ax.grid(axis="x", alpha=.12)
    ax.spines[["top", "right"]].set_visible(False)
    ax = axes[1]
    chosen = rows[-24:]
    tracks = [("link_a", "Link A"), ("link_b", "Link B"), ("enable", "Enable"),
              ("reset", "Reset"), ("active", "Active")]
    for j, (key, label) in enumerate(tracks):
        y = 4 - j
        x = [r["cycle"] for r in chosen]
        values = [y + .6 * r[key] for r in chosen]
        ax.step(x + [x[-1]+1], values + [values[-1]], where="post", color=ACCENT if key=="active" else "#54b9f2", linewidth=1.6)
    ax.set(yticks=[4.3, 3.3, 2.3, 1.3, .3], yticklabels=[x[1] for x in tracks],
           xlabel="Recorded cycle", ylim=(-.3, 5), xlim=(x[0], x[-1]+1))
    ax.set_title("Recorded reset, link and enable controls", loc="left", pad=18, color=INK)
    ax.grid(axis="x", alpha=.12)
    ax.spines[["top", "right"]].set_visible(False)
    fig.subplots_adjust(left=.08, right=.98, top=.85, bottom=.15, hspace=.55)
    fig.text(.08, .07, "Word bits: [7:0] data, [8] valid, [9] error. Each row pairs pre-edge inputs with post-edge registered outputs.", fontsize=11, color=MUTED)
    fig.text(.08, .035, "Step lines connect saved samples; they do not show propagation delay, PAM5 voltages or an analog eye diagram.", fontsize=11, color=MUTED)
    fig.savefig(DEST / "forwarding_waveforms.png", dpi=160)
    plt.close(fig)

def checked_receipt(path, status, fields):
    report = json.loads(path.read_text())
    if report.get("status") != status:
        raise ValueError(f"unexpected verification status: {path}")
    for field in fields:
        hashes = report.get(field)
        if not hashes:
            raise ValueError(f"missing input hashes: {path}: {field}")
        for name, expected in hashes.items():
            source = Path(name)
            if not source.is_file() or sha(source) != expected:
                raise ValueError(f"verification input changed or missing: {source}")
    return report


def build_receive_image(path, evidence):
    with gzip.open(path, "rt") as stream:
        rows = [tuple(map(int, row)) for row in csv.reader(stream)]
    fig, axes = plt.subplots(4, 1, figsize=(15, 12), sharex=True)
    labels = [(9, "Training busy"), (6, "Pairs locked"), (4, "PCS locked"),
              (5, "Quality"), (3, "Byte valid")]
    for case, ax in enumerate(axes):
        selected = [row for row in rows if row[0] == case]
        for index, (column, label) in enumerate(labels):
            # Keep every transition; sampling every N rows could hide a fault.
            points = [selected[0]]
            for before, after in zip(selected, selected[1:]):
                if before[column] != after[column]:
                    points.append(after)
            points.append(selected[-1])
            ax.step([row[1] for row in points],
                    [4 - index + 0.65 * row[column] for row in points],
                    where="post", linewidth=1, label=label)
        ax.set(yticks=[4.3, 3.3, 2.3, 1.3, .3], yticklabels=[x[1] for x in labels],
               ylim=(-.2, 5), xlim=(0, selected[-1][1]))
        ax.set_title(f"Case {case}: remote {'master' if case >= 2 else 'slave'}, "
                     + ("reordered/skewed/noisy pairs" if case % 2 else "straight pairs"),
                     loc="left", fontsize=12, color=INK)
        ax.grid(axis="x", alpha=.15)
    axes[-1].set_xlabel("Simulation clock index (functional trace; no physical delay)")
    fig.suptitle("Receive DSP + PCS / training, acquisition and decoded data", fontsize=20, y=.98)
    fixtures = evidence["functional"]["fixture"]
    frames = sum(case["frames"] for case in fixtures)
    byte_count = sum(case["bytes"] for case in fixtures)
    fig.text(.12, .935, f"C++RTL = generated RTL = IHP gates • {len(fixtures)} cases • "
             f"{frames} frames • {byte_count:,} bytes", color=ACCENT)
    fig.subplots_adjust(left=.12, right=.97, top=.9, bottom=.09, hspace=.5)
    timing = evidence["timing"]
    fig.text(.12, .025, "External supervised training and recovered samples. "
             f"{timing['period_ns']:g} ns timing: {timing['status']} "
             f"({timing['setup_slack_ns']:+.2f} ns slack); receiver is not in the GDS.",
             color=MUTED, fontsize=11)
    fig.savefig(DEST / "receive_dsp_pcs.png", dpi=160)
    plt.close(fig)


def main():
    global WORK, DEST, SOURCE, PDK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=SOURCE)
    parser.add_argument("--destination", type=Path, default=DEST)
    parser.add_argument("--pdk", type=Path, default=PDK)
    parser.add_argument("--receiver-work", type=Path, default=ROOT / "build/1G/tests/hardware")
    args = parser.parse_args()
    SOURCE, DEST, PDK = args.work.resolve(), args.destination.resolve(), args.pdk.resolve()
    WORK = SOURCE / "images"
    core_path = SOURCE / "core/flow.json"
    pad_path = SOURCE / "padframe/verification_ideal/physical_checks.json"
    core = checked_receipt(core_path, "core_checks_pass", ["input_sha256"])
    pad = checked_receipt(pad_path, "physical_checks_pass", ["input_sha256"])
    diagnostic_path = SOURCE / "padframe/verification/physical_checks.json"
    diagnostic = json.loads(diagnostic_path.read_text())
    checked_receipt(diagnostic_path, diagnostic["status"], ["input_sha256"])
    if sha(SOURCE / "core/repeater_core.gds") != core["sha256"]["repeater_core.gds"]:
        raise ValueError("core GDS differs from passing receipt")
    pad_gds = SOURCE / "padframe/repeater_padframe_filled.gds"
    if Path(pad["layout"]).resolve() != pad_gds:
        raise ValueError("padframe receipt belongs to a different GDS")
    padinfo = json.loads((SOURCE / "padframe/padframe.json").read_text())
    for name, expected in padinfo["source_sha256"].items():
        if sha(Path(name)) != expected:
            raise ValueError("padframe assembly input changed: " + name)
    fill = json.loads((SOURCE / "padframe/fill/fill.json").read_text())
    if (sha(SOURCE / "padframe/repeater_padframe.gds") != padinfo["gds_sha256"] or
            fill["input_sha256"] != padinfo["gds_sha256"] or
            fill["output_sha256"] != sha(pad_gds)):
        raise ValueError("padframe assembly/fill provenance does not match")
    # These views and scale bars describe this prototype's dimensions exactly.
    for path, top_name, size in ((SOURCE / "core/repeater_core.gds", "RepeaterCore", 120),
                                 (pad_gds, "RepeaterPadframe", 1280)):
        layout = db.Layout()
        layout.read(str(path))
        top = layout.top_cell()
        if top is None or top.name != top_name or layout.dbu != .001:
            raise ValueError("unexpected top cell or database unit")
        if top.bbox() != db.Box(0, 0, size * 1000, size * 1000):
            raise ValueError("layout dimensions differ from the annotated view")
    receiver = {}
    for name in ("receive_dsp", "receive_dsp_pcs"):
        functional_path = args.receiver_work / name / "checks.json"
        mapped_path = args.receiver_work / (name + "_mapped") / "hardware_checks.json"
        functional = checked_receipt(functional_path, "pass", ["source_sha256", "rtl_sha256"])
        mapped = checked_receipt(mapped_path, "pass", ["source_sha256"])
        timing_path = args.receiver_work / (name + "_mapped") / "cell_timing.json"
        timing = json.loads(timing_path.read_text())
        checked_receipt(timing_path, timing["status"], ["input_sha256"])
        if not mapped["native_mapped_exact"] or not functional["stages"]["rtl"]["exact_native"]:
            raise ValueError("receiver traces were not equal")
        if sha(Path(mapped["work"]) / "mapped.v") != mapped["mapped_sha256"]:
            raise ValueError("mapped receiver netlist changed")
        receiver[name] = dict(functional=functional, mapped=mapped, timing=timing,
                             receipt_sha256={str(p): sha(p) for p in (functional_path, mapped_path, timing_path)})
    functional = receiver["receive_dsp_pcs"]["functional"]
    trace = Path(functional["traces"]["native"]["path"])
    if sha(trace) != functional["traces"]["native"]["archive_sha256"]:
        raise ValueError("receiver trace changed")
    with gzip.open(trace, "rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != functional["traces"]["native"]["raw_sha256"]:
            raise ValueError("receiver raw trace changed")
    WORK.mkdir(parents=True, exist_ok=True)
    DEST.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SOURCE / "core/repeater_core.gds", DEST / "repeater_core.gds")
    with pad_gds.open("rb") as original, (DEST / "repeater_padframe.gds.gz").open("wb") as target:
        with gzip.GzipFile(filename="", mode="wb", fileobj=target, mtime=0) as compressed:
            shutil.copyfileobj(original, compressed)
    shutil.copyfile(PDK / "LICENSE", DEST / "IHP-LICENSE.txt")
    with (SOURCE / "core/native_vectors.csv").open() as stream:
        rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
    print("Rendering actual core and padframe masks", flush=True)
    build_core_image()
    build_pad_image()
    build_wave_image(rows)
    build_receive_image(trace, receiver["receive_dsp_pcs"])
    provenance = dict(
        package="1G repeater physical development artifacts",
        generated_utc=datetime.now(timezone.utc).isoformat(),
        source_base_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        source_worktree_included=True,
        pdk_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=PDK, text=True).strip(),
        release_status="incomplete_asic", full_1000base_t_chip=False, fabrication_release=False,
        new_simulation_or_physical_verification_run=True,
        core=dict(flow=core, receipt_sha256=sha(core_path),
                  drc=json.loads((SOURCE / "core/drc-check.log").read_text()),
                  lvs=json.loads((SOURCE / "core/lvs-check.log").read_text())),
        padframe=dict(verification=pad, assembly=padinfo, fill=fill,
                      receipt_sha256=sha(pad_path), integrated_with_core=False,
                      default_tap_verification=diagnostic,
                      default_tap_receipt_sha256=sha(diagnostic_path)),
        receiver=receiver,
        waveform=dict(rows=len(rows), source_sha256=sha(SOURCE / "core/native_vectors.csv")),
        visualization=dict(renderer_sha256=sha(Path(__file__)), palette=LAYERS,
                           geometry="actual KLayout drawing layers; top-metal fill shown in core panel 6",
                           waveform="functional samples only; no analog voltage or propagation delay"))
    provenance["artifacts"] = {p.name: dict(bytes=p.stat().st_size, sha256=sha(p))
                               for p in sorted(DEST.iterdir()) if p.is_file() and p.name not in ("provenance.json", "README.md")}
    (DEST / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print("Packaged GDSII, four images, input hashes and verification receipts", flush=True)


if __name__ == "__main__":
    main()
