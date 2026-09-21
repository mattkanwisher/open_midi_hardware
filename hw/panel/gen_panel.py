#!/usr/bin/env python3
"""Generate the controller front panel boards as KiCad 8 PCB files.

Everything mechanical about the panel lives in the tables at the top of this
file. Edit a number, run the script, and every board plus the preview is
regenerated. Nothing is hand-drawn in KiCad, so measured connector dimensions
replace the placeholder ones here, not in a .kicad_pcb.

Outputs (all in this directory):
  frame.kicad_pcb           hidden FR4 skeleton the segments screw to
  seg_<name>.kicad_pcb      one visible panel segment each (aluminium-core PCB)
  card_template.kicad_pcb   starting point for a cassette card (PCB parallel to the plate)
  backplane/backplane.kicad_pcb  the rear board, in its own project with the schematic
  preview.svg               the example assembly, drawn from the same tables

Coordinates follow KiCad: millimetres, x to the right, y DOWN. The panel's
origin is its top-left corner as seen from the front.
"""
import math
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ---------------------------------------------------------------- the grid

PITCH = 25.0          # one slot, mm. Every segment is an integer number of slots
SLOTS = 12            # slots in the example frame; the frame is parametric
HEIGHT = 88.0         # panel height (2U is 88.9; this rounds down)
RAIL = 8.0            # top and bottom rail height; screws live in it
SCREW_D = 3.2         # M3 clearance
SCREW_INSET = 5.0     # screw x from a segment's edge; y is RAIL/2 from top/bottom
SEG_GAP = 0.3         # total gap between neighbouring segments
ROWS = [17.0, 35.0, 53.0, 71.0]   # socket row centres, 18 mm pitch
STILE = 5.0           # frame material right of the opening
CORNER_R = 3.0

# --------------------------------------------------- connector face cutouts
#
# Each entry is the hole in the panel that the *plug* passes through, plus any
# mounting holes. `measured` False means the numbers are from memory and are
# placeholders: replace them after measuring the socket you actually bought
# (see README.md, "the connector-geometry gate"). Width x height in mm.

CUTOUTS = {
    # Standard D-sub size E (9-pin) panel cutout: 19.3 wide side, 10 deg
    # sides, 11.0 high, jack-screw holes 24.99 apart. Check against the
    # datasheet of the DE-9 you order (Amphenol L717 / TE Amplimite class).
    "de9":    dict(kind="dsub", w=19.3, h=11.0, taper_deg=10, hole_d=3.1,
                   hole_pitch=24.99, r=1.0, measured=False),
    "nes":    dict(kind="rrect", w=17.0, h=11.0, r=2.0, measured=False),
    "snes":   dict(kind="rrect", w=31.0, h=12.0, r=5.5, measured=False),
    "saturn": dict(kind="rrect", w=31.5, h=12.5, r=3.0, measured=False),
    "psx":    dict(kind="rrect", w=37.5, h=11.5, r=2.0, measured=False),
    "n64":    dict(kind="rrect", w=18.5, h=14.5, r=5.0, measured=False),
    "usba":   dict(kind="rrect", w=14.0, h=7.0, r=0.8, measured=False),
    "usb3a":  dict(kind="rrect", w=14.0, h=7.0, r=0.8, measured=False),
    "btn16":  dict(kind="circle", d=16.2, measured=True),   # 16 mm anti-vandal
    # 2.42" 128x64 OLED: active area 55.0 x 27.8; window with 1.5 mm margin.
    # Module mounting holes not modelled: measured=False until one is here.
    "oled242": dict(kind="rrect", w=58.0, h=31.0, r=1.0, measured=False),
}

# ------------------------------------------------------------ the segments
#
# A segment is `slots` wide. Features are (cutout name, x, y) in the segment's
# own frame (origin top-left). Labels are (text, x, y, size, justify).
# Bracket lines are (x1, y1, x2, y2) on silkscreen, the photo's grouping cue.


def _w(slots):
    return slots * PITCH - SEG_GAP


def seg_buttons():
    w = _w(2)
    bx = 33.0
    ys = [14.0, 34.0, 54.0, 74.0]
    feats = [("btn16", bx, y) for y in ys]
    labels = [(t, 6.0, y + 1.0, 2.2, "left") for t, y in zip(["PWR", "RST", "USR", "OSD"], ys)]
    lines = [(20.0, y, 23.0, y) for y in ys]
    return dict(name="buttons", slots=2, w=w, feats=feats, labels=labels, lines=lines)


def seg_blank(n):
    return dict(name=f"blank{n}", slots=n, w=_w(n), feats=[], labels=[], lines=[])


def seg_genesis_nes():
    w = _w(2); cx = w / 2
    feats = [("de9", cx, ROWS[0]), ("de9", cx, ROWS[1]),
             ("nes", cx, ROWS[2]), ("nes", cx, ROWS[3])]
    labels = [("Genesis", cx, 5.5, 2.2, "center"), ("NES", cx, 85.0, 2.2, "center")]
    lines = _group_lines(cx, 16, top=True) + _group_lines(cx, 10, top=False)
    return dict(name="genesis_nes", slots=2, w=w, feats=feats, labels=labels, lines=lines)


def seg_saturn_snes():
    w = _w(2); cx = w / 2
    feats = [("saturn", cx, ROWS[0]), ("saturn", cx, ROWS[1]),
             ("snes", cx, ROWS[2]), ("snes", cx, ROWS[3])]
    labels = [("Saturn", cx, 5.5, 2.2, "center"), ("SNES", cx, 85.0, 2.2, "center")]
    lines = _group_lines(cx, 16, top=True) + _group_lines(cx, 16, top=False)
    return dict(name="saturn_snes", slots=2, w=w, feats=feats, labels=labels, lines=lines)


def seg_psx_usb():
    w = _w(2); cx = w / 2
    feats = [("psx", cx, ROWS[0]), ("psx", cx, ROWS[1]),
             ("usba", cx - 10.0, ROWS[2]), ("usba", cx + 10.0, ROWS[2]),
             ("usba", cx - 10.0, ROWS[3]), ("usba", cx + 10.0, ROWS[3])]
    labels = [("PlayStation", cx, 5.5, 2.2, "center"), ("USB", cx, 85.0, 2.2, "center")]
    lines = _group_lines(cx, 19, top=True) + _group_lines(cx, 14, top=False)
    return dict(name="psx_usb", slots=2, w=w, feats=feats, labels=labels, lines=lines)


def seg_n64x4():
    w = _w(1); cx = w / 2
    feats = [("n64", cx, y) for y in ROWS]
    labels = [("N64", cx, 5.5, 2.2, "center")]
    lines = _group_lines(cx, 9.5, top=True)
    return dict(name="n64x4", slots=1, w=w, feats=feats, labels=labels, lines=lines)


def seg_snac4():
    """Four MiSTer-user-port-compatible USB3-A sockets, one per slot IO set."""
    w = _w(1); cx = w / 2
    feats = [("usb3a", cx, y) for y in ROWS]
    labels = [("SNAC", cx, 5.5, 2.2, "center")]
    lines = _group_lines(cx, 7.5, top=True)
    return dict(name="snac4", slots=1, w=w, feats=feats, labels=labels, lines=lines)


def seg_display():
    w = _w(3); cx = w / 2
    feats = [("oled242", cx, 26.0)]
    labels = []
    lines = []
    return dict(name="display", slots=3, w=w, feats=feats, labels=labels, lines=lines)


def _group_lines(cx, half, top):
    """The photo's bracket: a horizontal line with two short ticks, under a label."""
    y = 8.0 if top else 82.0
    t = 2.0 if top else -2.0
    return [(cx - half, y, cx + half, y), (cx - half, y, cx - half, y + t), (cx + half, y, cx + half, y + t)]


SEGMENTS = [seg_buttons(), seg_blank(1), seg_blank(2), seg_genesis_nes(),
            seg_saturn_snes(), seg_psx_usb(), seg_n64x4(), seg_snac4(), seg_display()]

# The example assembly: segment names left to right. Must sum to SLOTS.
ASSEMBLY = ["buttons", "genesis_nes", "saturn_snes", "psx_usb", "n64x4", "display"]

# --------------------------------------------------------- cassette and backplane
#
# A cassette is a segment, the rails it screws through, four M3 standoffs and a
# card parallel to the plate. The card carries the sockets on its front face
# (straight-mount directly, right-angle ones on a horizontal shelf sub-board)
# and its electronics on its back face, plus a 2x5 box header for the ribbon to
# the backplane. The standoff length is the depth of the deepest socket on that
# card, so every cassette's plug faces end up at the back of the plate.
DEPTH = 80.0            # front plate to backplane front face
CARD_MARGIN = 1.0       # card edge inside the segment edge, each side
CARD_Y0 = 1.0           # card top edge in panel coordinates; it spans the full height
CARD_H = HEIGHT - 2 * CARD_Y0
CARD_ROWS = [y - CARD_Y0 for y in ROWS]
CARD_TEMPLATE_SLOTS = 2
RIBBON = dict(cols=5, rows=2, pitch=2.54, shroud=(15.2, 8.9))   # 2x5 box header, IDC ribbon
BP_USB_Y = 28.0         # backplane: USB3-A socket row centre (SNAC port)
BP_IDC_Y = 52.0         # backplane: 2x5 box header row centre (same port, ribbon)
BP_SNAC_SLOTS = range(2, SLOTS)   # slots that get a SNAC port; 0 and 1 are the buttons
BP_HUB_HEADERS = 4      # 1x4 USB headers for USB sections (hub downstream ports)


# ============================================================ KiCad writer

_NS = uuid.UUID("6f1c2a4e-7b0d-4b1e-9c3a-0123456789ab")
_counter = [0]


def U():
    """Deterministic UUIDs so regenerating the boards yields a stable, reviewable diff."""
    _counter[0] += 1
    return f'(uuid "{uuid.uuid5(_NS, str(_counter[0]))}")'


def f(v):
    return f"{v:.3f}".rstrip("0").rstrip(".") if abs(v) > 1e-9 else "0"


class Board:
    def __init__(self, title):
        self.title = title
        self.items = []

    def line(self, x1, y1, x2, y2, layer="Edge.Cuts", w=0.1):
        self.items.append(f'  (gr_line (start {f(x1)} {f(y1)}) (end {f(x2)} {f(y2)}) '
                          f'(stroke (width {w}) (type default)) (layer "{layer}") {U()})')

    def arc(self, x1, y1, xm, ym, x2, y2, layer="Edge.Cuts", w=0.1):
        self.items.append(f'  (gr_arc (start {f(x1)} {f(y1)}) (mid {f(xm)} {f(ym)}) (end {f(x2)} {f(y2)}) '
                          f'(stroke (width {w}) (type default)) (layer "{layer}") {U()})')

    def circle(self, cx, cy, d, layer="Edge.Cuts", w=0.1):
        self.items.append(f'  (gr_circle (center {f(cx)} {f(cy)}) (end {f(cx + d / 2)} {f(cy)}) '
                          f'(stroke (width {w}) (type default)) (fill none) (layer "{layer}") {U()})')

    def poly(self, pts, layer="Edge.Cuts", w=0.1):
        p = " ".join(f"(xy {f(x)} {f(y)})" for x, y in pts)
        self.items.append(f'  (gr_poly (pts {p}) (stroke (width {w}) (type default)) (fill none) (layer "{layer}") {U()})')

    def text(self, s, x, y, size=2.0, layer="F.SilkS", justify="center", thickness=None):
        th = thickness or round(size * 0.15, 3)
        j = "" if justify == "center" else f" (justify {justify})"
        s = s.replace('"', "'")
        self.items.append(f'  (gr_text "{s}" (at {f(x)} {f(y)} 0) (layer "{layer}") {U()} '
                          f'(effects (font (size {size} {size}) (thickness {th})){j}))')

    def rrect(self, x, y, w, h, r, layer="Edge.Cuts"):
        """Rounded rectangle, top-left (x, y), as 4 lines + 4 arcs (KiCad has no radius on gr_rect)."""
        if r <= 0:
            self.items.append(f'  (gr_rect (start {f(x)} {f(y)}) (end {f(x + w)} {f(y + h)}) '
                              f'(stroke (width 0.1) (type default)) (fill none) (layer "{layer}") {U()})')
            return
        x2, y2 = x + w, y + h
        c = r * (1 - math.sqrt(0.5))
        self.line(x + r, y, x2 - r, y, layer)
        self.arc(x2 - r, y, x2 - c, y + c, x2, y + r, layer)
        self.line(x2, y + r, x2, y2 - r, layer)
        self.arc(x2, y2 - r, x2 - c, y2 - c, x2 - r, y2, layer)
        self.line(x2 - r, y2, x + r, y2, layer)
        self.arc(x + r, y2, x + c, y2 - c, x, y2 - r, layer)
        self.line(x, y2 - r, x, y + r, layer)
        self.arc(x, y + r, x + c, y + c, x + r, y, layer)

    def _fp(self, name, ref, value, x, y, rot, layer, body, pads, ref_at=(0, -6)):
        self.items.append(
            f'  (footprint "panel:{name}" (layer "{layer}") {U()} (at {f(x)} {f(y)} {rot})\n'
            f'    (property "Reference" "{ref}" (at {f(ref_at[0])} {f(ref_at[1])} 0) (layer "{"B" if layer == "B.Cu" else "F"}.SilkS") {U()} '
            f'(effects (font (size 1 1) (thickness 0.15))))\n'
            f'    (property "Value" "{value}" (at 0 3 0) (layer "{"B" if layer == "B.Cu" else "F"}.Fab") {U()} '
            f'(effects (font (size 1 1) (thickness 0.15))))\n'
            f'    (attr through_hole)\n' + "\n".join(body) + "\n" + "\n".join(pads) + "\n  )")

    def footprint_box_header(self, ref, value, x, y, cols=5, rows=2, pitch=2.54, shroud=(15.2, 8.9),
                             layer="F.Cu", rot=0):
        """Straight shrouded pin header, pin 1 at the footprint origin, columns +x, rows +y.
        Shroud outline centred on the pin field; key notch on the pin-1 side."""
        pads = []
        for c in range(cols):
            for r in range(rows):
                n = c * rows + r + 1
                shape = "rect" if n == 1 else "oval"
                pads.append(f'    (pad "{n}" thru_hole {shape} (at {f(c * pitch)} {f(r * pitch)}) (size 1.7 1.7) '
                            f'(drill 1.0) (layers "*.Cu" "*.Mask") (remove_unused_layers no) {U()})')
        cx, cy = (cols - 1) * pitch / 2, (rows - 1) * pitch / 2
        sw, sh = shroud
        fab = "B.Fab" if layer == "B.Cu" else "F.Fab"
        body = [f'    (fp_rect (start {f(cx - sw / 2)} {f(cy - sh / 2)}) (end {f(cx + sw / 2)} {f(cy + sh / 2)}) '
                f'(stroke (width 0.1) (type default)) (fill none) (layer "{fab}") {U()})',
                f'    (fp_rect (start {f(cx - 2)} {f(cy - sh / 2 - 0.4)}) (end {f(cx + 2)} {f(cy - sh / 2)}) '
                f'(stroke (width 0.1) (type default)) (fill none) (layer "{fab}") {U()})']
        self._fp(f"BoxHeader_{cols}x{rows:02d}_P{pitch}mm", ref, value, x, y, rot, layer, body, pads,
                 ref_at=(cx, cy - sh / 2 - 1.5))

    def footprint_pin_header(self, ref, value, x, y, n=4, layer="F.Cu", rot=0):
        pads = [f'    (pad "{i + 1}" thru_hole {"rect" if i == 0 else "oval"} (at {f(i * 2.54)} 0) (size 1.7 1.7) '
                f'(drill 1.0) (layers "*.Cu" "*.Mask") (remove_unused_layers no) {U()})' for i in range(n)]
        body = [f'    (fp_rect (start -1.27 -1.27) (end {f((n - 1) * 2.54 + 1.27)} 1.27) '
                f'(stroke (width 0.1) (type default)) (fill none) (layer "F.Fab") {U()})']
        self._fp(f"PinHeader_1x{n:02d}_P2.54mm", ref, value, x, y, rot, layer, body, pads, ref_at=((n - 1) * 1.27, -2.5))

    def footprint_usb3a_placeholder(self, ref, x, y):
        """USB 3.0 A right-angle receptacle, PLACEHOLDER: nine signal pads in two rows and
        a body outline of roughly the right size. Replace with the vendor footprint before
        routing; only the position and the pin names are meant to survive."""
        pads = []
        names = ["VBUS", "D-", "D+", "GND", "SSRX-", "SSRX+", "GND_DRAIN", "SSTX-", "SSTX+"]
        for i, xx in enumerate((-3.75, -1.25, 1.25, 3.75)):
            pads.append(f'    (pad "{i + 1}" thru_hole {"rect" if i == 0 else "circle"} (at {f(xx)} 0) (size 1.6 1.6) '
                        f'(drill 0.95) (layers "*.Cu" "*.Mask") (remove_unused_layers no) {U()})')
        for i, xx in enumerate((-4.0, -2.0, 0.0, 2.0, 4.0)):
            pads.append(f'    (pad "{i + 5}" thru_hole circle (at {f(xx)} 2.6) (size 1.6 1.6) '
                        f'(drill 0.95) (layers "*.Cu" "*.Mask") (remove_unused_layers no) {U()})')
        for xx in (-6.6, 6.6):
            pads.append(f'    (pad "S" thru_hole oval (at {f(xx)} 1.3) (size 2.2 3.0) (drill oval 1.2 2.0) '
                        f'(layers "*.Cu" "*.Mask") (remove_unused_layers no) {U()})')
        body = [f'    (fp_rect (start -7.3 -13.5) (end 7.3 4.0) (stroke (width 0.1) (type default)) (fill none) (layer "F.Fab") {U()})',
                f'    (fp_text user "USB3-A placeholder" (at 0 -6 0) (layer "F.Fab") {U()} (effects (font (size 0.9 0.9) (thickness 0.15))))']
        self._fp("USB3_A_Receptacle_Horizontal_PLACEHOLDER", ref, "SNAC", x, y, 0, "F.Cu", body, pads, ref_at=(0, 6))

    def write(self, path):
        layers = [
            (0, "F.Cu", "signal"), (31, "B.Cu", "signal"),
            (32, "B.Adhes", "user", "B.Adhesive"), (33, "F.Adhes", "user", "F.Adhesive"),
            (34, "B.Paste", "user"), (35, "F.Paste", "user"),
            (36, "B.SilkS", "user", "B.Silkscreen"), (37, "F.SilkS", "user", "F.Silkscreen"),
            (38, "B.Mask", "user"), (39, "F.Mask", "user"),
            (40, "Dwgs.User", "user", "User.Drawings"), (41, "Cmts.User", "user", "User.Comments"),
            (42, "Eco1.User", "user", "User.Eco1"), (43, "Eco2.User", "user", "User.Eco2"),
            (44, "Edge.Cuts", "user"), (45, "Margin", "user"),
            (46, "B.CrtYd", "user", "B.Courtyard"), (47, "F.CrtYd", "user", "F.Courtyard"),
            (48, "B.Fab", "user"), (49, "F.Fab", "user"),
        ]
        ls = "\n".join(f'    ({n} "{name}" {kind}' + (f' "{extra[0]}"' if extra else "") + ")"
                       for n, name, kind, *extra in layers)
        out = (
            '(kicad_pcb\n'
            '  (version 20240108)\n'
            '  (generator "gen_panel")\n'
            '  (generator_version "8.0")\n'
            '  (general (thickness 1.6) (legacy_teardrops no))\n'
            '  (paper "A3")\n'
            f'  (title_block (title "{self.title}") (comment 1 "generated by hw/panel/gen_panel.py - do not edit by hand"))\n'
            f'  (layers\n{ls}\n  )\n'
            '  (setup (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints no))\n'
            '  (net 0 "")\n'
            + "\n".join(self.items) + "\n)\n"
        )
        Path(path).write_text(out)


# =========================================================== geometry

def cutout_shapes(kind_name, cx, cy):
    """Yield ('rrect', x, y, w, h, r) / ('circle', cx, cy, d) / ('poly', pts) for a cutout."""
    c = CUTOUTS[kind_name]
    k = c["kind"]
    if k == "circle":
        yield ("circle", cx, cy, c["d"])
    elif k == "rrect":
        yield ("rrect", cx - c["w"] / 2, cy - c["h"] / 2, c["w"], c["h"], c["r"])
    elif k == "dsub":
        w, h, r = c["w"], c["h"], c["r"]
        dx = h * math.tan(math.radians(c["taper_deg"]))
        # wide side up; a D with 10 deg sides, corners approximated by short chamfers
        top_l, top_r = cx - w / 2, cx + w / 2
        bot_l, bot_r = cx - w / 2 + dx, cx + w / 2 - dx
        yt, yb = cy - h / 2, cy + h / 2
        pts = [(top_l + r, yt), (top_r - r, yt), (top_r, yt + r), (bot_r + r * 0.3, yb - r),
               (bot_r - r * 0.6, yb), (bot_l + r * 0.6, yb), (bot_l - r * 0.3, yb - r), (top_l, yt + r)]
        yield ("poly", pts)
        yield ("circle", cx - c["hole_pitch"] / 2, cy, c["hole_d"])
        yield ("circle", cx + c["hole_pitch"] / 2, cy, c["hole_d"])


def emit_shape(b, s, layer="Edge.Cuts"):
    if s[0] == "circle":
        b.circle(s[1], s[2], s[3], layer)
    elif s[0] == "rrect":
        b.rrect(s[1], s[2], s[3], s[4], s[5], layer)
    elif s[0] == "poly":
        b.poly(s[1], layer)


def segment_holes(w):
    return [(SCREW_INSET, RAIL / 2), (w - SCREW_INSET, RAIL / 2),
            (SCREW_INSET, HEIGHT - RAIL / 2), (w - SCREW_INSET, HEIGHT - RAIL / 2)]


def frame_holes():
    pts = []
    for k in range(SLOTS):
        for xi in (SCREW_INSET, PITCH - SCREW_INSET):
            for y in (RAIL / 2, HEIGHT - RAIL / 2):
                pts.append((k * PITCH + xi, y))
    return pts


def frame_opening():
    """The strip behind the module slots. Everything except the button segment."""
    x0 = 2 * PITCH
    return (x0, RAIL, SLOTS * PITCH - STILE, HEIGHT - RAIL)


def check_geometry(seg):
    """Cutouts must stay inside the segment and clear of the rails' screw holes."""
    w = seg["w"]
    for name, cx, cy in seg["feats"]:
        for s in cutout_shapes(name, cx, cy):
            if s[0] == "circle":
                x1, y1, x2, y2 = s[1] - s[3] / 2, s[2] - s[3] / 2, s[1] + s[3] / 2, s[2] + s[3] / 2
            elif s[0] == "rrect":
                x1, y1, x2, y2 = s[1], s[2], s[1] + s[3], s[2] + s[4]
            else:
                xs = [p[0] for p in s[1]]; ys = [p[1] for p in s[1]]
                x1, y1, x2, y2 = min(xs), min(ys), max(xs), max(ys)
            assert x1 >= 1.0 and x2 <= w - 1.0 and y1 >= 1.0 and y2 <= HEIGHT - 1.0, \
                f"{seg['name']}: {name} at ({cx},{cy}) leaves the segment"
            for hx, hy in segment_holes(w):
                if x1 - 1 <= hx <= x2 + 1 and y1 - 1 <= hy <= y2 + 1:
                    raise AssertionError(f"{seg['name']}: {name} at ({cx},{cy}) collides with screw ({hx},{hy})")
    # silkscreen must stay 1 mm clear of the screw heads (M3 button head, 5.5 mm)
    for x1, y1, x2, y2 in seg["lines"]:
        for hx, hy in segment_holes(w):
            if min(x1, x2) - 3.75 <= hx <= max(x1, x2) + 3.75 and min(y1, y2) - 3.75 <= hy <= max(y1, y2) + 3.75:
                raise AssertionError(f"{seg['name']}: silk line ({x1},{y1})-({x2},{y2}) under screw head ({hx},{hy})")


# ============================================================ boards

def build_segment(seg):
    b = Board(f"panel segment {seg['name']} ({seg['slots']} slot)")
    w = seg["w"]
    b.rrect(0, 0, w, HEIGHT, 1.0)
    for hx, hy in segment_holes(w):
        b.circle(hx, hy, SCREW_D)
    unmeasured = set()
    for name, cx, cy in seg["feats"]:
        for s in cutout_shapes(name, cx, cy):
            emit_shape(b, s)
        if not CUTOUTS[name]["measured"]:
            unmeasured.add(name)
    for t, x, y, size, j in seg["labels"]:
        b.text(t, x, y, size, justify=j)
    for x1, y1, x2, y2 in seg["lines"]:
        b.line(x1, y1, x2, y2, "F.SilkS", 0.2)
    for i, y in enumerate(ROWS):
        b.text(f"row {i + 1}", -6, y, 1.0, "Cmts.User")
        b.line(-3, y, 0, y, "Cmts.User", 0.1)
    if unmeasured:
        b.text("PLACEHOLDER CUTOUTS, NOT MEASURED: " + ", ".join(sorted(unmeasured)),
               w / 2, HEIGHT + 4, 1.5, "Cmts.User")
    b.text(f"seg_{seg['name']}  {f(w)} x {f(HEIGHT)} mm  {seg['slots']} x {f(PITCH)} mm slots  M3 x4",
           w / 2, -3, 1.5, "Cmts.User")
    return b


def build_frame():
    W = SLOTS * PITCH
    b = Board(f"panel frame, {SLOTS} slots, {f(W)} x {f(HEIGHT)} mm")
    b.rrect(0, 0, W, HEIGHT, CORNER_R)
    for hx, hy in frame_holes():
        b.circle(hx, hy, SCREW_D)
    x1, y1, x2, y2 = frame_opening()
    b.rrect(x1, y1, x2 - x1, y2 - y1, 2.0)
    # the button segment's holes go through the frame too
    for name, cx, cy in seg_buttons()["feats"]:
        for s in cutout_shapes(name, cx, cy):
            emit_shape(b, s)
    for k in range(SLOTS):
        b.line(k * PITCH, 0, k * PITCH, -4, "Cmts.User", 0.1)
        b.text(f"slot {k}", k * PITCH + PITCH / 2, -2, 1.2, "Cmts.User")
    b.text(f"frame  {f(W)} x {f(HEIGHT)} mm, {SLOTS} slots of {f(PITCH)} mm, FR4 1.6, hidden behind segments",
           W / 2, -7, 1.5, "Cmts.User")
    b.text("segments screw through this frame into the tray rails; the frame only locates them",
           W / 2, HEIGHT + 4, 1.5, "Cmts.User")
    return b


def card_holes(w):
    """Standoff holes: the segment's screw pattern, shifted by the card margin."""
    return [(SCREW_INSET - CARD_MARGIN, RAIL / 2 - CARD_Y0), (w - SCREW_INSET + CARD_MARGIN, RAIL / 2 - CARD_Y0),
            (SCREW_INSET - CARD_MARGIN, HEIGHT - RAIL / 2 - CARD_Y0), (w - SCREW_INSET + CARD_MARGIN, HEIGHT - RAIL / 2 - CARD_Y0)]


def build_card_template():
    n = CARD_TEMPLATE_SLOTS
    w = n * PITCH - 2 * CARD_MARGIN
    b = Board(f"cassette card template ({n} slot, parallel to the plate, viewed from the front)")
    # x: along the panel, card's left edge at x=0 (= segment left edge + CARD_MARGIN)
    # y: down, top edge y=0 is CARD_Y0 below the panel's top edge; same height as the panel less margins
    b.rrect(0, 0, w, CARD_H, 1.0)
    for hx, hy in card_holes(w):
        b.circle(hx, hy, SCREW_D)
    for i, y in enumerate(CARD_ROWS):
        b.line(0, y, w, y, "Cmts.User", 0.1)
        b.text(f"row {i + 1} centre (panel y {f(ROWS[i])})", 2, y - 1.2, 1.0, "Cmts.User", justify="left")
    for k in range(1, n):
        x = k * PITCH - CARD_MARGIN
        b.line(x, 0, x, CARD_H, "Cmts.User", 0.1)
        b.text("slot boundary", x, -1.5, 0.9, "Cmts.User")
    # ribbon header on the BACK face, top centre, between the standoffs
    hx = w / 2 - (RIBBON["cols"] - 1) * RIBBON["pitch"] / 2
    b.footprint_box_header("J1", "RIBBON", hx, 3.0, layer="B.Cu", **RIBBON)
    b.text("front face: straight sockets here; right-angle sockets on a horizontal shelf sub-board", w / 2, -4, 1.2, "Cmts.User")
    b.text("corner holes: M3 standoffs to the rails, length = deepest socket on this card", w / 2, CARD_H + 3, 1.2, "Cmts.User")
    b.text("J1 on the back face: 2x5 box header, ribbon to the backplane; pin 1 = +5V (README)", w / 2, 11.5, 1.0, "Cmts.User")
    return b


def build_backplane():
    W = SLOTS * PITCH
    b = Board(f"backplane, {SLOTS} slots, {f(W)} x {f(HEIGHT)} mm, {f(DEPTH)} mm behind the plate, viewed from the front")
    b.rrect(0, 0, W, HEIGHT, CORNER_R)
    for hx, hy in frame_holes():
        b.circle(hx, hy, SCREW_D)
    for k in BP_SNAC_SLOTS:
        cx = k * PITCH + PITCH / 2
        port = k - BP_SNAC_SLOTS.start + 1
        b.footprint_usb3a_placeholder(f"J{port}", cx, BP_USB_Y)
        hx = cx - (RIBBON["cols"] - 1) * RIBBON["pitch"] / 2
        b.footprint_box_header(f"J{port + 20}", f"SNAC{port}", hx, BP_IDC_Y - RIBBON["pitch"] / 2, **RIBBON)
        b.text(f"SNAC {port}", cx, 12, 1.5)
        b.line(k * PITCH, RAIL, k * PITCH, HEIGHT - RAIL, "Cmts.User", 0.1)
    # slot 0-1 zone: buttons and display harness
    b.footprint_box_header("J30", "HARNESS", 12.0, 24.0, cols=8, rows=2, shroud=(22.8, 8.9))
    b.text("buttons + OLED harness to the host", PITCH, 14, 1.2)
    # hub headers for USB sections, along the bottom strip
    for i in range(BP_HUB_HEADERS):
        b.footprint_pin_header(f"J{40 + i}", f"USB{i + 1}", 60.0 + i * 16.0, HEIGHT - 12.0, n=4)
    b.text("hub downstream ports for USB sections (VBUS D- D+ GND)", 60.0, HEIGHT - 16.5, 1.0, justify="left")
    b.text("MCU / hub / power zone: RP2040 per four SNAC ports, USB hub, 5 V in, host USB out on the back face",
           W - 4, HEIGHT - 14, 1.0, "Cmts.User", justify="right")
    b.text(f"backplane  {f(W)} x {f(HEIGHT)} mm, front face toward the cassettes, {f(DEPTH)} mm behind the plate; "
           f"same M3 pattern as the frame, into the rear rails", W / 2, -5, 1.5, "Cmts.User")
    b.text("USB3-A footprints are placeholders: replace with the vendor part before routing", W / 2, HEIGHT + 4, 1.5, "Cmts.User")
    return b


# ============================================================ preview SVG

def preview_svg(path):
    S = 4.0
    W = SLOTS * PITCH
    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W * S + 40}" height="{HEIGHT * S + 60}" '
             f'viewBox="-20 -20 {W * S + 40} {HEIGHT * S + 60}">')
    o.append('<style>.fr{fill:#3a3a3a;stroke:#777;stroke-width:1}.sg{fill:#1c1c1e;stroke:#8d8d8d;stroke-width:1.2}'
             '.cut{fill:#050505;stroke:#9a9a9a;stroke-width:1.2}.hole{fill:#2b2b2b;stroke:#aaa;stroke-width:1}'
             '.lbl{fill:#ececec;font:12px monospace}.silk{stroke:#ececec;stroke-width:1}.dim{fill:#9aa;font:10px monospace}</style>')
    # frame behind
    o.append(f'<rect class="fr" x="0" y="0" width="{W * S}" height="{HEIGHT * S}" rx="{CORNER_R * S}"/>')
    x = 0.0
    by_name = {s["name"]: s for s in SEGMENTS}
    for name in ASSEMBLY:
        seg = by_name[name]
        w = seg["w"]
        ox = x + SEG_GAP / 2
        o.append(f'<rect class="sg" x="{ox * S:.1f}" y="0" width="{w * S:.1f}" height="{HEIGHT * S}" rx="{1 * S}"/>')
        for hx, hy in segment_holes(w):
            o.append(f'<circle class="hole" cx="{(ox + hx) * S:.1f}" cy="{hy * S:.1f}" r="{SCREW_D / 2 * S:.1f}"/>')
        for fname, cx, cy in seg["feats"]:
            for s in cutout_shapes(fname, cx, cy):
                if s[0] == "circle":
                    o.append(f'<circle class="cut" cx="{(ox + s[1]) * S:.1f}" cy="{s[2] * S:.1f}" r="{s[3] / 2 * S:.1f}"/>')
                elif s[0] == "rrect":
                    o.append(f'<rect class="cut" x="{(ox + s[1]) * S:.1f}" y="{s[2] * S:.1f}" width="{s[3] * S:.1f}" '
                             f'height="{s[4] * S:.1f}" rx="{s[5] * S:.1f}"/>')
                else:
                    pts = " ".join(f"{(ox + px) * S:.1f},{py * S:.1f}" for px, py in s[1])
                    o.append(f'<polygon class="cut" points="{pts}"/>')
        for t, lx, ly, size, j in seg["labels"]:
            anchor = {"left": "start", "center": "middle", "right": "end"}[j]
            o.append(f'<text class="lbl" x="{(ox + lx) * S:.1f}" y="{ly * S:.1f}" text-anchor="{anchor}">{t}</text>')
        for x1, y1, x2, y2 in seg["lines"]:
            o.append(f'<line class="silk" x1="{(ox + x1) * S:.1f}" y1="{y1 * S:.1f}" x2="{(ox + x2) * S:.1f}" y2="{y2 * S:.1f}"/>')
        x += w + SEG_GAP
    o.append(f'<text class="dim" x="0" y="{HEIGHT * S + 16}">{f(W)} x {f(HEIGHT)} mm, {SLOTS} slots of {f(PITCH)} mm. '
             f'Cutouts marked in gen_panel.py as measured=False are placeholders.</text>')
    o.append('</svg>')
    Path(path).write_text("\n".join(o) + "\n")


# ============================================================ main

def main():
    assert sum(next(s for s in SEGMENTS if s["name"] == n)["slots"] for n in ASSEMBLY) == SLOTS, \
        "ASSEMBLY does not fill the frame"
    for seg in SEGMENTS:
        check_geometry(seg)
        build_segment(seg).write(HERE / f"seg_{seg['name']}.kicad_pcb")
    build_frame().write(HERE / "frame.kicad_pcb")
    build_card_template().write(HERE / "card_template.kicad_pcb")
    (HERE / "backplane").mkdir(exist_ok=True)
    build_backplane().write(HERE / "backplane" / "backplane.kicad_pcb")
    preview_svg(HERE / "preview.svg")
    pro = HERE / "panel.kicad_pro"
    if not pro.exists():
        pro.write_text('{\n  "meta": { "filename": "panel.kicad_pro", "version": 1 },\n'
                       '  "pcbnew": { "page_layout_descr_file": "" }\n}\n')
    print(f"wrote frame, {len(SEGMENTS)} segments, card template, backplane, preview.svg")


if __name__ == "__main__":
    main()
