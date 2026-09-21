#!/usr/bin/env python3
"""Generate docs/port-atlas.html: every controller port a retro panel might
need, drawn to one scale, with the 80 mm cassette depth check.

All dimensions are from memory and marked so in the page. Correct them here,
rerun, and the page and every drawing follow.
"""
import html
from pathlib import Path

OUT = Path(__file__).resolve().parent / "port-atlas.html"
S = 5.0  # px per mm in the face drawings

# ------------------------------------------------------------ the cassette budget (mm)
BUDGET = [
    ("front plate: segment + frame", 3.2),
    ("deepest socket body behind the plate", 25.0),
    ("card and parts on its back", 8.0),
    ("2x5 IDC header and plug", 12.0),
    ("ribbon bend to the backplane", 15.0),
]
BUDGET_PIGTAIL = [
    ("front plate: segment + frame", 3.2),
    ("deepest socket body behind the plate", 25.0),
    ("card and parts on its back", 8.0),
    ("side-facing USB3-A socket on the card", 8.0),
    ("USB3-A plug standing off the backplane", 20.0),
    ("pigtail bend", 15.0),
]
DEPTH = 80.0

# ------------------------------------------------------------ connectors
# face: (w, h) of the opening the plug passes through, mm; body: socket width
# behind the plate, mm; depth: socket body behind the plate, mm. All estimates.
# lines: signal lines the host drives or reads, excluding power. snac: whether a
# passive 7-line SNAC section can carry it.

CONNECTORS = [
    dict(id="de9", name="DE-9, D-sub 9", draw="dsub", pins=9, rows=(5, 4), gender="male on the machine",
         face=(17.0, 8.5), flange=(30.8, 12.5), body=31, depth=13, slots=2, level="5 V",
         lines="up to 7 (Genesis uses all 7)", snac="yes", source="catalogue part, right-angle or straight, with jack posts",
         systems="Atari 2600 / 7800 / 8-bit, Commodore 64 / 128 / Amiga / CD32, MSX, Amstrad CPC, Master System, Mega Drive / Genesis, Atari ST, Sharp X68000, FM Towns, Apple II (analogue), ZX Spectrum +2/+3 (Sinclair pinout)",
         note="One shell, several pinouts. Atari, Sega and Commodore agree on U/D/L/R/GND/+5; Sinclair, Coleco and Apple do not. A section decides, the socket does not. Apple II is analogue: not SNAC."),
    dict(id="da15", name="DA-15, D-sub 15", draw="dsub", pins=15, rows=(8, 7), gender="female on the machine",
         face=(25.5, 8.5), flange=(39.1, 12.5), body=39, depth=13, slots=2, level="5 V",
         lines="9 to 13", snac="no: too many lines, or analogue", source="catalogue part",
         systems="Neo Geo AES / CD, Atari Jaguar, Atari 5200, Atari STE enhanced ports, Famicom expansion, BBC Micro analogue, IBM PC game port",
         note="Neo Geo and Jaguar are digital but need more than seven lines; the PC game port is analogue and also carries MIDI on pins 12 and 15. All of them want a USB section with an MCU, not a passive one."),
    dict(id="nes", name="NES 7-pin", draw="row", pins=7, groups=(7,), shape="dee", gender="female on the console",
         face=(15.5, 9.5), body=22, depth=22, slots=1, level="5 V",
         lines="5 (CLK, LATCH, D0, D3, D4)", snac="yes", source="console replacement part, several variants",
         systems="NES",
         note="Shift register. D3 and D4 carry the Zapper and expansion pads."),
    dict(id="snes", name="SNES / SFC 7-pin", draw="row", pins=7, groups=(4, 3), shape="stadium", gender="female on the console",
         face=(30.0, 10.0), body=32, depth=22, slots=2, level="5 V",
         lines="4 (CLK, LATCH, DATA, IOBIT)", snac="yes", source="console replacement part",
         systems="Super NES, Super Famicom",
         note="Same protocol as NES with 16 bits; the 4+3 split is the key."),
    dict(id="saturn", name="Saturn 9-pin", draw="row", pins=9, groups=(9,), shape="rrect", gender="female on the console",
         face=(30.0, 11.0), body=33, depth=20, slots=2, level="5 V",
         lines="6 (D0..D3, S0, S1)", snac="yes", source="console replacement part, the hardest of the set to find",
         systems="Sega Saturn",
         note="Two select lines pick one of four nibbles. SNAC adapters for it exist."),
    dict(id="psx", name="PlayStation 9-pin", draw="row", pins=9, groups=(3, 3, 3), shape="rrect", gender="female on the console",
         face=(34.0, 9.0), body=38, depth=22, slots=2, level="3.3 V logic, 7.6 V motor",
         lines="5 (DATA, CMD, ATT, CLK, ACK)", snac="yes, with 3.3 V and 7.6 V made on the section", source="console replacement part, single and dual",
         systems="PlayStation, PlayStation 2",
         note="SPI-like at 250 kHz. The section needs a 3.3 V regulator and, for rumble, a 7.6 V boost; a SNAC port gives it 5 V only."),
    dict(id="n64", name="N64 3-pin", draw="row", pins=3, groups=(3,), shape="arch", gender="female on the console",
         face=(16.0, 13.0), body=22, depth=17, slots=1, level="3.3 V",
         lines="1 (Joybus)", snac="yes, four ports on one section", source="console replacement part",
         systems="Nintendo 64",
         note="One open-drain wire per pad, 4 µs per bit. A single seven-line section carries four ports with lines to spare."),
    dict(id="gc", name="GameCube", draw="gc", gender="female on the console",
         face=(14.0, 12.0), body=20, depth=25, slots=1, level="3.3 V data, 5 V rumble",
         lines="1 (Joybus)", snac="yes", source="console replacement part",
         systems="GameCube, early Wii",
         note="Same bus as N64 at the same timing; the extra pins are 5 V for the rumble motor and a shield."),
    dict(id="dc", name="Dreamcast", draw="row", pins=5, groups=(5,), shape="dcurve", gender="female on the console",
         face=(30.0, 9.0), body=34, depth=25, slots=2, level="5 V",
         lines="2 (SDCKA, SDCKB) + sense", snac="yes, timing permitting", source="console replacement part",
         systems="Dreamcast",
         note="Maple bus: two lines clock each other at up to 2 Mbit/s. Fine over 10 cm; the MCU end wants PIO, not bit-banging."),
    dict(id="xbox", name="Xbox (original)", draw="row", pins=5, groups=(5,), shape="trap", gender="female on the console",
         face=(18.0, 11.0), body=25, depth=25, slots=1, level="USB 1.1 at 5 V",
         lines="USB pair + video sync", snac="no: it is USB", source="console replacement part; or a breakaway cable's socket",
         systems="Xbox",
         note="Electrically a USB port with a fifth wire for light guns. Belongs on the hub through a plain USB section, never on a SNAC port."),
    dict(id="mdin8", name="mini-DIN 8", draw="din", pins=8, layout=((6, 7, 8), (3, 4, 5), (1, 2)), diam=9.5, gender="female on the machine",
         face=(9.5, 9.5), body=13, depth=20, slots=1, level="5 V",
         lines="PC Engine 6; CD-i serial 2", snac="yes", source="catalogue part",
         systems="PC Engine / CoreGrafx / SuperGrafx, Philips CD-i, Commodore Plus/4 joystick",
         note="Three different protocols in one shell. Two fit side by side in one slot only with a slim body; plan one per row."),
    dict(id="tg16", name="TurboGrafx-16 8-pin", draw="din", pins=8, layout=((1, 2, 3), (4, 5), (6, 7, 8)), diam=12.5, gender="female on the console",
         face=(12.5, 12.5), body=16, depth=22, slots=1, level="5 V",
         lines="6", snac="yes", source="replacement part, uncommon",
         systems="TurboGrafx-16, TurboDuo",
         note="Same signals as the PC Engine in a larger, incompatible round shell. Shell drawing approximate; pin arrangement unverified."),
    dict(id="mdin6", name="mini-DIN 6, PS/2", draw="din", pins=6, layout=((5, 6), (3, 4), (1, 2)), diam=9.5, gender="female on the machine",
         face=(9.5, 9.5), body=13, depth=20, slots=1, level="5 V",
         lines="2 (CLK, DATA)", snac="yes, but pointless", source="catalogue part, single and stacked",
         systems="IBM PS/2 and every PC until USB; keyboard and mouse",
         note="For a panel that fronts a PC or MiSTer it is a keyboard port, which a USB section with an MCU converts; MiSTer also takes PS/2 through USB adapters."),
    dict(id="mdin4", name="mini-DIN 4, ADB", draw="din", pins=4, layout=((3, 4), (1, 2)), diam=9.5, gender="female on the machine",
         face=(9.5, 9.5), body=13, depth=20, slots=1, level="5 V",
         lines="1 (ADB)", snac="yes, one line", source="catalogue part (same shell as S-Video)",
         systems="Apple IIGS, Macintosh SE through beige G3, Pippin, NeXT",
         note="Apple Desktop Bus, one open-drain wire with a keyboard power line. ADB-to-USB converters on small MCUs exist."),
    dict(id="din5", name="DIN 5, 180°", draw="din", pins=5, layout=((3,), (4, 5), (1, 2)), diam=13.2, gender="female on the machine",
         face=(13.2, 13.2), body=17, depth=22, slots=1, level="5 V",
         lines="2 (CLK, DATA)", snac="yes, but pointless", source="catalogue part",
         systems="IBM PC/XT/AT keyboard, MSX keyboard on some models",
         note="AT keyboard protocol is PS/2's; same conversion. Pin arrangement drawn schematically."),
    dict(id="usba", name="USB-A", draw="usb", variant="a", gender="receptacle on the machine",
         face=(13.0, 6.0), body=14, depth=15, slots=1, level="5 V",
         lines="USB", snac="no: hub port", source="catalogue part, single, stacked, right-angle or vertical",
         systems="Xbox 360, PS3, PS4, Xbox One, Switch dock, PS5, Xbox Series, every PC since 1998",
         note="A USB section is two of these wired to the backplane's hub header. This is also what most retro-to-USB adapters end in."),
    dict(id="usb3a", name="USB 3.0 A, SNAC", draw="usb", variant="3a", gender="receptacle on the backplane",
         face=(13.0, 6.0), body=14, depth=18, slots=1, level="3.3 V open-drain over SNAC; 5 V power",
         lines="7 IO in SNAC order", snac="it is the SNAC port", source="catalogue part",
         systems="MiSTer user port, blue212 SNAC and every SNAC console adapter",
         note="Nine contacts: the four USB 2.0 ones plus five deeper SuperSpeed ones. SNAC uses seven of them as GPIO and VBUS/GND for power. Never plug a real USB device into a SNAC port."),
    dict(id="usbc", name="USB-C", draw="usb", variant="c", gender="receptacle on the machine",
         face=(9.0, 3.5), body=10, depth=9, slots=1, level="5 V",
         lines="USB", snac="no: hub port", source="catalogue part",
         systems="Switch, Switch 2, PS5 and Xbox Series pads, modern PCs",
         note="Only worth a front position for charging a pad; for input, a USB-A on the hub does the same."),
]

# systems whose port could not be verified enough to draw
UNDRAWN = [
    ("3DO (1993)", "one proprietary port on the console; pads daisy-chain. Shape and pin count not verified here."),
    ("Virtual Boy (1995)", "one proprietary port. Not verified."),
    ("ColecoVision (1982)", "DE-9 shell with a keypad matrix on the pins: more than seven lines, USB section."),
    ("Intellivision (1979)", "hard-wired pads on the original; not a port."),
]

# ------------------------------------------------------------ systems, chronological
# (year, name, maker, class, port id or text, ports, section kind)
# section kind: "passive" = a SNAC-style seven-line section can carry it,
# "usb" = needs a USB section with its own MCU or is USB already, "none" = wireless only
SYSTEMS = [
    (1977, "Atari 2600", "Atari", "console", "de9", 2, "passive", "digital stick; paddles are analogue"),
    (1979, "Atari 400 / 800", "Atari", "computer", "de9", 4, "passive", ""),
    (1982, "Commodore 64", "Commodore", "computer", "de9", 2, "passive", "C128 the same"),
    (1982, "ColecoVision", "Coleco", "console", "de9 (Coleco pinout)", 2, "usb", "keypad matrix"),
    (1982, "Vectrex", "GCE", "console", "de9", 2, "passive", "analogue stick on the original pad"),
    (1982, "Atari 5200", "Atari", "console", "da15", 4, "usb", "analogue stick, keypad"),
    (1983, "MSX", "many", "computer", "de9", 2, "passive", "Atari pinout"),
    (1983, "Famicom", "Nintendo", "console", "da15 (expansion, female)", 1, "passive", "pads hard-wired; port carries pad 3 and 4 signals"),
    (1984, "Amstrad CPC", "Amstrad", "computer", "de9", 1, "passive", "second stick chains through the first"),
    (1984, "IBM PC/AT and clones", "IBM and clones", "computer", "da15 game port; din5 keyboard", 1, "usb", "analogue; MIDI on the same port"),
    (1985, "NES", "Nintendo", "console", "nes", 2, "passive", ""),
    (1985, "Master System", "Sega", "console", "de9", 2, "passive", ""),
    (1985, "Amiga 1000 / 500 / 1200", "Commodore", "computer", "de9", 2, "passive", "mouse is quadrature on the same port"),
    (1985, "Atari ST", "Atari", "computer", "de9", 2, "passive", "STE adds two da15 enhanced ports (usb)"),
    (1986, "Atari 7800", "Atari", "console", "de9", 2, "passive", "two buttons on pins 5 and 9"),
    (1986, "ZX Spectrum +2 / +3", "Amstrad", "computer", "de9 (Sinclair pinout)", 2, "passive", "not Atari-compatible wiring"),
    (1986, "BBC Master", "Acorn", "computer", "da15 analogue", 1, "usb", "analogue"),
    (1986, "Apple IIGS", "Apple", "computer", "de9 (analogue); mdin4 ADB", 1, "usb", "analogue stick; ADB keyboard and mouse"),
    (1987, "Macintosh SE / II", "Apple", "computer", "mdin4", 2, "usb", "ADB until 1998"),
    (1987, "IBM PS/2", "IBM", "computer", "mdin6", 2, "usb", "keyboard and mouse"),
    (1987, "PC Engine", "NEC", "console", "mdin8", 1, "passive", "multitap for more"),
    (1987, "Sharp X68000", "Sharp", "computer", "de9", 2, "passive", ""),
    (1988, "Mega Drive / Genesis", "Sega", "console", "de9", 2, "passive", "seven lines exactly"),
    (1989, "TurboGrafx-16", "NEC", "console", "tg16", 1, "passive", ""),
    (1989, "FM Towns", "Fujitsu", "computer", "de9", 2, "passive", ""),
    (1990, "Neo Geo AES", "SNK", "console", "da15", 2, "usb", "more than seven lines; Neo Geo CD the same"),
    (1990, "Super Famicom / SNES", "Nintendo", "console", "snes", 2, "passive", ""),
    (1991, "CD-i", "Philips", "console", "mdin8", 1, "passive", "serial pointer protocol"),
    (1992, "Sega CD", "Sega", "console", "de9", 2, "passive", "uses the Genesis ports"),
    (1993, "3DO", "Panasonic and others", "console", "proprietary", 1, "usb", "not verified"),
    (1993, "Atari Jaguar", "Atari", "console", "da15", 2, "usb", "keypad matrix"),
    (1993, "Amiga CD32", "Commodore", "console", "de9", 2, "passive", "CD32 pad shifts extra buttons on the same pins"),
    (1994, "Saturn", "Sega", "console", "saturn", 2, "passive", ""),
    (1994, "PlayStation", "Sony", "console", "psx", 2, "passive", "3.3 V and 7.6 V on the section"),
    (1994, "32X", "Sega", "console", "de9", 2, "passive", "uses the Genesis ports"),
    (1995, "Virtual Boy", "Nintendo", "console", "proprietary", 1, "usb", "not verified"),
    (1996, "Nintendo 64", "Nintendo", "console", "n64", 4, "passive", "four ports on one section"),
    (1998, "Dreamcast", "Sega", "console", "dc", 4, "passive", "Maple bus"),
    (1998, "iMac and USB PCs", "Apple and others", "computer", "usba", 2, "usb", "everything after is USB"),
    (2000, "PlayStation 2", "Sony", "console", "psx", 2, "passive", ""),
    (2001, "GameCube", "Nintendo", "console", "gc", 4, "passive", ""),
    (2001, "Xbox", "Microsoft", "console", "xbox", 4, "usb", "USB in a proprietary shell"),
    (2005, "Xbox 360", "Microsoft", "console", "usba; proprietary 2.4 GHz", 3, "usb", "wired pads are USB"),
    (2006, "PlayStation 3", "Sony", "console", "usba; Bluetooth", 4, "usb", ""),
    (2006, "Wii", "Nintendo", "console", "gc; Bluetooth", 4, "passive", "GameCube ports on early units only"),
    (2012, "Wii U", "Nintendo", "console", "usba; Bluetooth", 4, "usb", ""),
    (2013, "PlayStation 4", "Sony", "console", "usba; Bluetooth", 2, "usb", ""),
    (2013, "Xbox One", "Microsoft", "console", "usba; proprietary wireless", 3, "usb", ""),
    (2017, "Switch", "Nintendo", "console", "usbc; usba on dock; Bluetooth", 3, "usb", ""),
    (2020, "PlayStation 5", "Sony", "console", "usba; usbc; Bluetooth", 4, "usb", ""),
    (2020, "Xbox Series X / S", "Microsoft", "console", "usba; proprietary wireless", 2, "usb", ""),
    (2025, "Switch 2", "Nintendo", "console", "usbc; Bluetooth", 2, "usb", ""),
]

# ============================================================ drawing

def mm(v):
    return f"{v * S:.1f}"


class SVG:
    def __init__(self, w_mm, h_mm, label):
        self.w, self.h = w_mm, h_mm
        self.parts = [f'<svg role="img" aria-label="{html.escape(label)}" viewBox="0 0 {mm(w_mm)} {mm(h_mm)}" '
                      f'class="face">']

    def rect(self, x, y, w, h, r=0, cls="shell"):
        self.parts.append(f'<rect class="{cls}" x="{mm(x)}" y="{mm(y)}" width="{mm(w)}" height="{mm(h)}" rx="{mm(r)}"/>')

    def circle(self, cx, cy, d, cls="shell"):
        self.parts.append(f'<circle class="{cls}" cx="{mm(cx)}" cy="{mm(cy)}" r="{mm(d / 2)}"/>')

    def path(self, d, cls="shell"):
        self.parts.append(f'<path class="{cls}" d="{d}"/>')

    def caption(self, s):
        self.text(self.w - 1.5, self.h - 1.0, s, "dim", "end")

    def text(self, x, y, s, cls="pin", anchor="middle"):
        self.parts.append(f'<text class="{cls}" x="{mm(x)}" y="{mm(y)}" text-anchor="{anchor}">{html.escape(str(s))}</text>')

    def pin(self, x, y, n, d=2.3):
        self.circle(x, y, d, "pinhole")
        self.text(x, y + 0.55, n, "pin")

    def scale_bar(self):
        x0, y = 1.5, self.h - 1.5
        self.parts.append(f'<line class="bar" x1="{mm(x0)}" y1="{mm(y)}" x2="{mm(x0 + 10)}" y2="{mm(y)}"/>')
        self.text(x0 + 10.8, y + 0.5, "10 mm", "dim", "start")

    def done(self):
        self.scale_bar()
        self.parts.append("</svg>")
        return "\n".join(self.parts)


def P(x, y):
    return f"{mm(x)} {mm(y)}"


def draw_dsub(c):
    W, H = 48, 27
    s = SVG(W, H, f"{c['name']} face, {c['gender']}")
    cx, cy = W / 2, H / 2 - 1
    fw, fh = c["flange"]
    s.rect(cx - fw / 2, cy - fh / 2, fw, fh, 1.0, "flange")
    dw, dh = c["face"]
    t = 1.5  # taper per side at the bottom
    d = (f"M {P(cx - dw / 2 + 1, cy - dh / 2)} L {P(cx + dw / 2 - 1, cy - dh / 2)} "
         f"Q {P(cx + dw / 2, cy - dh / 2)} {P(cx + dw / 2, cy - dh / 2 + 1)} "
         f"L {P(cx + dw / 2 - t, cy + dh / 2 - 1)} Q {P(cx + dw / 2 - t - 0.3, cy + dh / 2)} {P(cx + dw / 2 - t - 1.3, cy + dh / 2)} "
         f"L {P(cx - dw / 2 + t + 1.3, cy + dh / 2)} Q {P(cx - dw / 2 + t + 0.3, cy + dh / 2)} {P(cx - dw / 2 + t, cy + dh / 2 - 1)} "
         f"L {P(cx - dw / 2, cy - dh / 2 + 1)} Q {P(cx - dw / 2, cy - dh / 2)} {P(cx - dw / 2 + 1, cy - dh / 2)} Z")
    s.path(d, "shell")
    # jack-screw holes
    pitch = 24.99 if c["pins"] == 9 else 33.3
    s.circle(cx - pitch / 2, cy, 3.1, "hole")
    s.circle(cx + pitch / 2, cy, 3.1, "hole")
    top, bot = c["rows"]
    pp = 2.77
    male = c["gender"].startswith("male")
    # male face view: pin 1 top-left; female face view: pin 1 top-right
    for i in range(top):
        x = cx - (top - 1) * pp / 2 + i * pp
        n = (i + 1) if male else (top - i)
        s.pin(x, cy - 1.5, n, 2.2)
    for i in range(bot):
        x = cx - (bot - 1) * pp / 2 + i * pp
        n = (top + i + 1) if male else (top + bot - i)
        s.pin(x, cy + 1.5, n, 2.2)
    s.caption("male, pins, face view" if male else "female, sockets, face view")
    return s.done()


def draw_row(c):
    W, H = 48, 27
    s = SVG(W, H, f"{c['name']} face, {c['gender']}")
    cx, cy = W / 2, H / 2 - 1
    fw, fh = c["face"]
    bw = c["body"]
    s.rect(cx - bw / 2, cy - (fh + 4) / 2, bw, fh + 4, 1.0, "flange")
    shape = c["shape"]
    x0, y0, x1, y1 = cx - fw / 2, cy - fh / 2, cx + fw / 2, cy + fh / 2
    if shape == "rrect":
        s.rect(x0, y0, fw, fh, 2.0)
    elif shape == "stadium":
        s.rect(x0, y0, fw, fh, fh / 2)
    elif shape == "dee":   # flat top, rounded lower corners, a key notch on top
        s.path(f"M {P(x0, y0)} L {P(x1, y0)} L {P(x1, y1 - 3)} Q {P(x1, y1)} {P(x1 - 3, y1)} L {P(x0 + 3, y1)} "
               f"Q {P(x0, y1)} {P(x0, y1 - 3)} Z")
    elif shape == "arch":  # N64: arched top, flat bottom
        s.path(f"M {P(x0, y1)} L {P(x0, y0 + 6)} Q {P(x0, y0)} {P(x0 + 6, y0)} L {P(x1 - 6, y0)} "
               f"Q {P(x1, y0)} {P(x1, y0 + 6)} L {P(x1, y1)} Z")
    elif shape == "dcurve":  # Dreamcast: wide, gently curved top
        s.path(f"M {P(x0, y1)} L {P(x0, y0 + 2.5)} Q {P(cx, y0 - 2.5)} {P(x1, y0 + 2.5)} L {P(x1, y1)} Z")
    elif shape == "trap":   # Xbox: wider at the top
        s.path(f"M {P(x0 - 1.5, y0)} L {P(x1 + 1.5, y0)} L {P(x1, y1)} L {P(x0, y1)} Z")
    groups = c["groups"]
    n = c["pins"]
    gap = 1.6
    pitch = (fw - 2.5 - gap * (len(groups) - 1)) / n
    x = x0 + 1.25 + pitch / 2
    k = 1
    for g in groups:
        for _ in range(g):
            s.pin(x, cy, k)
            x += pitch
            k += 1
        if g != groups[-1]:
            s.parts.append(f'<line class="key" x1="{mm(x - pitch / 2 - gap / 2 + gap / 2)}" y1="{mm(y0 + 0.8)}" '
                           f'x2="{mm(x - pitch / 2 - gap / 2 + gap / 2)}" y2="{mm(y1 - 0.8)}"/>')
            x += gap
    s.caption(c["gender"] + ", face view")
    return s.done()


def draw_gc(c):
    W, H = 48, 27
    s = SVG(W, H, f"{c['name']} face, {c['gender']}")
    cx, cy = W / 2, H / 2 - 1
    fw, fh = c["face"]
    s.rect(cx - c["body"] / 2, cy - (fh + 4) / 2, c["body"], fh + 4, 1.0, "flange")
    x0, y0, x1, y1 = cx - fw / 2, cy - fh / 2, cx + fw / 2, cy + fh / 2
    s.path(f"M {P(x0 + 2, y0)} L {P(x1 - 2, y0)} Q {P(x1, y0)} {P(x1, y0 + 2)} L {P(x1, y1 - 5)} "
           f"Q {P(x1, y1)} {P(x1 - 5, y1)} L {P(x0 + 5, y1)} Q {P(x0, y1)} {P(x0, y1 - 5)} L {P(x0, y0 + 2)} Q {P(x0, y0)} {P(x0 + 2, y0)} Z")
    for i, n in enumerate((1, 2, 3)):
        s.pin(cx - 3.6 + i * 3.6, cy - 2.2, n)
    for i, n in enumerate((4, 5, 6)):
        s.pin(cx - 3.6 + i * 3.6, cy + 2.2, n)
    s.caption("female on the console, face view; pin 7 is the cable shield")
    return s.done()


def draw_din(c):
    W, H = 48, 27
    s = SVG(W, H, f"{c['name']} face, {c['gender']}")
    cx, cy = W / 2, H / 2 - 1
    d = c["diam"]
    s.rect(cx - (d + 4) / 2, cy - (d + 4) / 2, d + 4, d + 4, 1.0, "flange")
    s.circle(cx, cy, d, "shell")
    # key notch at the bottom
    s.rect(cx - 1.2, cy + d / 2 - 1.2, 2.4, 1.4, 0, "key2")
    rows = c["layout"]
    rp = d / (len(rows) + 1)
    for ri, row in enumerate(rows):
        y = cy - d / 2 + rp * (ri + 1)
        cp = min(2.6, (d - 2.5) / max(len(row), 1))
        for ci, n in enumerate(row):
            x = cx - (len(row) - 1) * cp / 2 + ci * cp
            s.pin(x, y, n, 2.2)
    s.caption(c["gender"] + ", face view; numbering schematic")
    return s.done()


def draw_usb(c):
    W, H = 48, 27
    s = SVG(W, H, f"{c['name']} face, {c['gender']}")
    cx, cy = W / 2, H / 2 - 1
    v = c["variant"]
    if v == "c":
        fw, fh = c["face"]
        s.rect(cx - (fw + 4) / 2, cy - (fh + 4) / 2, fw + 4, fh + 4, 1.0, "flange")
        s.rect(cx - fw / 2, cy - fh / 2, fw, fh, fh / 2, "shell")
        s.rect(cx - fw / 2 + 1.2, cy - 0.35, fw - 2.4, 0.7, 0.3, "tongue")
        s.caption("24 contacts, reversible")
        return s.done()
    fw, fh = c["face"]
    s.rect(cx - (fw + 4) / 2, cy - (fh + 4) / 2, fw + 4, fh + 4, 1.0, "flange")
    s.rect(cx - fw / 2, cy - fh / 2, fw, fh, 0.5, "shell")
    # tongue with contacts
    s.rect(cx - fw / 2 + 1.0, cy - 0.9, fw - 2.0, 2.0, 0.2, "tongue" if v == "a" else "tongue3")
    xs = [cx - 4.2, cx - 1.4, cx + 1.4, cx + 4.2]
    for x, n in zip(xs, (1, 2, 3, 4)):
        s.rect(x - 0.4, cy - 0.7, 0.8, 1.2, 0, "contact")
        s.text(x, cy + 3.6, n, "pin")
    if v == "3a":
        xs5 = [cx - 5.2, cx - 2.6, cx, cx + 2.6, cx + 5.2]
        for x, n in zip(xs5, (5, 6, 7, 8, 9)):
            s.rect(x - 0.35, cy - 2.6, 0.7, 1.4, 0, "contact")
            s.text(x, cy - 3.6, n, "pin")
        s.caption("SNAC: 5 V on 1, GND on 4, IO on 8 2 7 3 6 5 9")
    else:
        s.caption("1 VBUS, 2 D-, 3 D+, 4 GND")
    return s.done()


DRAW = dict(dsub=draw_dsub, row=draw_row, gc=draw_gc, din=draw_din, usb=draw_usb)


def budget_svg(items, title, limit):
    W, H = 460, 100 + 14 * len(items)
    total = sum(v for _, v in items)
    scale = 380 / limit
    x = 40
    parts = [f'<svg role="img" aria-label="{html.escape(title)}" viewBox="0 0 {W} {H}" class="budget">']
    parts.append(f'<line class="axis" x1="40" y1="30" x2="{40 + limit * scale:.0f}" y2="30"/>')
    for tick in range(0, int(limit) + 1, 20):
        tx = 40 + tick * scale
        parts.append(f'<line class="axis" x1="{tx:.0f}" y1="26" x2="{tx:.0f}" y2="34"/>')
        parts.append(f'<text class="tick" x="{tx:.0f}" y="20" text-anchor="middle">{tick}</text>')
    parts.append(f'<text class="tick" x="{40 + limit * scale + 6:.0f}" y="34" text-anchor="start">mm</text>')
    y = 44
    for i, (name, v) in enumerate(items):
        w = v * scale
        parts.append(f'<rect class="seg{i % 2}" x="{x:.1f}" y="{y}" width="{w:.1f}" height="16"/>')
        if v >= 6:
            parts.append(f'<text class="lbl" x="{x + w / 2:.1f}" y="{y + 12}" text-anchor="middle">{v:g}</text>')
        x += w
    parts.append(f'<line class="limit" x1="{40 + limit * scale:.0f}" y1="26" x2="{40 + limit * scale:.0f}" y2="70"/>')
    parts.append(f'<text class="sum" x="40" y="86" text-anchor="start">{total:g} of {limit:g} mm used, {limit - total:g} mm spare</text>')
    ly = 104
    for i, (name, v) in enumerate(items):
        col = i % 2
        lx = 40 + (i // 2) * 0
        parts.append(f'<rect class="seg{col}" x="40" y="{ly - 9}" width="10" height="10"/>')
        parts.append(f'<text class="key" x="56" y="{ly}" text-anchor="start">{html.escape(name)}: {v:g} mm</text>')
        ly += 14
    parts.append("</svg>")
    return "\n".join(parts)


# ============================================================ page

def chip(kind):
    k = kind.lower()
    if k.startswith("yes") or k.startswith("it is"):
        return f'<span class="chip ok">{html.escape(kind)}</span>'
    if k.startswith("no"):
        return f'<span class="chip no">{html.escape(kind)}</span>'
    return f'<span class="chip warn">{html.escape(kind)}</span>'


def verdict(c):
    spare = DEPTH - (BUDGET[0][1] + c["depth"] + BUDGET[2][1] + BUDGET[3][1] + BUDGET[4][1])
    return spare


def connector_cards():
    out = []
    for c in CONNECTORS:
        svg = DRAW[c["draw"]](c)
        spare = verdict(c)
        fw, fh = c["face"]
        out.append(f'''
<article class="card" id="c-{c["id"]}">
  <figure>{svg}<figcaption>{html.escape(c["name"])}, {html.escape(c["gender"])}. Drawn at the shared scale.</figcaption></figure>
  <h3>{html.escape(c["name"])}</h3>
  <p class="systems">{html.escape(c["systems"])}</p>
  <dl>
    <div><dt>Plug opening</dt><dd>{fw:g} × {fh:g} mm</dd></div>
    <div><dt>Socket body</dt><dd>{c["body"]:g} mm wide, {c["depth"]:g} mm deep</dd></div>
    <div><dt>Panel width</dt><dd>{c["slots"]} slot{"s" if c["slots"] > 1 else ""} of 25 mm</dd></div>
    <div><dt>80 mm cassette</dt><dd>{"fits, " if spare >= 0 else "does not fit, "}{abs(spare):g} mm {"spare" if spare >= 0 else "over"}</dd></div>
    <div><dt>Logic</dt><dd>{html.escape(c["level"])}</dd></div>
    <div><dt>Lines</dt><dd>{html.escape(c["lines"])}</dd></div>
    <div><dt>Passive SNAC section</dt><dd>{chip(c["snac"])}</dd></div>
    <div><dt>Sourcing</dt><dd>{html.escape(c["source"])}</dd></div>
  </dl>
  <p class="note">{html.escape(c["note"])}</p>
</article>''')
    return "\n".join(out)


def systems_rows():
    names = {c["id"]: c["name"] for c in CONNECTORS}
    out = []
    for year, name, maker, cls, port, n, kind, note in sorted(SYSTEMS, key=lambda r: (r[0], r[1])):
        # link port ids that exist
        parts = []
        for tok in port.split(";"):
            tok = tok.strip()
            pid = tok.split(" ")[0]
            if pid in names:
                rest = tok[len(pid):]
                parts.append(f'<a href="#c-{pid}">{html.escape(names[pid])}</a>{html.escape(rest)}')
            else:
                parts.append(html.escape(tok))
        kind_label = {"passive": "passive, SNAC-style", "usb": "USB section with MCU", "none": "none"}[kind]
        out.append(f'<tr data-class="{cls}"><td class="num">{year}</td><td>{html.escape(name)}</td><td class="muted">{html.escape(maker)}</td>'
                   f'<td>{html.escape(cls)}</td><td>{"; ".join(parts)}</td><td class="num">{n}</td>'
                   f'<td><span class="chip {"ok" if kind == "passive" else "warn"}">{kind_label}</span></td>'
                   f'<td class="muted">{html.escape(note)}</td></tr>')
    return "\n".join(out)


def page():
    n_fit = sum(1 for c in CONNECTORS if verdict(c) >= 0)
    deepest = max(CONNECTORS, key=lambda c: c["depth"])
    total_ribbon = sum(v for _, v in BUDGET)
    total_pig = sum(v for _, v in BUDGET_PIGTAIL)
    undrawn = "\n".join(f"<li><strong>{html.escape(a)}</strong> {html.escape(b)}</li>" for a, b in UNDRAWN)
    return f'''<title>Controller Port Atlas</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans+Condensed:wght@500;600&family=IBM+Plex+Sans:wght@400;500&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
:root {{
  --bg: #eceef1; --panel: #f7f8fa; --ink: #191c21; --muted: #5a6370; --line: #c9ced6;
  --accent: #1f6fd1; --ok: #2b8a4e; --warn: #b06a10; --no: #b8382f;
  --shell: #101214; --flange: #d9dce2;
  color-scheme: light;
}}
@media (prefers-color-scheme: dark) {{
  :root:not([data-theme="light"]) {{
    --bg: #141517; --panel: #1d1f23; --ink: #e9e7e0; --muted: #a0a4ab; --line: #363a41;
    --accent: #6eb0ff; --ok: #5ccc82; --warn: #e5a03a; --no: #f0776c; --shell: #060708; --flange: #2b2e34;
    color-scheme: dark;
  }}
}}
:root[data-theme="dark"] {{
  --bg: #141517; --panel: #1d1f23; --ink: #e9e7e0; --muted: #a0a4ab; --line: #363a41;
  --accent: #6eb0ff; --ok: #5ccc82; --warn: #e5a03a; --no: #f0776c; --shell: #060708; --flange: #2b2e34;
  color-scheme: dark;
}}
body {{ background: var(--bg); color: var(--ink); font-family: "IBM Plex Sans", system-ui, sans-serif; font-size: 15px; line-height: 1.5; margin: 0; }}
.wrap {{ max-width: 1180px; margin: 0 auto; padding-block: 32px 64px; padding-inline: 20px; }}
h1, h2, h3 {{ font-family: "IBM Plex Sans Condensed", "IBM Plex Sans", system-ui, sans-serif; text-wrap: balance; margin: 0; }}
h1 {{ font-size: clamp(30px, 5vw, 46px); font-weight: 600; letter-spacing: -0.01em; line-height: 1.05; }}
h2 {{ font-size: 24px; font-weight: 600; margin-top: 48px; margin-bottom: 12px; padding-top: 12px; border-top: 2px solid var(--ink); }}
h3 {{ font-size: 19px; font-weight: 600; }}
.eyebrow {{ font-family: "IBM Plex Mono", ui-monospace, monospace; font-size: 12px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); margin-bottom: 8px; }}
p {{ max-width: 68ch; }}
.lede {{ font-size: 17px; max-width: 70ch; margin-top: 12px; }}
.muted {{ color: var(--muted); }}
.mono, .num, td.num {{ font-family: "IBM Plex Mono", ui-monospace, monospace; font-variant-numeric: tabular-nums; }}
a {{ color: var(--accent); }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(340px, 1fr)); gap: 18px; margin-top: 16px; }}
.card {{ background: var(--panel); border: 1px solid var(--line); padding: 14px 16px 16px; display: flex; flex-direction: column; gap: 8px; }}
.card figure {{ margin: 0; }}
.card figcaption {{ display: none; }}
.card .systems {{ font-size: 13.5px; color: var(--muted); margin: 0; }}
.card dl {{ display: grid; grid-template-columns: 1fr 1fr; gap: 6px 14px; margin: 4px 0 0; font-size: 13.5px; }}
.card dl div {{ display: flex; flex-direction: column; }}
.card dt {{ font-family: "IBM Plex Mono", ui-monospace, monospace; font-size: 11px; letter-spacing: 0.06em; text-transform: uppercase; color: var(--muted); }}
.card dd {{ margin: 0; }}
.card .note {{ font-size: 13.5px; margin: 4px 0 0; }}
.chip {{ display: inline-block; font-family: "IBM Plex Mono", ui-monospace, monospace; font-size: 11.5px; padding: 1px 7px; border-radius: 3px; border: 1px solid currentColor; }}
.chip.ok {{ color: var(--ok); }} .chip.warn {{ color: var(--warn); }} .chip.no {{ color: var(--no); }}
svg.face {{ width: 100%; max-width: 100%; height: auto; display: block; background: var(--panel); color: var(--ink); }}
svg.face .flange {{ fill: var(--flange); stroke: currentColor; stroke-width: 0.6; }}
svg.face .shell {{ fill: var(--shell); stroke: currentColor; stroke-width: 0.8; }}
svg.face .hole {{ fill: var(--panel); stroke: currentColor; stroke-width: 0.6; }}
svg.face .pinhole {{ fill: var(--panel); stroke: currentColor; stroke-width: 0.5; }}
svg.face .pin {{ font: 500 6px "IBM Plex Mono", ui-monospace, monospace; fill: currentColor; }}
svg.face .dim {{ font: 5.2px "IBM Plex Mono", ui-monospace, monospace; fill: var(--muted); }}
svg.face .bar {{ stroke: currentColor; stroke-width: 1.2; }}
svg.face .key {{ stroke: var(--flange); stroke-width: 1.2; }}
svg.face .key2 {{ fill: var(--flange); }}
svg.face .tongue {{ fill: #d8d8d2; stroke: none; }}
svg.face .tongue3 {{ fill: #2a6fd6; stroke: none; }}
svg.face .contact {{ fill: #c9a23a; stroke: none; }}
.two {{ display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-top: 16px; }}
.budgetbox {{ background: var(--panel); border: 1px solid var(--line); padding: 14px 16px; }}
.budgetbox h3 {{ margin-bottom: 6px; }}
svg.budget {{ width: 100%; height: auto; display: block; color: var(--ink); }}
svg.budget .axis {{ stroke: currentColor; stroke-width: 1; }}
svg.budget .tick, svg.budget .key, svg.budget .sum {{ font: 11px "IBM Plex Mono", ui-monospace, monospace; fill: currentColor; }}
svg.budget .sum {{ font-weight: 500; font-size: 12px; }}
svg.budget .lbl {{ font: 500 10px "IBM Plex Mono", ui-monospace, monospace; fill: #fff; }}
svg.budget .seg0 {{ fill: #3d6fb5; }} svg.budget .seg1 {{ fill: #6d8fc5; }}
svg.budget .limit {{ stroke: var(--no); stroke-width: 1.5; stroke-dasharray: 4 3; }}
.verdict {{ border-left: 4px solid var(--accent); padding: 6px 14px; margin-top: 18px; background: var(--panel); }}
.verdict p {{ margin: 6px 0; }}
.tablewrap {{ overflow-x: auto; margin-top: 12px; border: 1px solid var(--line); background: var(--panel); }}
table {{ border-collapse: collapse; width: 100%; font-size: 13.5px; min-width: 900px; }}
th, td {{ text-align: left; padding: 7px 10px; border-bottom: 1px solid var(--line); vertical-align: top; }}
th {{ font-family: "IBM Plex Mono", ui-monospace, monospace; font-size: 11px; letter-spacing: 0.06em; text-transform: uppercase; color: var(--muted); font-weight: 500; position: sticky; top: env(safe-area-inset-top, 0px); background: var(--panel); }}
.filters {{ display: flex; gap: 8px; flex-wrap: wrap; margin-top: 12px; }}
.filters button {{ font: 500 13px "IBM Plex Mono", ui-monospace, monospace; padding: 5px 12px; border: 1px solid var(--line); background: var(--panel); color: var(--ink); cursor: pointer; border-radius: 3px; }}
.filters button[aria-pressed="true"] {{ border-color: var(--accent); color: var(--accent); }}
.filters button:focus-visible {{ outline: 2px solid var(--accent); outline-offset: 2px; }}
ul.plain {{ padding-left: 18px; max-width: 70ch; }}
.foot {{ margin-top: 40px; font-size: 13.5px; color: var(--muted); max-width: 70ch; }}
@media (max-width: 720px) {{ .two {{ grid-template-columns: 1fr; }} .card dl {{ grid-template-columns: 1fr; }} }}
@media (prefers-reduced-motion: no-preference) {{ .filters button {{ transition: border-color .15s, color .15s; }} }}
</style>
<div class="wrap">
<p class="eyebrow">hw/panel reference · controller ports 1977 to 2026</p>
<h1>Controller Port Atlas</h1>
<p class="lede">Every controller port a retro front panel might carry, drawn to one scale, with what each needs from the panel: how wide a segment, how deep a cassette, how many lines, and whether a passive SNAC-style section can carry it or the section needs its own microcontroller. Systems that never had a wired port are listed so nobody looks for one.</p>
<p class="muted">Every dimension on this page is a from-memory estimate written down to be replaced by a measurement. The drawings are the shape and pin count as commonly charted; pin numbering is as the usual pinout tables give it and must be checked on the real part before wiring anything.</p>

<h2>Does 80 mm fit them all?</h2>
<p>Yes. The deepest socket bodies here are the {html.escape(deepest["name"])}, Dreamcast and original Xbox at about {deepest["depth"]:g} mm behind the plate. With a ribbon to the backplane the cassette stack uses {total_ribbon:g} of 80 mm at the worst socket; with a USB3 pigtail standing straight off the backplane it uses {total_pig:g}, which is why our own sections should use the ribbon and leave the USB3 sockets for commercial SNAC dongles that arrive with a pigtail anyway. {n_fit} of {len(CONNECTORS)} connectors fit with the ribbon; none exceed it.</p>
<div class="two">
  <div class="budgetbox"><h3>Ribbon to the backplane</h3>{budget_svg(BUDGET, "Cassette depth budget with a ribbon cable: 63 of 80 mm used", DEPTH)}</div>
  <div class="budgetbox"><h3>USB3 pigtail to the backplane</h3>{budget_svg(BUDGET_PIGTAIL, "Cassette depth budget with a USB3 pigtail: 79 of 80 mm used", DEPTH)}</div>
</div>
<div class="verdict">
  <p><strong>Rule for the rails:</strong> 80 mm front plate to backplane. A socket may be up to 25 mm deep behind the plate, which every port below satisfies; 30 mm would still fit with a ribbon.</p>
  <p><strong>Rule for the width:</strong> any port with a plug opening or socket body over 22 mm takes a two-slot segment (50 mm). D-subs, SNES, Saturn, PlayStation and Dreamcast are two-slot; NES, N64, GameCube, the round DINs and USB are one-slot and stack four high.</p>
</div>

<h2>The connectors</h2>
<p>Face views at a shared scale, 10 mm bar in each. "Passive SNAC section" means a section with sockets and level shifters only, seven IO lines to the backplane in SNAC order, and the backplane's MCU speaking the protocol. Anything analogue, anything with more than seven lines and anything that is really USB needs a section with its own MCU on the hub instead.</p>
<div class="grid">
{connector_cards()}
</div>
<h3 style="margin-top:24px">Not drawn</h3>
<ul class="plain">
{undrawn}
</ul>

<h2>The systems</h2>
<p>Chronological, consoles and the home computers and PCs people keep. Port counts are the machine's own; wireless-only generations are listed with their USB pad ports because a panel for them is just USB.</p>
<div class="filters" role="group" aria-label="Filter systems">
  <button id="f-all" aria-pressed="true" data-f="all">all</button>
  <button id="f-console" aria-pressed="false" data-f="console">consoles</button>
  <button id="f-computer" aria-pressed="false" data-f="computer">computers and PCs</button>
</div>
<div class="tablewrap">
<table id="systems">
<thead><tr><th>Year</th><th>System</th><th>Maker</th><th>Class</th><th>Controller port</th><th>Ports</th><th>Section</th><th>Notes</th></tr></thead>
<tbody>
{systems_rows()}
</tbody>
</table>
</div>

<h2>What this says for the panel</h2>
<ul class="plain">
  <li><strong>Seven passive sections cover the 8-bit and 16-bit world and Nintendo's 3.3 V buses:</strong> DE-9, NES, SNES, Saturn, PlayStation, N64, GameCube, plus Dreamcast and PC Engine if wanted. All are at most 25 mm deep and one or two slots wide.</li>
  <li><strong>Three families need an MCU on the section:</strong> DA-15 machines (Neo Geo, Jaguar, 5200, the PC game port and its MIDI), keyboards and mice (PS/2, DIN 5, ADB), and analogue sticks (Apple II, BBC, 5200). They are USB sections on the hub.</li>
  <li><strong>From the Xbox onward there is nothing to build:</strong> the port is USB or the pad is wireless. A two-slot USB section with four USB-A covers 2001 to 2026.</li>
  <li><strong>Sourcing risk sits in six replacement-part sockets:</strong> NES, SNES, Saturn, PlayStation, N64, GameCube, plus Dreamcast and TurboGrafx if built. Everything else is a catalogue part. The SNAC segment covers any of them with a bought adapter until its module exists.</li>
</ul>

<p class="foot">Compiled from memory of nesdev, the MiSTer SNAC ecosystem, adapter firmware sources and console service manuals; nothing here was fetched or measured. The one verified fact is the SNAC channel-to-pin order, read from blue212's schematic. Generated by <span class="mono">docs/gen_port_atlas.py</span>; edit the tables there.</p>
</div>
<script>
(function () {{
  var rows = document.querySelectorAll('#systems tbody tr');
  var buttons = document.querySelectorAll('.filters button');
  function apply(f) {{
    rows.forEach(function (r) {{ r.hidden = !(f === 'all' || r.dataset.class === f); }});
    buttons.forEach(function (b) {{ b.setAttribute('aria-pressed', String(b.dataset.f === f)); }});
    try {{ localStorage.setItem('atlas-filter', f); }} catch (e) {{}}
  }}
  buttons.forEach(function (b) {{ b.addEventListener('click', function () {{ apply(b.dataset.f); }}); }});
  var saved = null;
  try {{ saved = localStorage.getItem('atlas-filter'); }} catch (e) {{}}
  if (saved === 'console' || saved === 'computer') apply(saved);
}})();
</script>
'''


if __name__ == "__main__":
    OUT.write_text(page())
    print("wrote", OUT, f"{OUT.stat().st_size / 1024:.0f} kB")
