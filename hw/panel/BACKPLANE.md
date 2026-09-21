# The backplane

Rev 0.1, 2026-09-21. Status: **schematic generated, not reviewed in KiCad,
nothing routed, no part ordered.** The circuit is `gen_backplane_sch.py`; it
writes `backplane/backplane.kicad_sch`. The board outline and connector
positions are `backplane/backplane.kicad_pcb` from `gen_panel.py`.

The backplane is the only intelligent board in the panel. Cassettes are
sockets and level shifters; this board speaks every console protocol,
presents the pads as USB gamepads, and passes the buttons and display through
to the host.

## 1. Blocks

```
  5 V in ─ fuse ─ reverse FET ─┬─ +5V ──┬─ 10 x (polyfuse, ferrite) ─ port +5V
                               │        └─ 5 x polyfuse ─ USB headers
                               └─ LDO ─ +3V3 ─ MCUs, hubs, pull-ups

  host USB-B ─ hub A ─┬─ MCU 1 (RP2350B) ── ports P1..P5 ── 7 lines each ──┐
                      ├─ MCU 2 (RP2350B) ── ports P6..P10 ─ 7 lines each ──┤  each line:
                      ├─ hub B ─┬─ USB section headers 2, 3, 4              │  100 R series,
                      │         └─ rear USB-A                                │  10 k pull-up to 3.3 V,
                      └─ USB section header 1                               │  ESD array,
                                                                            │  2x5 ribbon header
  74HC165 chain per MCU ◀── five DIP4 (port ID) + one DIP4 (config)         │  and USB3-A socket
                                                                            │  in parallel
  harness 2x8 (front) ══ straight through ══ host 2x8 (rear)                │
  P10 lines ── 3x7 jumper block ── rear USB3-A "TO MISTER"                 ─┘
```

| Block | Parts | Notes |
|---|---|---|
| Power in | DC jack, 3 A polyfuse, AO3401 reverse-polarity P-FET, SMBJ5.0A, 100 µF | 5 V from the host's supply; the host USB VBUS is not used (self-powered hub) |
| 3.3 V | AMS1117-3.3 | 1 A is plenty: two MCUs, two hubs, 70 pull-ups |
| Hubs | 2 × FE1.1s cascaded | 7 usable downstream: MCU 1, MCU 2, four USB section headers, one rear USB-A. Each FE1.1s wants its 12 MHz crystal and a 1 µF on VD18; RESET#, power-switch and LED pins to be completed from the vendor reference schematic |
| MCUs | 2 × RP2350B, W25Q16 flash, 12 MHz crystal, 3.3 µH for the core regulator, 27 Ω USB series, RUN and BOOTSEL buttons, SWD header, status LED | 48 GPIO each: 35 for five ports, 3 for the DIP chain, 1 LED, 9 spare. 12 PIO state machines each, one per port protocol with room over |
| Port, ×10 | 500 mA polyfuse, ferrite, 10 µF; 2 × 4×100 Ω series arrays; 2 × 4×10 k pull-up arrays; 2 × SRV05-4 class ESD arrays; 2×5 shrouded header; USB3-A socket | ribbon header and socket are wired in parallel: the same seven nets |
| Port ID | 4-position DIP per port, 4×10 k pull-up array, three 74HC165 per MCU | § 3 |
| Harness | 2 × 2×8 headers, front and rear, pin for pin | four buttons, four button LEDs, seven-wire SPI OLED; nothing on this board touches them |
| Pass-through | 3×7 jumper block on port 10, rear USB3-A socket | § 4 |

## 2. Why RP2350B and not RP2040

Ten ports at seven lines is 70 GPIO. Two RP2040 give 60, so it would take
three, plus a hub port and a flash and a crystal each. Two RP2350B (48 GPIO,
12 PIO state machines) take five ports each with nine pins spare, and the
PIO count matters: N64, GameCube, PlayStation and Dreamcast each want a state
machine per port. The RP2350 has no 5 V-tolerant pins either, so nothing
changes electrically. One erratum matters: **E9**, where an input with the
internal pull-down enabled can latch at about 2 V. Every line here has an
external 10 k pull-up and the firmware must never enable internal pull-downs.

If RP2350B is unobtainable, the schematic degrades to three RP2040 at three,
three and four ports; the port tables in the generator are per-MCU counts
and nothing else changes.

## 3. The DIP switches, and what they are for

A ribbon has ten wires: 5 V, two grounds, seven IO. There is no pin left to
tell the backplane what kind of cassette is on the other end, and the seven
lines cannot always tell it either: a NES pad and a SNES pad are both a
shift register that answers to any clock, a Genesis pad is a multiplexer that
answers to nothing, and an empty port looks like a pad with no buttons held.
N64, GameCube and PlayStation pads do answer an identify command, but a
scheme that probes three families and guesses the rest is a scheme that
misidentifies a port on the day it matters.

So each port has a 4-position DIP switch next to its header, read once at
boot, that tells the firmware **which protocol to run on those seven lines
and which gamepad descriptor to report** for that port. The firmware then
loads the matching PIO program on that port's lines. Sixteen codes, ON = 0:

| Code | Cassette | Lines used | Reports as |
|---:|---|---|---|
| 0 | nothing plugged in | — | port idle, no HID interface |
| 1 | NES, 1 pad | CLK, LATCH, D0, D3, D4 | 8-button gamepad; D3/D4 as extra buttons for the Zapper and expansion pads |
| 2 | SNES / SFC, 1 pad | CLK, LATCH, DATA, IOBIT | 12-button gamepad |
| 3 | Genesis / Master System / Atari, 1 pad | all 7 | 3-button, 6-button (auto-detected by the TH burst) or 1-button stick |
| 4 | Saturn, 1 pad | D0..D3, S0, S1 | 9-button gamepad |
| 5 | PlayStation, 1 pad | DATA, CMD, ATT, CLK, ACK | DualShock: two sticks, 14 buttons, pressure ignored |
| 6 | N64, up to 4 pads | one line each on IO1..IO4 | four 14-button gamepads with a stick |
| 7 | GameCube, up to 4 pads | one line each on IO1..IO4 | four gamepads, two sticks, analogue triggers |
| 8 | PC Engine, 1 pad | D0..D3, SEL, CLR | 8-button; 6-button pads by the SEL toggle |
| 9 | Dreamcast, 1 pad | SDCKA, SDCKB | gamepad with stick and analogue triggers |
| 10 | NES, 2 pads sharing CLK and LATCH | CLK, LATCH, D0a, D0b, D3, D4 | two gamepads on one ribbon |
| 11 | SNES, 2 pads sharing CLK and LATCH | CLK, LATCH, DATAa, DATAb | two gamepads on one ribbon |
| 12 | PlayStation, 2 pads sharing the bus | CLK, CMD, DATA, ATTa, ACKa, ATTb, ACKb | two DualShocks on one ribbon |
| 13 | spare | | |
| 14 | spare | | |
| 15 | passed through to the MiSTer (§ 4) | — | firmware leaves the lines high-impedance |

Codes 10 to 12 are why the cassette segments in `gen_panel.py` can carry two
sockets on one ribbon: the shift-register and PlayStation families share
their clock lines exactly as the consoles did. Genesis and Saturn cannot
share a ribbon; a two-port Genesis cassette uses two ribbons and two ports.

The switches are read through a chain of three 74HC165 shift registers per
MCU on three GPIO, which is why 24 switch positions cost three pins and not
24. The four positions left over on each chain are a config DIP for firmware
options: report rate, port order, a debug mode.

The switch is on the backplane and not on the cassette because that is where
a pin was free. A later revision could move the code into the cassette as a
resistor on a reserved line, at the price of one of the seven lines, which
only Genesis needs all of.

## 4. Pass-through to a real MiSTer

Port 10's seven lines pass through a 3×7 jumper block. Row M is MCU 2 after
its series resistors, row C is the port itself, row R is a USB3-A socket on
the back face wired as a MiSTer user port. Seven jumpers M-C is the normal
state; moved to C-R, that port's cassette is driven by the FPGA at zero
latency over an A-to-A USB3 cable, exactly like a SNAC adapter, and MCU 2
must be told to leave the lines alone (DIP code 15). The rear socket's VBUS
pin is deliberately unconnected so the MiSTer's 5 V never meets ours.

Which `USER_IO` bit each of the seven lines lands on at the MiSTer end is not
verified (README § 3.2). The channel order is the SNAC's, so a cassette that
works behind a blue212 SNAC works here.

## 5. What is not in the schematic yet

- FE1.1s support pins beyond power, crystal and USB: RESET# pull-up value,
  the per-port power-switch and overcurrent pins, LED pins. Take them from
  the FE1.1s reference schematic; every $2 hub board is one.
- RP2350B decoupling is drawn as one capacitor standing for six. Package pin
  numbers are absent by design: the symbol's pins are named, and assigning
  the vendor footprint fills them in. Same for the FE1.1s.
- ESD array pin numbers follow the common SOT-23-6 four-channel layout and
  must be checked against the exact part; a 5 V-tolerant type is needed on the
  channel that guards the port's 5 V pin.
- BOOTSEL should have a 1 k in series with the QSPI_SS pull-up in layout.
- No connector for the pass-through's 5 V, on purpose.
- Nothing is placed or routed. The PCB has the outline, the rail holes and
  the port connectors on the grid; everything else is in the "MCU / hub /
  power zone" strip along the bottom.

## 6. Next

1. Open `backplane/backplane.kicad_pro` in KiCad, run ERC, assign footprints
   (RP2350B QFN-80, FE1.1s SSOP-28, the rest 0603/SOT), and let the
   symbol-to-footprint mapping give the ICs their pin numbers.
2. Complete the two hub chips from the FE1.1s reference schematic.
3. Route as 4-layer: ground plane under the ten port entries and the hub's
   upstream pair; MCUs in the bottom strip, ports on the 25 mm grid as
   already placed.
4. Firmware skeleton: TinyUSB composite, one HID interface per active port,
   a PIO program per DIP code, the 74HC165 read at boot.
