# A multi-console controller front panel: how it could be built

Design study, 2026-09-21. Status: **nothing measured, nothing ordered, no
copper.** This answers "how would we make a front panel like the MiSTer one in
the photo — the case and the PCB", in enough detail to start buying connectors.

The photographed panel: four illuminated buttons (PWR, RST, USR, OSD), two
Genesis DE-9, two Saturn, two USB-A, two NES, two SNES, two PlayStation, four
N64, and a wide OLED, on a black anodised plate with silkscreen labels and
bracket lines. An illustrative layout at 260 × 88 mm is in
`controller-front-panel.svg`; the connector faces on it are from memory, not
measured, and it exists to show the arrangement, not the dimensions.

Same source tags as `hw/HARDWARE.md`: **[DS]** datasheet, **[C]** community
(nesdev, MiSTer forums, adapter firmware source, from memory in this session —
nothing was fetched), **[I]** engineering judgement, **[GUESS]** a number to be
replaced.

> **Revision, 2026-09-21, later the same day.** Decision 2 below is superseded.
> The console sockets no longer sit on one big panel PCB; each family is its
> own **cassette**: a panel segment, four standoffs and a card parallel to
> the plate carrying the sockets and level shifters, on a 25 mm slot grid,
> joined by a 2×5 ribbon to a **backplane** 80 mm behind the plate. The
> backplane has one SNAC port per slot (a USB3-A socket and the ribbon header
> in parallel), the RP2040s that speak the console protocols, the hub, power
> and the button and display harness. A cassette is electrically a MiSTer
> SNAC adapter, so bought adapters mount behind a blank segment and cover any
> console the panel has no module for. The reason is sourcing: a hard-to-find
> socket now delays one cassette, not the panel. The generated KiCad boards,
> the port pinout and the grid live in `hw/panel/` (start with its README);
> the port survey and the 80 mm check are `port-atlas.html`. Decision 1
> stands: the MCUs are on the backplane, one per four ports. §§ 3, 7 and 8
> below describe the earlier single-board version and are kept as the
> reasoning record.

## 0. The two decisions everything else follows from

**1. The ports are bridged to USB HID by a microcontroller on the panel PCB;
the host sees one USB cable.** Not native pass-through to an FPGA, not fourteen
off-the-shelf adapters. See § 2 for the alternatives and why.

**2. The panel PCB is horizontal, the connectors are right-angle through-hole
along its front edge, and the front plate is a second PCB in the same KiCad
project.** The plate is manufactured as an aluminium-core PCB with black solder
mask and white silkscreen — that is how the labels, the bracket lines, and the
cutouts all come out of one $10 order. See § 7 and § 8.

Everything else — MCU choice, level shifting, the hub, the enclosure — is
downstream of those two.

## 1. What the photo is, electrically

Seven connector families, three voltages, four protocols:

| Family | Ports | Pins used | Logic level | Protocol | Bit timing | Lines to the MCU |
|---|---:|---|---|---|---|---|
| NES | 2 | GND, CLK, LATCH, D0, D3, D4, +5 V | 5 V CMOS (CD4021 in the pad) | 8-bit shift register; console pulses LATCH then clocks 8 bits | slow, any rate under ~100 kHz works | 2 out (CLK, LATCH), 3 in (D0, D3, D4) |
| SNES | 2 | +5 V, CLK, LATCH, DATA, IOBIT, NC, GND | 5 V CMOS | same, 16 bits | same | 2 out, 2 in |
| Genesis / Mega Drive | 2 | DE-9: U, D, L, R, +5 V, A/B, TH (select), GND, Start/C | 5 V (74HC157 mux in the pad) | parallel, TH selects one of two nibble sets; 6-button decodes a burst of TH pulses within ~1.5 ms; Atari 2600 sticks are a subset | TH pulses at a few µs each | 1 out (TH), 6 in |
| Saturn | 2 | VCC, D1, D0, S0, S1, +5 V sense, D3, D2, GND | 5 V | parallel, S0/S1 select one of four nibbles | slow | 2 out, 4 in |
| PlayStation | 2 | DATA, CMD, +7.6 V (motor), GND, +3.3 V, ATT, CLK, NC, ACK | 3.3 V (3.5 V on a real PS1) | SPI-like, 250 kHz (PS2 up to 500 kHz), LSB first, ACK pulse after each byte; DATA is open-collector | 4 µs per bit | 3 shared (CLK, CMD, DATA) + 2 per port (ATT, ACK) |
| N64 | 4 | GND, DATA, +3.3 V | 3.3 V, single wire, open-drain | Joybus: 4 µs per bit, a 0 is 3 µs low + 1 µs high, a 1 is 1 µs low + 3 µs high; host sends 0x01 for a poll | 1 µs edges | 1 bidirectional per port |
| USB-A | 2 | standard | — | straight to the hub | — | 0 |

All pinouts [C], from nesdev and the adapter firmware ecosystem, from memory.
**Every one of them gets re-checked against a second source and a multimeter
on the real socket before the schematic is drawn.** Getting a +5 V and a GND
swapped on a Genesis port kills a controller.

Two things the table hides:

- **The 5 V families need real level shifting in both directions.** The
  CD4021 in a NES pad at 5 V wants VIH ≈ 3.5 V, so a 3.3 V MCU driving CLK and
  LATCH directly is out of spec [DS, CD4021 at VDD = 5 V, from memory]. And no
  RP2040 or RP2350 GPIO is 5 V tolerant [DS]. So: 74AHCT125 / 74AHCT244 powered
  at 5 V for the MCU-to-pad outputs (TTL thresholds accept a 3.3 V high), and
  74LVC245 powered at 3.3 V (5 V-tolerant inputs) for the pad-to-MCU inputs.
  Directions are fixed per pin, so the auto-direction TXS0108 is not needed and
  its weak-pull-up flakiness is avoided [I].
- **N64 and PlayStation are timing-critical at the microsecond level.** That is
  what decides the MCU (§ 3): the RP2040's PIO does Joybus and the PS bus in
  hardware, and open-source PIO programs for both exist [C].

## 2. Architecture: three ways to do it, and the pick

| | A. USB HID bridge on the panel (**pick**) | B. Native pass-through (MiSTer SNAC style) | C. Fourteen retail adapters behind a plate |
|---|---|---|---|
| What the host sees | one USB cable, N gamepads | raw controller lines on GPIO; the core drives them | N USB devices via a hub |
| Host coupling | none — works on MiSTer, a Pi, a PC, the T113 | FPGA only, and only cores with SNAC support | none |
| Latency | ~1 ms (1 kHz HID polling) on top of the host's own | zero | ~1 ms, plus whatever each adapter does |
| Panel electronics | 1–2 MCUs, hub, shifters | shifters and a wide connector, no MCU | none, just mounting |
| Cost of the electronics [GUESS] | $10–15 | $5 | $150–250 |
| Firmware | ours, but built from existing PIO/HID code | none | none |
| Weak point | firmware effort; it is a product, not a plate | host lock-in; SNAC handles one or two ports at a time, not fourteen | cost, thickness, twelve cables inside the box |

A wins because it makes the panel a self-contained peripheral. It is also what
most of the multi-port panels in the MiSTer community actually are inside:
Daemonbite-class adapters on a hub [C].

**Option B is not lost.** Two ports' worth of raw lines (the Genesis pair, say)
can be brought to a 2×6 header before their level shifters, with the MCU lines
isolated by a jumper block or a 74LVC4066-class switch, for a SNAC-style
zero-latency path on an FPGA host. That is a v2 feature; the pads and header
cost nothing on v1 and are worth placing.

## 3. The MCU and the line budget

Raw count from § 1: 2×5 + 2×4 + 2×7 + 2×6 + 2×5 + 4 = 58 lines. Too many for
one RP2040 (30 GPIO). Three reductions, all legitimate because we poll every
port in lockstep:

| Share | Saves | Cost |
|---|---|---|
| One CLK and one LATCH for all four NES/SNES ports (each pad only ever loads and shifts; the outputs fan out through one AHCT244) | 6 | none |
| One TH for both Genesis ports, one S0/S1 pair for both Saturn ports | 3 | none; both ports read on the same phase |
| One CLK/CMD/DATA bus for both PlayStation ports, ATT and ACK per port | 3 | none; that is how the console does it |

Result: **46 lines.** Two ways to house them:

| | 1 × RP2350B | 2 × RP2040 |
|---|---|---|
| GPIO | 48 | 30 each |
| PIO state machines | 12 | 8 each |
| USB | one device, composite HID with 14 gamepad interfaces | two devices behind the hub |
| Firmware | one image | one image, two build configurations (which families I own) |
| Cost [GUESS] | ~$1.3 + flash | ~$1.6 + 2 flash |
| Risk | 46 of 48 pins used; no slack for a SNAC switch or debug | none; buckets fall out naturally: A = NES, SNES, Saturn (22 lines); B = Genesis, PlayStation, N64 (24 lines) |

**Pick 2 × RP2040 for v1** [I]: the pin slack, the larger firmware ecosystem,
and the fact that the split also lets a stripped-down panel be built with one
MCU stuffed. Move to one RP2350B if a v2 wants the board smaller.

Each RP2040 needs its 12 MHz crystal, a W25Q16-class QSPI flash, 3.3 V and
1.1 V rails (the on-chip 1.1 V regulator suffices) and a USB-BOOT button or
pads. Copy the RP2040 hardware design note's minimal schematic verbatim [DS];
it is the reference design, and this project's rule is to copy, not derive.

## 4. Hub, power and protection

**Hub.** A 4-port USB 2.0 hub IC on the panel: FE1.1s or SL2.1A (both under
$0.50 and on every cheap hub) or Microchip USB2514B if a datasheet with an
English reference schematic is worth the extra $2 [V, from memory]. Downstream
ports: MCU A, MCU B, the two front USB-A jacks. Upstream: one internal USB-B
or a 4-pin header to the host. The MCUs are full-speed devices, so only the
hub's upstream pair carries 480 Mbit/s; route that one as a 90 Ω pair over a
ground plane and stop worrying.

**Power.** Do not run fourteen ports from a host USB port's 500 mA. A
DualShock with both motors on draws hundreds of mA; a Rumble Pak more. The hub
is configured self-powered and the panel takes **5 V at 2 A from the host's
supply** on a 2-pin JST or barrel, with:

- a 2 A polyfuse on the input, then a 500 mA polyfuse per family group of ports,
- 10 µF bulk per port, ferrite on the 5 V to each connector,
- a 3.3 V LDO (or a buck if the budget allows) rated for 1 A: the MCUs, N64
  and PlayStation ports all sit on it,
- an **optional MT3608-class boost to 7.6 V** for PlayStation pin 3, jumper
  selectable to plain 5 V. Rumble works at 5 V, weaker [C]; the boost is the
  correct answer and costs $0.40.

**ESD.** Every one of these ports is hot-plugged by hand. A TVS array per
connector (PRTR5V0U2X or the 4-line SP3012/USBLC6 class), 100 Ω series
resistors on every data line, and on the N64 lines a 1 kΩ pull-up to 3.3 V
because the bus is open-drain and the console's own is 1 kΩ [C].

## 5. The buttons and the display

These are **not** routed through USB. They belong to the host and are wired
straight to it:

- Four 16 mm anti-vandal illuminated momentary buttons, panel-mount, ring LED.
  Each goes to a 4-pin JST-XH on the PCB (two switch, two LED) and from there to
  a 10-pin harness header for the host. On a MiSTer that is the I/O board's
  button and LED header; on the T113 board it is GPIO. The panel PCB does
  nothing but pass them through and provide LED series resistors.
- The OLED is the 3.12" 256 × 64 SSD1322 SPI module (the size that matches the
  photo's window; the 2.42" 128 × 64 SSD1309 is the cheaper alternative) [C].
  It mounts on standoffs behind the window and its 7-pin header goes to the
  host over the same harness. MiSTer drives this as `tty2oled` over a serial
  link [C]; if the host has no free UART, one of the RP2040s can expose a USB
  CDC port and forward it — a firmware feature, not a board change, so the
  OLED header gets pads to both.

## 6. Firmware

TinyUSB on the RP2040, one composite device per MCU with one HID gamepad
interface per port, 1 kHz polling. Each family is one PIO program plus a
decoder:

| Family | Reads | Existing code to start from [C, licences unverified] |
|---|---|---|
| NES / SNES | 8/16-bit shift; one PIO SM clocks all four | trivial; dozens of Pico examples |
| Genesis | TH pulse burst on a timer, sample the six lines at each phase; 3-button, 6-button and Mega Mouse | Daemonbite (AVR, GPL) has the decode tables; retranslate |
| Saturn | four S0/S1 phases | same |
| PlayStation | PIO SPI mode-3 LSB-first at 250 kHz with ACK wait; 0x01 0x42 poll, analogue mode enable, DualShock 1/2 | PsxNewLib logic (Arduino); several Pico ports exist |
| N64 | Joybus PIO | `joybus-pio` (MIT) and the GameCube-adapter family; GameCube pads are the same bus with +5 V rumble, so a GC port is a free addition |

Rules that avoid the usual failures:

- **Every port is always enumerated.** No hot-plug detection on the USB side:
  a NES port with nothing in it reads 0xFF and reports "no buttons". Hosts cope
  with idle gamepads; they do not cope with devices appearing and vanishing.
- **Per-family sample rate, not per-frame.** Poll the shift-register pads at
  1 kHz; poll PlayStation and N64 at their native ~250–1000 Hz; never block one
  family's PIO on another's.
- **Report descriptors match what MiSTer's and Linux's HID parsers already
  map** — a plain 16-button, 2-axis (or 4-axis for DualShock/N64 stick) gamepad
  per interface, with a distinct product string per port ("NES 1", "N64 3") so
  the host's controller assignment is stable.

## 7. The PCB

**One board, horizontal, 4-layer, roughly 250 × 80 mm** [GUESS], sitting on
standoffs in the bottom of the box. 2-layer is possible; 4-layer is about $15
more at JLCPCB for five and buys a ground plane under fourteen ESD entry points
and a HS USB pair, which is the right trade [I].

Placement rules, in priority order:

1. **All connectors on the front edge, right-angle, through-hole.** Nothing
   surface-mount faces the user; a plugged N64 pad is a lever.
2. **The MCUs, hub and shifters in the rear third.** Short lines from shifter
   to connector, one TVS array per connector right at the pins.
3. **The board's front edge sits at one fixed setback behind the plate.** Every
   connector's face-to-pin-row dimension is different, so pick the setback for
   the families that must be flush (DE-9, whose jack posts also anchor the
   plate) and let the others sit **recessed up to ~3 mm behind their cutouts.
   Flush everywhere is a non-goal**; the recess hides tolerance, and the photo's
   N64 ports visibly sit back.
4. Buttons and the OLED are off-board; their headers go along the front edge
   next to their positions so harnesses are short.

**The connector-geometry gate, which is the real schedule risk.** None of the
NES, SNES, N64, Saturn or PlayStation sockets is a catalogue part with a
datasheet. They are console replacement parts from AliExpress-class sellers,
in several incompatible variants each (pin pitch, lug position, body depth),
and they change without notice. Therefore, before any layout:

1. Buy two of every socket, from two sellers. Under $30 total [GUESS].
2. Measure each with calipers: pin pitch and row offset, body width, height,
   face-to-pin-row depth, lug positions, and the cutout its face needs.
3. Make a KiCad footprint **and a STEP model** for each, from the measured
   part, and put a photo and the measurements in `hw/connectors/`.
4. Only then place the front edge.

The DE-9 male (Genesis) and the USB-A jacks are standard catalogue right-angle
parts with proper footprints and models; they anchor the mechanical reference.

**The front plate is a second board in the same KiCad project.** Its
`Edge.Cuts` layer holds the outline and every cutout, generated by projecting
the connectors' 3D models onto the plate plane; its silkscreen holds the labels
and bracket lines; its mounting holes coincide with the DE-9 jack-post and
button positions. Because it is a board, DRC checks that a cutout does not
cross a hole, and the fab routes the cutouts. That is what makes § 8 cheap.

## 8. The case

Three tiers, cheapest first. All three share the same panel PCB and the same
plate design; only the plate's material and the box change.

| Tier | Front plate | Box | Cost for one [GUESS] | When |
|---|---|---|---|---|
| **1. Prototype** | **the plate PCB, fabbed as a 1.6 mm FR4 board**, black mask, white silk, cutouts routed | 3D-printed FDM tray with a slot that captures the plate and standoffs for the PCB; open or lidded | $10 plate + $10 filament | first bring-up; proves cutouts, setback and recess |
| **2. Metal panel** | **the same plate design, fabbed as an aluminium-core PCB**, black mask, white silk (JLCPCB offers this on 1.0/1.6 mm aluminium) | same 3D-printed tray, or a bent-sheet U from a sheet-metal service | $15 plate | the photographed look for almost nothing; the "MiSTer FPGA" vertical wordmark is silkscreen |
| **3. Product** | 2 mm anodised aluminium, laser-cut and laser-engraved or UV-printed (Front Panel Express / Schaeffer, SendCutSend, JLC CNC + anodise) | custom bent-sheet U-shell with lid, powder-coated or anodised; or a commercial extrusion if the width fits | $60–200 | if it ships |

Notes on the tiers:

- **Tier 2 is the recommendation.** An aluminium-core PCB is a real metal
  plate; its solder mask is glossy rather than anodised-matte, and silkscreen
  is coarser than laser engraving, but at arm's length it reads like the photo,
  and its cutouts are exact by construction because they came from the 3D
  layout. The one thing it cannot do is countersunk or tapped holes: the plate
  is held by the DE-9 jack posts, the button nuts and the corner screws into
  the tray, which is plenty.
- **The extruded enclosure trick does not fit this panel.** Hammond 1455-class
  boxes (PCB slides into side slots, custom end plate screws on) are the ideal
  home for a right-angle-connector panel, but their widest is about 165 mm
  [V, from memory] and this layout wants 250. A **two-row, half-width panel
  (seven ports, 160 mm)** would fit a 1455T and is worth keeping as a variant;
  it is also the natural companion size for the MT-32 module's own box.
- **The OLED window** is an open cutout with a tinted acrylic window glued
  behind the plate (tier 3) or simply the module's own glass showing through
  (tiers 1 and 2). Recess the module 1 mm so the glass is protected.
- **Buttons** are panel-mount and self-fixing (16 mm hole, nut behind), which
  is also why they carry the plate's left end mechanically.

## 9. BOM, first pass [GUESS, September 2026]

| Item | Qty | Est. |
|---|---:|---:|
| RP2040 + crystal + W25Q16 flash | 2 | $3.0 |
| USB hub IC (FE1.1s class) + crystal | 1 | $0.8 |
| 74LVC245 (5 V-tolerant inputs) | 4 | $0.8 |
| 74AHCT244 / AHCT125 (3.3 → 5 V outputs) | 2 | $0.6 |
| 3.3 V regulator, 7.6 V boost, polyfuses, TVS arrays, passives | — | $5 |
| DE-9 male right-angle with jack posts | 2 | $1.5 |
| NES socket | 2 | $3 |
| SNES socket | 2 | $3 |
| Saturn socket | 2 | $8 |
| PlayStation socket | 2 | $4 |
| N64 socket | 4 | $6 |
| USB-A right-angle | 2 | $0.6 |
| 16 mm illuminated anti-vandal buttons | 4 | $10 |
| 3.12" SSD1322 OLED module | 1 | $18 |
| Panel PCB, 4-layer, qty 5 | 1 of 5 | $12 |
| Front plate, aluminium-core PCB, qty 5 | 1 of 5 | $4 |
| JST harnesses, standoffs, screws | — | $5 |
| **Electronics and plate, one unit** | | **≈ $85** |
| 3D-printed tray | | +$10 |
| Tier-3 anodised plate and bent shell | | +$60–200 |

Saturn sockets are the outlier in both price and availability; the design
should let the Saturn pair be unstuffed and its cutouts blanked without
changing anything else.

## 10. What to do first, in order

1. **Buy the sockets** (§ 7 gate). Two of each, two sellers. This is the long
   pole and it is a $30 purchase; nothing else can start honestly without it.
2. **Prove the two hard protocols on a Pico** on a breadboard: an N64 pad over
   Joybus PIO, a DualShock over PIO SPI. If both poll at 1 kHz with a
   real pad, the MCU decision is closed. One evening each with existing code.
3. **Prove the 5 V shifting** with one NES pad through an AHCT125 and LVC245.
   This checks the CD4021 VIH claim in § 1 against a real pad, and its
   third-party clones, which are the ones that misbehave.
4. **Measure, model, place.** Footprints and STEP for every socket; the panel
   board and the plate board in one KiCad project; export the plate's DXF and
   check it against the sockets by printing it 1:1 on paper and pushing the
   plugs through.
5. **Order tier 1**: FR4 plate, 4-layer panel, 3D-printed tray. Bring up one
   family at a time.
6. Order the aluminium-core plate only after tier 1 has proven the cutouts.

## 11. Open questions

- **Which host, first?** MiSTer wants the buttons on its I/O board header and
  the OLED as `tty2oled`; the T113 module wants both on GPIO. The harness
  header can carry either, but the OLED-forwarding CDC feature only matters
  for one of them. Decide before firmware.
- **Analogue sticks and rumble as HID output reports** (N64 Rumble Pak,
  DualShock motors): do we drive rumble from the host's force-feedback output,
  or leave it off in v1? Off in v1 [I]; the boost regulator's pads stay.
- **GameCube instead of, or in addition to, one N64 port?** Same Joybus, same
  code, one more replacement-part socket to source.
- **Does a half-width, two-row, seven-port variant** (Hammond 1455T, § 8) earn
  a place in the MT-32 module's own enclosure family? It would make the MIDI
  box and the controller box the same product line.
