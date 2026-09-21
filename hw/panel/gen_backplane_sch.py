#!/usr/bin/env python3
"""Generate hw/panel/backplane/backplane.kicad_sch: the backplane schematic.

It is a netlist-level schematic: every part is a box symbol with named pins,
and every pin carries a global label naming its net. KiCad connects by label,
so the drawing is complete for ERC, BOM and netlist without a single drawn
wire, and the whole design is reviewable as text in this file.

Pin NUMBERS on the large ICs (RP2350B, FE1.1s) are their pin NAMES, not the
package pin numbers: assign footprints from the vendor libraries and let the
symbol-to-footprint mapping supply the numbers. Jellybean parts (resistor
arrays, ESD arrays, DIP switches) use the usual numbering but must be checked
against the footprint chosen.

Read BACKPLANE.md for the circuit; this file is the circuit.
"""
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT_DIR = HERE / "backplane"
OUT = OUT_DIR / "backplane.kicad_sch"

PORTS = 10                 # SNAC ports; slots 2..11 of the frame
PORTS_PER_MCU = 5
PASSTHROUGH_PORT = 10      # the port with the rear MiSTer socket
G = 2.54                   # schematic grid

_NS = uuid.UUID("2b7e6a10-9c4d-4d0e-8a1f-0123456789ab")
_n = [0]


def U():
    _n[0] += 1
    return str(uuid.uuid5(_NS, str(_n[0])))


ROOT = U()


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".") if abs(v) > 1e-9 else "0"


# ============================================================ symbol library
# name -> (left pins, right pins); each pin (number, name, type)

def gpio(n):
    return [(f"GPIO{i}", f"GPIO{i}", "bidirectional") for i in range(n)]


LIB = {
    "RP2350B": ([("IOVDD", "IOVDD", "power_in"), ("USB_VDD", "USB_VDD", "power_in"), ("ADC_AVDD", "ADC_AVDD", "power_in"),
                 ("VREG_VIN", "VREG_VIN", "power_in"), ("VREG_AVDD", "VREG_AVDD", "power_in"), ("VREG_LX", "VREG_LX", "output"),
                 ("VREG_FB", "VREG_FB", "input"), ("VREG_PGND", "VREG_PGND", "power_in"), ("DVDD", "DVDD", "power_in"),
                 ("GND", "GND", "power_in"),
                 ("USB_DP", "USB_DP", "bidirectional"), ("USB_DM", "USB_DM", "bidirectional"),
                 ("XIN", "XIN", "input"), ("XOUT", "XOUT", "output"),
                 ("QSPI_SCLK", "QSPI_SCLK", "output"), ("QSPI_SS", "QSPI_SS", "output"), ("QSPI_SD0", "QSPI_SD0", "bidirectional"),
                 ("QSPI_SD1", "QSPI_SD1", "bidirectional"), ("QSPI_SD2", "QSPI_SD2", "bidirectional"), ("QSPI_SD3", "QSPI_SD3", "bidirectional"),
                 ("RUN", "RUN", "input"), ("SWCLK", "SWCLK", "input"), ("SWD", "SWD", "bidirectional")],
                gpio(48)),
    "FE1.1s": ([("VDD33", "VDD33", "power_in"), ("VD18", "VD18", "passive"), ("GND", "GND", "power_in"),
                ("XIN", "XIN", "input"), ("XOUT", "XOUT", "output"), ("RESET#", "RESET#", "input"),
                ("DM0", "DM0 (up)", "bidirectional"), ("DP0", "DP0 (up)", "bidirectional")],
               [("DM1", "DM1", "bidirectional"), ("DP1", "DP1", "bidirectional"), ("DM2", "DM2", "bidirectional"), ("DP2", "DP2", "bidirectional"),
                ("DM3", "DM3", "bidirectional"), ("DP3", "DP3", "bidirectional"), ("DM4", "DM4", "bidirectional"), ("DP4", "DP4", "bidirectional")]),
    "W25Q16": ([("1", "/CS", "input"), ("2", "DO", "output"), ("3", "/WP", "input"), ("4", "GND", "power_in")],
               [("8", "VCC", "power_in"), ("7", "/HOLD", "input"), ("6", "CLK", "input"), ("5", "DI", "input")]),
    "74HC165": ([("1", "SH/LD", "input"), ("2", "CLK", "input"), ("15", "CLK_INH", "input"), ("10", "SER", "input"),
                 ("8", "GND", "power_in"), ("16", "VCC", "power_in")],
                [("11", "A", "input"), ("12", "B", "input"), ("13", "C", "input"), ("14", "D", "input"),
                 ("3", "E", "input"), ("4", "F", "input"), ("5", "G", "input"), ("6", "H", "input"),
                 ("9", "QH", "output"), ("7", "/QH", "output")]),
    "DIP4": ([("1", "1", "passive"), ("2", "2", "passive"), ("3", "3", "passive"), ("4", "4", "passive")],
             [("8", "8", "passive"), ("7", "7", "passive"), ("6", "6", "passive"), ("5", "5", "passive")]),
    "RN4": ([("1", "R1a", "passive"), ("2", "R2a", "passive"), ("3", "R3a", "passive"), ("4", "R4a", "passive")],
            [("8", "R1b", "passive"), ("7", "R2b", "passive"), ("6", "R3b", "passive"), ("5", "R4b", "passive")]),
    "ESD4": ([("1", "IO1", "passive"), ("2", "IO2", "passive"), ("3", "GND", "power_in")],
             [("6", "IO3", "passive"), ("5", "IO4", "passive"), ("4", "VCC", "passive")]),
    "IDC2x5": ([("1", "1", "passive"), ("3", "3", "passive"), ("5", "5", "passive"), ("7", "7", "passive"), ("9", "9", "passive")],
               [("2", "2", "passive"), ("4", "4", "passive"), ("6", "6", "passive"), ("8", "8", "passive"), ("10", "10", "passive")]),
    "USB3A": ([("1", "VBUS", "passive"), ("2", "D-", "passive"), ("3", "D+", "passive"), ("4", "GND", "passive"), ("SH", "SHIELD", "passive")],
              [("5", "SSRX-", "passive"), ("6", "SSRX+", "passive"), ("7", "GND_DRAIN", "passive"), ("8", "SSTX-", "passive"), ("9", "SSTX+", "passive")]),
    "USBB": ([("1", "VBUS", "passive"), ("2", "D-", "passive"), ("3", "D+", "passive")],
             [("4", "GND", "passive"), ("SH", "SHIELD", "passive")]),
    "HDR1x4": ([("1", "1", "passive"), ("2", "2", "passive")], [("3", "3", "passive"), ("4", "4", "passive")]),
    "HDR1x3": ([("1", "1", "passive"), ("2", "2", "passive")], [("3", "3", "passive")]),
    "HDR2x8": ([(str(i), str(i), "passive") for i in range(1, 16, 2)], [(str(i), str(i), "passive") for i in range(2, 17, 2)]),
    "HDR3x7": ([(f"M{i}", f"M{i}", "passive") for i in range(1, 8)],
               [(f"C{i}", f"C{i}", "passive") for i in range(1, 8)] + [(f"R{i}", f"R{i}", "passive") for i in range(1, 8)]),
    "BARREL": ([("1", "VIN", "passive")], [("2", "GND", "passive"), ("3", "GND", "passive")]),
    "R": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "C": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "L": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "FB": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "F": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "TVS": ([("1", "K", "passive")], [("2", "A", "passive")]),
    "LED": ([("1", "A", "passive")], [("2", "K", "passive")]),
    "SW": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "XTAL": ([("1", "1", "passive")], [("2", "2", "passive")]),
    "PFET": ([("G", "G", "input")], [("S", "S", "passive"), ("D", "D", "passive")]),
    "LDO": ([("1", "GND", "power_in"), ("3", "VIN", "power_in")], [("2", "VOUT", "power_out")]),
}
REF_PREFIX = {"RP2350B": "U", "FE1.1s": "U", "W25Q16": "U", "74HC165": "U", "LDO": "U", "DIP4": "SW", "SW": "SW",
              "RN4": "RN", "ESD4": "D", "TVS": "D", "LED": "D", "R": "R", "C": "C", "L": "L", "FB": "FB", "F": "F",
              "XTAL": "Y", "PFET": "Q", "IDC2x5": "J", "USB3A": "J", "USBB": "J", "HDR1x4": "J", "HDR1x3": "J",
              "HDR2x8": "J", "HDR3x7": "JP", "BARREL": "J"}


def sym_size(name):
    l, r = LIB[name]
    rows = max(len(l), len(r))
    width = 10 * G if name in ("RP2350B", "FE1.1s", "74HC165", "HDR3x7") else 6 * G
    return width, (rows + 1) * G


def lib_symbol(name):
    l, r = LIB[name]
    w, h = sym_size(name)
    out = [f'    (symbol "panel:{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)',
           f'      (property "Reference" "{REF_PREFIX[name]}" (at 0 {f(h / 2 + G)} 0) (effects (font (size 1.27 1.27))))',
           f'      (property "Value" "{name}" (at 0 {f(-h / 2 - G)} 0) (effects (font (size 1.27 1.27))))',
           '      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
           '      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
           '      (property "Description" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
           f'      (symbol "{name}_0_1"',
           f'        (rectangle (start {f(-w / 2)} {f(h / 2)}) (end {f(w / 2)} {f(-h / 2)}) (stroke (width 0.254) (type default)) (fill (type background)))',
           '      )',
           f'      (symbol "{name}_1_1"']
    for side, pins in (("L", l), ("R", r)):
        for i, (num, pname, ptype) in enumerate(pins):
            y = h / 2 - (i + 1) * G           # lib coords: y up
            if side == "L":
                x, rot = -w / 2 - G, 0
            else:
                x, rot = w / 2 + G, 180
            out.append(f'        (pin {ptype} line (at {f(x)} {f(y)} {rot}) (length {f(G)}) '
                       f'(name "{pname}" (effects (font (size 1.27 1.27)))) (number "{num}" (effects (font (size 1.27 1.27)))))')
    out.append('      )')
    out.append('    )')
    return "\n".join(out)


# ============================================================ schematic builder

class Sch:
    def __init__(self):
        self.items = []
        self.used = set()
        self.counts = {}

    def ref(self, name, fixed=None):
        if fixed:
            return fixed
        p = REF_PREFIX[name]
        self.counts[p] = self.counts.get(p, 0) + 1
        return f"{p}{self.counts[p]}"

    def place(self, name, x, y, nets, value=None, ref=None, note=None):
        """Place a symbol at (x, y) (schematic coords, y down) and label its pins.
        nets: dict pin_number -> net name; pins absent from it get a no-connect flag."""
        self.used.add(name)
        l, r = LIB[name]
        w, h = sym_size(name)
        ref = self.ref(name, ref)
        val = value or name
        lines = [f'  (symbol (lib_id "panel:{name}") (at {f(x)} {f(y)} 0) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)',
                 f'    (uuid "{U()}")',
                 f'    (property "Reference" "{ref}" (at {f(x)} {f(y - h / 2 - G)} 0) (effects (font (size 1.27 1.27))))',
                 f'    (property "Value" "{val}" (at {f(x)} {f(y + h / 2 + G)} 0) (effects (font (size 1.27 1.27))))',
                 f'    (property "Footprint" "" (at {f(x)} {f(y)} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
                 f'    (property "Datasheet" "" (at {f(x)} {f(y)} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
                 f'    (property "Description" "{note or ""}" (at {f(x)} {f(y)} 0) (effects (font (size 1.27 1.27)) (hide yes)))']
        labels = []
        for side, pins in (("L", l), ("R", r)):
            for i, (num, pname, ptype) in enumerate(pins):
                lines.append(f'    (pin "{num}" (uuid "{U()}"))')
                py = y - (h / 2 - (i + 1) * G)          # schematic y down
                px = x + (-w / 2 - G if side == "L" else w / 2 + G)
                net = nets.get(num)
                if net is None:
                    labels.append(f'  (no_connect (at {f(px)} {f(py)}) (uuid "{U()}"))')
                else:
                    rot = 180 if side == "L" else 0
                    just = "right" if side == "L" else "left"
                    labels.append(f'  (global_label "{net}" (shape bidirectional) (at {f(px)} {f(py)} {rot}) (fields_autoplaced yes)\n'
                                  f'    (effects (font (size 1.27 1.27)) (justify {just}))\n    (uuid "{U()}")\n'
                                  f'    (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {f(px)} {f(py)} 0) '
                                  f'(effects (font (size 1.27 1.27)) (hide yes))))')
        lines.append(f'    (instances (project "backplane" (path "/{ROOT}" (reference "{ref}") (unit 1))))')
        lines.append('  )')
        self.items.append("\n".join(lines))
        self.items.extend(labels)
        return ref, w, h

    def text(self, s, x, y, size=2.0):
        s = s.replace('"', "'").replace("\n", "\\n")
        self.items.append(f'  (text "{s}" (exclude_from_sim no) (at {f(x)} {f(y)} 0) (effects (font (size {size} {size})) (justify left bottom)) (uuid "{U()}"))')

    def write(self, path):
        libs = "\n".join(lib_symbol(n) for n in sorted(self.used))
        out = ('(kicad_sch\n  (version 20231120)\n  (generator "gen_backplane_sch")\n  (generator_version "8.0")\n'
               f'  (uuid "{ROOT}")\n  (paper "A0")\n'
               '  (title_block (title "controller panel backplane") (date "2026-09-21") (rev "0.1") '
               '(comment 1 "generated by hw/panel/gen_backplane_sch.py - do not edit by hand") '
               '(comment 2 "netlist by global label; IC pin numbers are pin names until footprints are assigned"))\n'
               f'  (lib_symbols\n{libs}\n  )\n' + "\n".join(self.items) + '\n  (sheet_instances (path "/" (page "1")))\n)\n')
        path.write_text(out)


# ============================================================ a column placer

class Col:
    """Stacks symbols down a column; wraps to the next column at a height limit."""
    def __init__(self, sch, x0, y0, colw, ymax):
        self.s, self.x, self.y, self.colw, self.ymax, self.y0 = sch, x0, y0, colw, ymax, y0

    def put(self, name, nets, **kw):
        w, h = sym_size(name)
        if self.y + h > self.ymax:
            self.x += self.colw
            self.y = self.y0
        ref, w, h = self.s.place(name, self.x, self.y + h / 2, nets, **kw)
        self.y += h + 3 * G
        return ref

    def heading(self, s):
        if self.y + 6 * G > self.ymax:
            self.x += self.colw
            self.y = self.y0
        self.s.text(s, self.x - 8 * G, self.y + G, 2.5)
        self.y += 3 * G


# ============================================================ the circuit

def build():
    s = Sch()
    s.text("controller panel backplane, rev 0.1. Nets connect by global label. Ports P1..P10 are the SNAC ports, "
           "slots 2..11 of the frame. Read BACKPLANE.md.", 20, 20, 3.0)

    # ---------------- power
    c = Col(s, 40, 40, 90, 780)
    c.heading("POWER IN: 5 V, 3 A")
    c.put("BARREL", {"1": "VIN_RAW", "2": "GND", "3": "GND"}, value="DC jack 5.5/2.1", note="5 V from the host PSU; alternative 2-pin JST-XH in parallel")
    c.put("F", {"1": "VIN_RAW", "2": "VIN_F"}, value="polyfuse 3 A")
    c.put("PFET", {"G": "GND", "S": "+5V", "D": "VIN_F"}, value="AO3401 reverse-polarity", note="gate to GND; body diode conducts at power-up, then the channel shorts it. Vgs = -5 V is within rating, no zener needed")
    c.put("TVS", {"1": "+5V", "2": "GND"}, value="SMBJ5.0A")
    c.put("C", {"1": "+5V", "2": "GND"}, value="100 uF bulk")
    c.put("C", {"1": "+5V", "2": "GND"}, value="10 uF")
    c.put("LDO", {"1": "GND", "3": "+5V", "2": "+3V3"}, value="AMS1117-3.3, 1 A")
    c.put("C", {"1": "+3V3", "2": "GND"}, value="10 uF")
    c.put("C", {"1": "+3V3", "2": "GND"}, value="10 uF")
    c.put("LED", {"1": "PWR_LED", "2": "GND"}, value="green")
    c.put("R", {"1": "+5V", "2": "PWR_LED"}, value="1k")

    # ---------------- host USB and hubs
    c.heading("HOST USB and HUBS: two FE1.1s cascaded = 7 downstream")
    c.put("USBB", {"1": None, "2": "HOST_DM", "3": "HOST_DP", "4": "GND", "SH": "GND"}, value="USB-B, rear face", note="self-powered: host VBUS not used")
    c.put("FE1.1s", {"VDD33": "+3V3", "VD18": "HUBA_VD18", "GND": "GND", "XIN": "HUBA_XIN", "XOUT": "HUBA_XOUT", "RESET#": "+3V3",
                     "DM0": "HOST_DM", "DP0": "HOST_DP",
                     "DM1": "MCU1_USB_DM", "DP1": "MCU1_USB_DP", "DM2": "MCU2_USB_DM", "DP2": "MCU2_USB_DP",
                     "DM3": "HUBB_UP_DM", "DP3": "HUBB_UP_DP", "DM4": "USBH1_DM", "DP4": "USBH1_DP"},
          value="FE1.1s hub A", note="RESET# pull-up and power-switch pins per datasheet; complete from the reference schematic")
    c.put("C", {"1": "HUBA_VD18", "2": "GND"}, value="1 uF core")
    c.put("XTAL", {"1": "HUBA_XIN", "2": "HUBA_XOUT"}, value="12 MHz")
    c.put("C", {"1": "HUBA_XIN", "2": "GND"}, value="22 pF")
    c.put("C", {"1": "HUBA_XOUT", "2": "GND"}, value="22 pF")
    c.put("FE1.1s", {"VDD33": "+3V3", "VD18": "HUBB_VD18", "GND": "GND", "XIN": "HUBB_XIN", "XOUT": "HUBB_XOUT", "RESET#": "+3V3",
                     "DM0": "HUBB_UP_DM", "DP0": "HUBB_UP_DP",
                     "DM1": "USBH2_DM", "DP1": "USBH2_DP", "DM2": "USBH3_DM", "DP2": "USBH3_DP",
                     "DM3": "USBH4_DM", "DP3": "USBH4_DP", "DM4": "USBR_DM", "DP4": "USBR_DP"},
          value="FE1.1s hub B")
    c.put("C", {"1": "HUBB_VD18", "2": "GND"}, value="1 uF core")
    c.put("XTAL", {"1": "HUBB_XIN", "2": "HUBB_XOUT"}, value="12 MHz")
    c.put("C", {"1": "HUBB_XIN", "2": "GND"}, value="22 pF")
    c.put("C", {"1": "HUBB_XOUT", "2": "GND"}, value="22 pF")
    for i in range(1, 5):
        c.put("F", {"1": "+5V", "2": f"USBH{i}_5V"}, value="polyfuse 500 mA")
        c.put("HDR1x4", {"1": f"USBH{i}_5V", "2": f"USBH{i}_DM", "3": f"USBH{i}_DP", "4": "GND"}, value=f"USB section {i}",
              note="1x4 header to a front USB-A section: VBUS D- D+ GND")
    c.put("F", {"1": "+5V", "2": "USBR_5V"}, value="polyfuse 500 mA")
    c.put("HDR1x4", {"1": "USBR_5V", "2": "USBR_DM", "3": "USBR_DP", "4": "GND"}, value="rear USB-A", note="spare hub port on the back face")

    # ---------------- harness
    c.heading("BUTTONS + OLED HARNESS: straight through to the host")
    harness = {"1": "BTN_PWR", "2": "BTN_RST", "3": "BTN_USR", "4": "BTN_OSD", "5": "LED_PWR", "6": "LED_RST", "7": "LED_USR", "8": "LED_OSD",
               "9": "OLED_VCC", "10": "GND", "11": "OLED_SCK", "12": "OLED_MOSI", "13": "OLED_DC", "14": "OLED_RST", "15": "OLED_CS", "16": "GND"}
    c.put("HDR2x8", harness, value="HARNESS, to buttons segment + OLED", note="front face, slot 0-1")
    c.put("HDR2x8", harness, value="HOST, same pins", note="rear face; a MiSTer I/O board or the T113 board drives buttons, LEDs and the OLED")

    # ---------------- MCUs
    for m in (1, 2):
        c.heading(f"MCU {m}: RP2350B, ports {(m - 1) * PORTS_PER_MCU + 1}..{m * PORTS_PER_MCU}")
        nets = {"IOVDD": "+3V3", "USB_VDD": "+3V3", "ADC_AVDD": "+3V3", "VREG_VIN": "+3V3", "VREG_AVDD": "+3V3",
                "VREG_LX": f"MCU{m}_LX", "VREG_FB": f"MCU{m}_DVDD", "VREG_PGND": "GND", "DVDD": f"MCU{m}_DVDD", "GND": "GND",
                "USB_DP": f"MCU{m}_DP_R", "USB_DM": f"MCU{m}_DM_R", "XIN": f"MCU{m}_XIN", "XOUT": f"MCU{m}_XOUT",
                "QSPI_SCLK": f"MCU{m}_QSPI_SCLK", "QSPI_SS": f"MCU{m}_QSPI_SS", "QSPI_SD0": f"MCU{m}_QSPI_SD0", "QSPI_SD1": f"MCU{m}_QSPI_SD1",
                "QSPI_SD2": f"MCU{m}_QSPI_SD2", "QSPI_SD3": f"MCU{m}_QSPI_SD3", "RUN": f"MCU{m}_RUN", "SWCLK": f"MCU{m}_SWCLK", "SWD": f"MCU{m}_SWD"}
        g = 0
        for p in range((m - 1) * PORTS_PER_MCU + 1, m * PORTS_PER_MCU + 1):
            for k in range(1, 8):
                nets[f"GPIO{g}"] = f"P{p}_M{k}"
                g += 1
        nets[f"GPIO{g}"] = f"MCU{m}_165_LD"; g += 1
        nets[f"GPIO{g}"] = f"MCU{m}_165_CLK"; g += 1
        nets[f"GPIO{g}"] = f"MCU{m}_165_Q"; g += 1
        nets[f"GPIO{g}"] = f"MCU{m}_LED"; g += 1
        c.put("RP2350B", nets, value="RP2350B", note="GPIO0..34 = five ports x seven lines; 35..37 = 74HC165 chain; 38 = LED. Avoid internal pull-downs (RP2350 erratum E9); lines have external 10k pull-ups")
        c.put("L", {"1": f"MCU{m}_LX", "2": f"MCU{m}_DVDD"}, value="3.3 uH, core regulator")
        c.put("C", {"1": f"MCU{m}_DVDD", "2": "GND"}, value="4.7 uF")
        c.put("C", {"1": "+3V3", "2": "GND"}, value="100 nF x6, one per supply pin")
        c.put("XTAL", {"1": f"MCU{m}_XIN", "2": f"MCU{m}_XOUT"}, value="12 MHz")
        c.put("C", {"1": f"MCU{m}_XIN", "2": "GND"}, value="15 pF")
        c.put("C", {"1": f"MCU{m}_XOUT", "2": "GND"}, value="15 pF")
        c.put("R", {"1": f"MCU{m}_DP_R", "2": f"MCU{m}_USB_DP"}, value="27R")
        c.put("R", {"1": f"MCU{m}_DM_R", "2": f"MCU{m}_USB_DM"}, value="27R")
        c.put("W25Q16", {"1": f"MCU{m}_QSPI_SS", "2": f"MCU{m}_QSPI_SD1", "3": f"MCU{m}_QSPI_SD2", "4": "GND",
                         "8": "+3V3", "7": f"MCU{m}_QSPI_SD3", "6": f"MCU{m}_QSPI_SCLK", "5": f"MCU{m}_QSPI_SD0"}, value="W25Q16JV QSPI flash")
        c.put("R", {"1": "+3V3", "2": f"MCU{m}_RUN"}, value="10k")
        c.put("SW", {"1": f"MCU{m}_RUN", "2": "GND"}, value="RESET")
        c.put("SW", {"1": f"MCU{m}_QSPI_SS", "2": "GND"}, value="BOOTSEL", note="hold at power-up for the UF2 bootloader; 1k series in the real layout")
        c.put("HDR1x3", {"1": f"MCU{m}_SWCLK", "2": "GND", "3": f"MCU{m}_SWD"}, value="SWD")
        c.put("LED", {"1": f"MCU{m}_LED", "2": f"MCU{m}_LED_K"}, value="status")
        c.put("R", {"1": f"MCU{m}_LED_K", "2": "GND"}, value="1k")

        # DIP switches and the 74HC165 chain: 5 ports x 4 bits + 4 config bits = 24 bits = 3 chips
        c.heading(f"MCU {m} PORT ID: five DIP4 + config DIP4 on three 74HC165")
        bits = []
        for p in range((m - 1) * PORTS_PER_MCU + 1, m * PORTS_PER_MCU + 1):
            c.put("DIP4", {"1": f"P{p}_ID3", "2": f"P{p}_ID2", "3": f"P{p}_ID1", "4": f"P{p}_ID0", "8": "GND", "7": "GND", "6": "GND", "5": "GND"},
                  value=f"ID P{p}", note="ON = 0. Code table in BACKPLANE.md")
            c.put("RN4", {"1": f"P{p}_ID3", "2": f"P{p}_ID2", "3": f"P{p}_ID1", "4": f"P{p}_ID0", "8": "+3V3", "7": "+3V3", "6": "+3V3", "5": "+3V3"},
                  value="4x10k pull-up")
            bits += [f"P{p}_ID3", f"P{p}_ID2", f"P{p}_ID1", f"P{p}_ID0"]
        c.put("DIP4", {"1": f"MCU{m}_CFG3", "2": f"MCU{m}_CFG2", "3": f"MCU{m}_CFG1", "4": f"MCU{m}_CFG0", "8": "GND", "7": "GND", "6": "GND", "5": "GND"},
              value=f"CFG U{m}", note="firmware options, e.g. 1 kHz vs 250 Hz reports, swap ports")
        c.put("RN4", {"1": f"MCU{m}_CFG3", "2": f"MCU{m}_CFG2", "3": f"MCU{m}_CFG1", "4": f"MCU{m}_CFG0", "8": "+3V3", "7": "+3V3", "6": "+3V3", "5": "+3V3"},
              value="4x10k pull-up")
        bits += [f"MCU{m}_CFG3", f"MCU{m}_CFG2", f"MCU{m}_CFG1", f"MCU{m}_CFG0"]
        ser = "GND"
        for chip in range(3):
            inputs = bits[chip * 8:(chip + 1) * 8]
            q = f"MCU{m}_165_Q" if chip == 2 else f"MCU{m}_165_S{chip + 1}"
            nets165 = {"1": f"MCU{m}_165_LD", "2": f"MCU{m}_165_CLK", "15": "GND", "10": ser, "8": "GND", "16": "+3V3",
                       "11": inputs[0], "12": inputs[1], "13": inputs[2], "14": inputs[3], "3": inputs[4], "4": inputs[5],
                       "5": inputs[6], "6": inputs[7], "9": q}
            c.put("74HC165", nets165, value="74HC165", note="parallel-in serial-out; chained SER<-QH; one LOAD/CLK per MCU")
            ser = q

    # ---------------- ports
    for p in range(1, PORTS + 1):
        pt = (p == PASSTHROUGH_PORT)
        c.heading(f"PORT P{p}: 2x5 ribbon + USB3-A in parallel" + (", with MiSTer pass-through jumpers" if pt else ""))
        c.put("F", {"1": "+5V", "2": f"P{p}_5V"}, value="polyfuse 500 mA")
        c.put("FB", {"1": f"P{p}_5V", "2": f"P{p}_5VF"}, value="ferrite 600R")
        c.put("C", {"1": f"P{p}_5VF", "2": "GND"}, value="10 uF")
        # series resistors MCU -> port, in two 4x100R arrays (one element spare)
        io = (lambda k: f"P{p}_A{k}") if pt else (lambda k: f"P{p}_IO{k}")
        c.put("RN4", {"1": f"P{p}_M1", "8": io(1), "2": f"P{p}_M2", "7": io(2), "3": f"P{p}_M3", "6": io(3), "4": f"P{p}_M4", "5": io(4)},
              value="4x100R series")
        c.put("RN4", {"1": f"P{p}_M5", "8": io(5), "2": f"P{p}_M6", "7": io(6), "3": f"P{p}_M7", "6": io(7)}, value="4x100R series")
        if pt:
            jp = {}
            for k in range(1, 8):
                jp[f"M{k}"] = f"P{p}_A{k}"; jp[f"C{k}"] = f"P{p}_IO{k}"; jp[f"R{k}"] = f"P{p}_R{k}"
            c.put("HDR3x7", jp, value="pass-through select", note="jumper M-C: this port to MCU 2 (default). Jumper C-R: this port to the rear MiSTer socket")
            c.put("USB3A", {"1": None, "2": f"P{p}_R2", "3": f"P{p}_R4", "4": "GND", "SH": "GND", "5": f"P{p}_R6", "6": f"P{p}_R5",
                            "7": f"P{p}_R3", "8": f"P{p}_R1", "9": f"P{p}_R7"},
                  value="TO MISTER user port, rear face", note="A-to-A USB3 cable to the MiSTer I/O board. VBUS deliberately open: the MiSTer must not feed this board")
        # pull-ups on the port side, 10k to 3.3 V (MiSTer user-port semantics)
        c.put("RN4", {"1": f"P{p}_IO1", "2": f"P{p}_IO2", "3": f"P{p}_IO3", "4": f"P{p}_IO4", "8": "+3V3", "7": "+3V3", "6": "+3V3", "5": "+3V3"},
              value="4x10k pull-up")
        c.put("RN4", {"1": f"P{p}_IO5", "2": f"P{p}_IO6", "3": f"P{p}_IO7", "8": "+3V3", "7": "+3V3", "6": "+3V3"}, value="4x10k pull-up")
        c.put("ESD4", {"1": f"P{p}_IO1", "2": f"P{p}_IO2", "3": "GND", "6": f"P{p}_IO3", "5": f"P{p}_IO4", "4": "+3V3"}, value="SRV05-4 class ESD array")
        c.put("ESD4", {"1": f"P{p}_IO5", "2": f"P{p}_IO6", "3": "GND", "6": f"P{p}_IO7", "5": f"P{p}_5VF", "4": "+3V3"}, value="SRV05-4 class ESD array",
              note="fourth channel guards the 5 V pin; VCC pin per datasheet, may be 5 V-tolerant type instead")
        c.put("IDC2x5", {"1": f"P{p}_5VF", "2": "GND", "3": f"P{p}_IO1", "4": f"P{p}_IO2", "5": f"P{p}_IO3", "6": f"P{p}_IO4",
                         "7": f"P{p}_IO5", "8": f"P{p}_IO6", "9": f"P{p}_IO7", "10": "GND"}, value=f"P{p} ribbon", note="2x5 shrouded, pin 1 = +5V")
        c.put("USB3A", {"1": f"P{p}_5VF", "2": f"P{p}_IO2", "3": f"P{p}_IO4", "4": "GND", "SH": "GND", "5": f"P{p}_IO6", "6": f"P{p}_IO5",
                        "7": f"P{p}_IO3", "8": f"P{p}_IO1", "9": f"P{p}_IO7"},
              value=f"P{p} SNAC socket", note="SNAC channel order: IO1=SSTX- IO2=D- IO3=GND_DRAIN IO4=D+ IO5=SSRX+ IO6=SSRX- IO7=SSTX+")

    OUT_DIR.mkdir(exist_ok=True)
    s.write(OUT)
    pro = OUT_DIR / "backplane.kicad_pro"
    if not pro.exists():
        pro.write_text('{\n  "meta": { "filename": "backplane.kicad_pro", "version": 1 },\n'
                       '  "pcbnew": { "page_layout_descr_file": "" }\n}\n')
    n_sym = sum(1 for it in s.items if it.startswith("  (symbol"))
    print(f"wrote {OUT} with {n_sym} symbols")


if __name__ == "__main__":
    build()
