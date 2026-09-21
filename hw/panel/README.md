# hw/panel — the modular controller front panel

Status, 2026-09-21: **generated boards, nothing ordered, no connector measured.**
The design study behind this is `docs/controller-front-panel.md`; this
directory is where it becomes files.

Everything here is produced by one script. Do not edit a `.kicad_pcb` by hand;
edit the tables in `gen_panel.py` and rerun it:

```
python3 hw/panel/gen_panel.py
```

| File | What | Fab as |
|---|---|---|
| `frame.kicad_pcb` | hidden skeleton, 300 × 88 mm, 12 slots; one strip opening behind the module slots, button holes, and an M3 hole pattern at every slot edge | FR4 1.6 mm, any colour |
| `seg_buttons.kicad_pcb` | 2 slots: four 16 mm illuminated buttons with labels | aluminium-core PCB, black mask, white silk |
| `seg_genesis_nes.kicad_pcb` | 2 slots: two DE-9 over two NES | same |
| `seg_saturn_snes.kicad_pcb` | 2 slots: two Saturn over two SNES | same |
| `seg_psx_usb.kicad_pcb` | 2 slots: two PlayStation over four USB-A | same |
| `seg_n64x4.kicad_pcb` | 1 slot: four N64 stacked | same |
| `seg_snac4.kicad_pcb` | 1 slot: four USB3-A sockets wired as MiSTer user ports (§ 3.4) | same |
| `seg_display.kicad_pcb` | 3 slots: window for a 2.42" 128 × 64 OLED | same |
| `seg_blank1`, `seg_blank2` | blanks | same |
| `card_template.kicad_pcb` | outline, row marks and slot header for a module card | start of every module |
| `preview.svg` | the example assembly, drawn from the same numbers | — |
| `panel.kicad_pro` | the KiCad project; open any board from it | — |

![example assembly](preview.svg)

## 1. The scheme

The previous plan soldered every console socket onto one big panel PCB. That
made the whole board hostage to the worst-sourced socket. This version splits
it so that each console family is its own small, independently sourced part,
and the panel and main board never change:

Side view, one slot:

```
  front                                                       rear
  ──────┬─────┬──────────────────────────────────────────────────
        │ seg │ frame                              (top rail, M3 into tray)
        │ment │ ║ module card, parallel to the plate
  plug ▶▶▶▶▶▶▶▶▶╣ straight socket, pins through the card       row 1
        │     │ ║
  plug ▶▶▶▶▶▶▶▶▶╗ right-angle socket on a shelf ═══╣           row 3
        │     │ ║                     (horizontal sub-board)
        │     │ ║ electronics on the back face
        │     │ ╨ 2x8 right-angle header, pins down
  ══════╧═════╧═╩══════════════════════════════════  main board (horizontal)
                hub · power · buttons/OLED harness · one 2x8 socket per slot
```

| Part | Role | Changes when… |
|---|---|---|
| **Segment** (visible plate, `seg_*`) | the face: cutouts for one module's plugs, its labels | a socket is measured, a family is added |
| **Frame** (`frame`) | hidden FR4 skeleton; locates segments, carries the button holes | the panel width changes |
| **Module card** | PCB parallel to the plate, one per module, as wide as its segment: the console sockets on its front face, its electronics on its back face, a 2×8 header on its bottom edge | never for other modules' sake |
| **Shelf** | small horizontal sub-board joined to the card at 90°, for sockets that only exist right-angle | per module |
| **Main board** (next, not here) | USB hub, 5 V distribution, one 2×8 socket per slot, harness header for buttons and display | never for a family's sake |
| **Tray** | 3D-printed or bent sheet; top and bottom rails with M3 inserts | the panel width changes |

Sockets come in whichever mounting style their console used, and the card
takes both. Straight-mount sockets (a vertical DE-9, the NES and SNES port
sockets, which sat on vertical boards behind the console's face) solder
straight through the card. Right-angle sockets (N64, PlayStation and Genesis
sat on their mainboards' front edges) sit on a shelf: a strip of PCB at the
row's height, joined to the card's front face with a row of pins or
castellations, its sockets facing forward. Either way the socket's face ends
up flush with the back of the frame, and the card's 2D layout is free: two
DE-9 stacked, four N64 in a row, or a display module that is nothing but an
OLED on standoffs.

## 2. The grid

| | mm | Why |
|---|---:|---|
| Slot pitch | 25 | a NES or N64 socket beside a 1.6 mm card fits in one slot; DE-9, SNES, Saturn, PlayStation fit in two |
| Panel height | 88 | four socket rows plus rails; 2U is 88.9 |
| Rails | 8 top, 8 bottom | M3 button heads at y = 4 and 84 |
| Screws | M3, Ø3.2 clearance | at 5 mm inside each segment edge, top and bottom: four per segment, on the frame's pattern at 25k+5 and 25k+20 |
| Segment gap | 0.3 total | segments tile at exactly 25 mm per slot |
| Rows | y = 17, 35, 53, 71 | 18 mm pitch; the tallest plug (N64, ~15 mm) leaves 3 mm |
| Frame opening | x 50…295, y 8…80 | everything behind the module slots is open; a 5 mm stile remains at the right |
| Card | 75 tall, segment width less 1 mm each side | top edge 1 mm under the top rail, bottom edge on the main board, front face against the frame |

The screws that hold a segment go through the segment, through the frame, and
into inserts in the tray rails. One screw does all three jobs; the frame is
never load-bearing on its own.

**Sockets sit behind the plate, not in it.** The cutout passes the *plug*; the
socket body is wider than the cutout and bears on the back of the frame during
unplugging, which is how consoles do it. Plug insertion pushes the card
backwards; the card takes that on M3 standoffs from the frame's screw pattern
or on a rear rib in the tray, not on its header. The DE-9 is the exception:
its jack posts pass through both plates and its nuts clamp segment, frame and
card together, so the Genesis segment is the stiffest in the row and the one
to build first.

## 3. The slot

### 3.1 Mechanical

The card stands parallel to the plate with its front face against the back
of the frame, spans its segment's width less 1 mm each side, and is 75 mm
tall. Its bottom edge rests on the main board's top surface. A right-angle
2×8 male header on its bottom edge, pin 1 at 5 mm from the slot's left edge
with the columns running to the right, puts its pins down into a vertical 2×8
female socket on the main board; the pad row nearest the bottom edge is the
odd row. Multi-slot modules have one card and use the header of their
leftmost slot only; the other slot sockets on the main board stay empty. The
header's exact distance behind the frame follows from the chosen header's
geometry and is fixed when the main board is drawn.

### 3.2 Pinout, 2×8 at 2.54 mm

```
   odd row (front)          even row (rear)
   1  +5V                   2  +5V
   3  GND                   4  GND
   5  +3V3                  6  SENSE
   7  USB_D-                8  USB_D+
   9  IO1                  10  IO2
  11  IO3                  12  IO4
  13  IO5                  14  IO6
  15  IO7                  16  GND
```

| Group | Spec |
|---|---|
| +5V | up to 1 A per slot, polyfused on the main board |
| +3V3 | 300 mA per slot, from the main board's regulator |
| USB_D± | one full-speed downstream port of the main board's hub |
| IO1…IO7 | 3.3 V, **open-drain both ways, 10 kΩ pull-up to 3.3 V on the main board**. This is exactly how the MiSTer FPGA drives its user port (`sys_top.v`: each `USER_IO` pin drives 0 or high-Z, verified this session), so a slot's IO set behaves like one MiSTer user port |
| SENSE | resistor to GND that tells the host what module is present; open = "USB module, nothing to poll" |

Two kinds of module can plug into that:

- **USB module.** Carries its own RP2040 (or any MCU) and appears on the hub as
  a HID gamepad device. Uses +5V, GND and USB_D±; leaves IO1…7 unconnected.
  This is the v1 default: each module is a complete console-to-USB adapter, the
  firmware is per family, and the main board is a hub with no MCU at all.
- **Passive module.** No MCU; the sockets are wired through level shifters to
  IO1…7, one SNAC channel each. It needs a host that polls: a MiSTer through
  the SNAC segment (§ 3.4), or a later main board with RP2040s on the IO lines.
  Passive modules are how an FPGA host gets zero-latency native pads.

### 3.3 SNAC channel order

IO1…IO7 follow blue212's SNAC, so a passive module's wiring is the same as
the corresponding SNAC console adapter's. From the SNAC Eagle schematic
(`SNAC 7ch USB3-5v.sch`, read this session; channel n is its `LVn`/`HVn` net):

| Slot pin | Channel | USB3-A receptacle pin (name) |
|---|---|---|
| IO1 | 1 | 8 (StdA_SSTX−) |
| IO2 | 2 | 2 (D−) |
| IO3 | 3 | 7 (GND_DRAIN) |
| IO4 | 4 | 3 (D+) |
| IO5 | 5 | 6 (StdA_SSRX+) |
| IO6 | 6 | 5 (StdA_SSRX−) |
| IO7 | 7 | 9 (StdA_SSTX+); jumpered on the SNAC, the "io6 for SEGA" line in its README |
| +5V | — | 1 (VBUS) |
| GND | — | 4 (GND) |

The USB3-A pin numbers are the connector standard's, from memory; check them
against any USB 3.0 receptacle drawing before the SNAC segment is wired.
**Which `USER_IO[n]` bit each channel is on the MiSTer I/O board is not
verified** — the I/O board release archive holds Gerbers only. It matters for
passive modules driven by MiSTer cores, and for nothing else; measure it once
on an I/O board with a meter and record it here.

### 3.4 The SNAC segment

`seg_snac4` is four USB3-A receptacles, each wired 1:1 to one slot's +5V, GND
and IO1…7 in the table above. Every one of them is then electrically a MiSTer
user port. Plug a blue212 SNAC (the level shifter) in, and any SNAC console
adapter behind it, and a console the panel has no module for yet is covered
by parts other people already make. That is the sourcing escape hatch the
modular design exists for, and it costs one 1-slot segment.

## 4. What is placeholder

Every cutout except the 16 mm button hole is marked `measured=False` in
`gen_panel.py`, and each generated segment says so on its `Cmts.User` layer.
The numbers are plausible enough to check the layout and to print 1:1 on
paper; they are not good enough to order aluminium.

The gate, unchanged from the study: buy two of every socket from two sellers,
measure pin pitch, body, face-to-pin depth and the plug profile, replace the
table entries, rerun, print 1:1, push the real plugs through the paper.

The DE-9 is the one cutout with a standard behind it (D-sub size E: 19.3 wide,
11.0 high, 10° sides, jack-screw holes 24.99 apart, from memory); confirm it
against the datasheet of the part actually ordered.

## 5. Ordering notes

- Segments: JLCPCB "aluminium PCB", 1.6 mm, black solder mask, white silk,
  single layer, no copper needed. Internal cutouts on `Edge.Cuts` are routed;
  keep inside corners ≥ 0.5 mm radius (all are).
- Frame: FR4 1.6 mm, any colour, no copper. At 300 × 88 it is the largest
  board; if the fab's cheap tier caps at 100 mm, split it at slot 6 with a
  lap joint — the tray rails carry it anyway.
- Screws: M3 × 8 button head, black oxide; M3 heat-set inserts in the tray.
- Cards: ordinary 2-layer FR4, ENIG if the card ever becomes a card-edge
  connector instead of a header.

## 6. Next

1. Main board: hub, power, the row of 2×8 sockets on the 25 mm grid, harness
   header. Its front edge sits directly under the cards' bottom edges.
2. First module: **Genesis ×2**, because the DE-9 is a catalogue part with a
   datasheet and STEP model, so the whole mechanical chain (segment, frame,
   card, header, main board) can be proven without waiting on a single
   AliExpress socket.
3. Then the measured sockets, one family per module, in whatever order they
   arrive.
