# hw/ — the carrier board, as an interface contract

Workstream B, second pass. Date of everything below: **2026-09-18**.
Read `hw/HARDWARE.md` first — this document assumes its findings, and § 11 lists
the places where it corrects them.

`hw/HARDWARE.md` ends at a wall: the standing decision is to buy a vendor SoM
(Forlinx FET113i-S or MYIR MYC-YT113i) rather than lay out the T113-i BGA
ourselves, and **the SoM pinouts and the power-sequencing document are not
reachable from this environment**, so a schematic cannot be finished here.

This document does not paper over that. It does two things instead:

1. **§§ 2–3: the carrier expressed as nets and requirements, parameterised on
   the SoM pinout rather than pretending to know it**, plus a single table (§ 3)
   of every fact that must be read off a vendor document before copper — what
   holds it, and what breaks if it is wrong. That table is meant to be filled in
   in one sitting by somebody with a network that works.
2. **§§ 4–9: the parts that do not depend on the SoM at all**, at component
   level with values and arithmetic — MIDI in, MIDI THRU, the analogue output
   stage, the WaveBlaster connector, and the power budget. These are derivable
   from public standards and part behaviour and they are *finished*.

## 0. Evidence tags

`hw/HARDWARE.md` § 0 defines `[DS]`, `[V]`, `[C]`, `[I]`, `[GUESS]`. This
document keeps them and splits two of them, because the split matters:

| Tag | Means |
|---|---|
| `[DS]` | From a datasheet or a published standard that **I read myself**. In this document: *nothing*. Every vendor and standards domain is blocked (§ 10) |
| `[DS-X]` | Datasheet or standard content obtained by **search-engine extraction of a document I could not open**. A weaker class than `[DS]` and tracked separately, exactly as `docs/HISTORY.md` § 8 insists |
| `[C-A]` | **Community artefact read directly** — an open-hardware design's netlist or schematic file, cloned from GitHub and parsed here. Not a vendor document, but not hearsay either: it is what somebody actually fabricated. The strongest class available in this session |
| `[C]` | Community text: forum post, wiki, blog, search summary |
| `[I]` | My inference. The reasoning is shown |
| `[OPEN]` | Not known. Nobody may assert it. § 3 lists these |

Arithmetic is always shown. Every resistor and capacitor value below is either a
consequence of arithmetic on this page or is marked `[OPEN]`.

---

## 1. Findings first

Four of these change something. Two of them are negative and one of them is a
correction to `hw/HARDWARE.md`.

### 1.1 The power budget does **not** kill the daughterboard. It imposes an inrush requirement

Full working in § 9. The short version:

| | Watts at 5 V | mA at 5 V |
|---|---:|---:|
| SoM, CPU + DRAM + storage stress `[DS-X]` | 1.05 | 210 |
| Carrier 3.3 V load (DAC, opto, SD, misc), through an 85 %-efficient buck | 0.61 | 123 |
| **Steady total** | **1.66** | **333** |
| SoM no-load *starting peak* `[DS-X]` | 3.25 | 650 |
| **Startup total** | **3.86** | **773** |

Against that, the only field evidence of what a WaveBlaster header actually
supplies: **the Yamaha DB50XG is specified at 2 W** `[DS-X, Yamaha owner's
manual via search extraction]`. That is a mass-market board Creative's header was
designed to host.

So: **our steady 1.66 W sits inside a proven 2 W envelope with 0.34 W to
spare — and our 3.86 W startup transient sits outside it.** That is not fatal,
because the 2 W is a continuous rating and the 3.25 W is a millisecond-scale
turn-on peak, but it makes **inrush limiting a hard requirement on the
daughterboard build, not a nicety** (§ 9.4). Uncontrolled, a 100 µF bulk
capacitor charging through 0.1 Ω of trace is a 50 A first-instant peak; the
same capacitor behind a load switch slewed over 5 ms draws
100 µF × 5 V / 5 ms = **100 mA**.

What would overturn this verdict, in order of likelihood: the SoM's real
turn-on profile (the 3.25 W figure is "no-load starting peak" from a vendor
table I could not open); whether the host card puts a fuse, a ferrite or a thin
trace between the bus +5 V and the header; and whether our SoM choice idles
higher than Forlinx's number. All three are rows in § 3.

### 1.2 `hw/HARDWARE.md` § 6.2's form-factor objection is retired

§ 6.2 says the one thing that would change its mind about the SoM route is "if
the SoM's 37 × 39 mm footprint plus connectors cannot be made to fit the
WaveBlaster form factor (~63 × 38 mm)".

**There is no ~63 × 38 mm WaveBlaster form factor.** No outline specification
for a WaveBlaster daughterboard was found in any reachable source. What exists
is a range of shipped boards:

| Board | Size | Source |
|---|---|---|
| Yamaha DB50XG | **139 × 89 × 15 mm** | `[DS-X]` Yamaha owner's manual, specifications page |
| DreamBlaster X2 | 65 × 38 mm | `[C]` vendor listing |
| DreamBlaster S2 | 24 × 34 mm | `[C]` vendor listing |

63 × 38 mm is the size of one modern miniature board, not a limit. The real
constraint is mechanical clearance inside a given PC case above a given card,
which is a fit check, not a specification. **A 37 × 39 mm SoM plus a 2×13 header
is comfortably smaller than the DB50XG that every WaveBlaster host was built to
accept.** `[I]`

### 1.3 The daughterboard cannot honour a host reset the way a DB50XG can

WaveBlaster pin 26 is `~RESET`, active low `[C-A]`, and every reachable
daughterboard design ties it to its synth chip's reset with a pull-up and a
small capacitor — the DB50XG-class synth is making sound again within
milliseconds of release.

**We are a SoC that boots a bootloader, initialises DDR3, reads an SD card,
SHA-1s a ROM and opens a synth.** `docs/BOARD_SPEC.md` PWR-5 records that
boot-to-first-sound is `[open]` and nothing has measured it, but it is
seconds, not milliseconds. Consequences, all `[I]`:

- **Do not wire pin 26 to the SoM's reset.** A host that pulses it during a DOS
  game's init would take the module off the air for the length of a full boot.
- Bring it in as a level-shifted **GPIO input** instead, and let firmware decide.
  The sane firmware behaviour is to treat it as "host says reinitialise" — reset
  the synth's MIDI state, not the SoC.
- Say so in the product description. A daughterboard that takes seconds to come
  up after a cold boot behaves differently from a DB50XG and somebody will file
  that as a bug otherwise.

### 1.4 Correction: the H11L1's output is pin 4, not pin 6

`hw/HARDWARE.md` § 3.3 says "output pin 6 with 4.7 kΩ pull-up to 3.3 V". Two
independent artefacts read directly in this session disagree:

| Source | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 | Pin 6 |
|---|---|---|---|---|---|---|
| KiCad `Isolator:H11L1` symbol, read from an embedded copy in a shipped design `[C-A]` | anode | cathode | NC | **output, marked inverted** | power_in (bottom → GND) | power_in (top → VCC) |
| `nikitalita/waveblaster-to-midi-module-adapter`, fabricated board, PCB netlist `[C-A]` | LED + D1 | LED + D1 | unconnected | **`WT_RX`, with a 280 Ω pull-up to +5 V** | GND | +5 V |

Pin 6 is VCC. Wiring a 4.7 kΩ "pull-up" to it and calling it the output is a
short from the supply to nothing, and the UART would see nothing at all.
`hw/HARDWARE.md` has been corrected. The pin numbering still carries a
`[C-A]` tag and not `[DS]`, so it is a row in § 3 — but it is now *two*
independent community artefacts against one unsourced sentence.

---

## 2. The carrier interface contract

### 2.1 How to read this

Every SoM-side signal is named `SOM[FUNCTION]` — a placeholder for a pin whose
number is not known here. **Filling in the pin numbers is a transcription job
from the SoM's pin-mux table, not a design job**, and § 3 says which document
holds each one.

Everything else on each row — what the carrier does with the net, its electrical
requirement, and its termination or protection — is decided here and does not
change when the pin numbers arrive.

Two rules that apply to the whole table, both `[I]`:

- **The SoM is the only thing that may drive a signal into the SoM before the
  SoM is powered.** Forlinx's manual instruction to carrier designers is to gate
  the carrier's power on the SoM's `VDD_3V3` output `[V, via hw/HARDWARE.md
  § 2.1]`. We honour it in the cheapest possible way: the carrier generates its
  own 3.3 V from the 5 V input, and **uses the SoM's 3V3 output as the buck's
  enable**, not as a supply (§ 2.8). That removes "how much current can the SoM
  source" from the unknowns list entirely, for eight cents.
- **Every connector pin that leaves the board gets ESD protection.** The DIN
  jack, the USB-C receptacle, the SD socket and the audio jack all go to the
  outside world.

### 2.2 I²S out — four nets, of which we use three

| Net | Direction | Carrier does | Electrical | Termination / protection |
|---|---|---|---|---|
| `SOM[I2S_BCLK]` | SoM → DAC `BCK` | Bit clock to the PCM5102A | 3.3 V CMOS, **3.072 MHz** (48 kHz × 32 bit × 2 ch) | 33 Ω series at the SoM end, footprint always fitted; see § 2.3 for why it is probably 0 Ω |
| `SOM[I2S_LRCLK]` | SoM → DAC `LRCK` | Word clock | 3.3 V CMOS, **48.0 kHz** | as above |
| `SOM[I2S_DOUT]` | SoM → DAC `DIN` | Serial audio data | 3.3 V CMOS, 3.072 Mb/s | as above |
| `SOM[I2S_MCLK]` | SoM → *test point only* | **Not connected to the DAC.** Brought to a test point and left there | would be 12.288 MHz (256 fs) or 24.576 MHz (512 fs) | — |

**Why MCLK is not routed.** The PCM5102A generates its system clock internally
from BCK with an on-chip PLL — TI's own wording is "the integrated PLL […]
removes the requirement for a system clock […] allowing a 3-wire I²S connection
and reducing system EMI" `[DS-X]`. The commonly reported way to select that mode
is to tie the DAC's `SCK` pin to ground `[C]`; that is a `[OPEN]` row in § 3
because I could not open the datasheet to confirm it. Bring MCLK to a test point
so that if the PLL mode turns out to need something else, the fix is a wire and
not a respin. `[I]`

`port/DESIGN.md` § 2.1 already assumes exactly this ("it can run MCLK-less with
its internal PLL"). Nothing changes above the seam.

Clock arithmetic, shown because the frame format is a choice:

```
48 000 Hz × 32 bits × 2 channels = 3.072 MHz   (32-bit frames, what we use)
48 000 Hz × 16 bits × 2 channels = 1.536 MHz   (16-bit frames)
```

`port/DESIGN.md` renders `Bit16s`, so 16-bit frames would do; 32-bit frames are
chosen because the PCM5102A family accepts 16-to-32-bit data `[DS-X]` and a
32-bit frame leaves room to move to a 24-bit renderer later without changing the
clock tree. `[I]`

### 2.3 Why the I²S group needs no real termination

FR4 microstrip propagation is ≈ 5.9 ps/mm. A net needs series termination when
its one-way delay approaches half the driver's rise time:

```
critical length = t_rise / (2 × t_pd)
t_rise = 1 ns  →  1e-9 / (2 × 5.9e-12) =  85 mm
t_rise = 2 ns  →                          169 mm
t_rise = 3 ns  →                          254 mm
```

A carrier that puts the DAC within ~50 mm of the SoM connector — which it will,
because the board is small — is inside the critical length for any plausible
3.3 V CMOS output. **Fit 33 Ω series footprints anyway and populate them with
0 Ω**, so the EMI answer exists if the first board radiates. `[I]`

Layout requirements, all `[I]` from ordinary practice and none of them
SoM-dependent:

- BCLK, LRCLK and DIN routed as a group, same layer, over unbroken ground.
- Length matching is irrelevant at 3.072 MHz (a 100 mm mismatch is 0.6 ns
  against a 325 ns bit period) — **do not spend layout effort on it.**
- Keep the group away from the analogue output section and from the SD clock.

### 2.4 The DAC's own pins

The PCM5102A is a TSSOP-20. Pin numbering below is `[C-A]` — read from KiCad's
official `Audio.lib` `PCM5100/5101/5102` symbol, cloned and parsed here. **The
BOM part is `PCM5102APWR`, the "A" variant, and I could not confirm that its
pinout is identical to the non-A part** (§ 3).

| Pin | Name | What the carrier does |
|---:|---|---|
| 1 | `CPVDD` | Charge-pump supply, 3.3 V, own 100 nF |
| 2 | `CAPP` | Charge-pump flying capacitor + `[OPEN]` value |
| 3 | `CPGND` | Charge-pump ground |
| 4 | `CAPM` | Charge-pump flying capacitor − |
| 5 | `VNEG` | Charge-pump negative rail reservoir `[OPEN]` value |
| 6 | `OUTL` | Left analogue out → § 6 |
| 7 | `OUTR` | Right analogue out → § 6 |
| 8 | `AVDD` | Analogue 3.3 V, **ferrite-isolated**, 10 µF + 100 nF |
| 9 | `AGND` | Analogue ground |
| 10 | `DEMP` | De-emphasis select — tie inactive; we never send 44.1 kHz `[C]` |
| 11 | `FLT` | Interpolation filter select — affects group delay, see § 6.5 |
| 12 | `SCK` | **Tie to ground for MCLK-less internal-PLL operation** `[C]`, § 3 |
| 13 | `BCK` | ← `SOM[I2S_BCLK]` |
| 14 | `DIN` | ← `SOM[I2S_DOUT]` |
| 15 | `LRCK` | ← `SOM[I2S_LRCLK]` |
| 16 | `FMT` | Audio format select — I²S vs left-justified `[C]`. **Strap with a fitted resistor to each rail, populate one** |
| 17 | `XSMT` | Soft mute, active low. **Do not hard-tie high.** 10 kΩ pull-up to 3.3 V plus a `SOM[GPIO_XSMT]` net, so firmware can mute across a reconfiguration |
| 18 | `LDOO` | Internal LDO output, decoupling only `[OPEN]` value |
| 19 | `DGND` | Digital ground |
| 20 | `DVDD` | Digital 3.3 V, 100 nF |

`FLT`, `DEMP`, `FMT` and `XSMT` polarity are `[C]`. Each gets **both** pull-up
and pull-down footprints with one populated, so that a datasheet read after fab
costs a rework of one 0402 and not a board. `[I]`

`XSMT` on a GPIO rather than a strap earns its pin: `docs/BOARD_SPEC.md` OUT-4
says the I²S output must run continuously from boot in every state, and OUT-5
that silence must be digital silence and never a stuck DC level. A hardware mute
that firmware can assert is how you guarantee OUT-5 across a DMA restart. `[I]`

### 2.5 UART console

| Net | Direction | Carrier does | Electrical | Protection |
|---|---|---|---|---|
| `SOM[UART0_TX]` | SoM → header | Pin 3 of a 4-pin 2.54 mm header | 3.3 V CMOS, 115200 8N1 | 100 Ω series |
| `SOM[UART0_RX]` | header → SoM | Pin 2 | 3.3 V CMOS, **not 5 V tolerant until § 3 says otherwise** | 100 Ω series + ESD diode |
| `3V3` | — | Pin 4, for a dongle that wants to know the rail | 3.3 V | — |
| `GND` | — | Pin 1 | — | — |

Header order is GND / RX / TX / 3V3 with pin 1 squared on the silkscreen, and
**the silkscreen labels are from the dongle's point of view** — the commonest
bring-up hour lost is a crossed console. `[I]`

`docs/BOARD_SPEC.md` CON-1 and CON-2 make this the only diagnostic channel the
device has, so it is not a debug convenience; it is a product interface.

### 2.6 UART2 — MIDI in

| Net | Direction | Carrier does | Electrical | Protection |
|---|---|---|---|---|
| `SOM[UART2_RX]` | front end → SoM | The single MIDI input net. **One of three mutually exclusive front ends drives it** (§ 4, § 5.3) | 3.3 V CMOS, 31 250 baud 8N1, idle high | 0 Ω link + test point between the front end and the SoM |
| `SOM[UART2_TX]` | SoM → MIDI THRU / OUT | Optional (§ 5). Not required by `docs/BOARD_SPEC.md` NG-5 | 3.3 V CMOS | 100 Ω series |

The 0 Ω link and test point is `hw/HARDWARE.md` § 3.3's suggestion and it stands:
during bring-up the MIDI input can be driven from a bench TTL source with no DIN
cable, and the FPGA-fed variant feeds exactly that node.

Baud arithmetic, from `hw/HARDWARE.md` § 3.3 and re-derived here because it is
one line: the T113 UARTs are clocked from 24 MHz and oversample ×16, so
**24 000 000 / 16 / 31 250 = 48 exactly**. No fractional divider, no error.

### 2.7 SD / SMHC

| Net | Direction | Carrier does | Electrical | Termination / protection |
|---|---|---|---|---|
| `SOM[SDC0_CLK]` | SoM → socket | Clock | 3.3 V, 25 MHz default, 50 MHz high speed | 22 Ω series at the SoM end |
| `SOM[SDC0_CMD]` | bidirectional | Command | 3.3 V | 10 kΩ pull-up to 3V3 |
| `SOM[SDC0_D0..D3]` | bidirectional | Data | 3.3 V | 10 kΩ pull-up each. **D3 must not be left to float** or some cards enter SPI mode |
| `SOM[GPIO_CD]` | socket → SoM | Card detect | 3.3 V, switch to ground | 10 kΩ pull-up, 100 nF debounce |
| `3V3_SD` | — | Socket supply | 3.3 V | 10 µF + 100 nF at the socket |

Requirements, `[I]`:

- Keep the six data-path nets under ~50 mm and matched to within a few
  millimetres of each other. The critical-length arithmetic of § 2.3 gives
  169 mm at a 2 ns edge; 50 mm is comfortable.
- **ESD on all six lines plus CD.** This is a user-accessible slot.
- `docs/BOARD_SPEC.md` ST-2 says nothing reads the card while audio renders, and
  ST-3 says the device must boot and sound with no card at all. Neither is a
  carrier requirement, but both mean the SD net group can be routed *last* and
  compromised first if the layout gets tight.

### 2.8 Power rails

| Net | Source | Carrier does | Electrical |
|---|---|---|---|
| `VIN_5V` | USB-C VBUS (standalone) **or** WaveBlaster pins 6/10/14 (card) | Board input. **Never both at once** — see § 9.5 | 5 V, 770 mA worst case (§ 9) |
| `5V_SW` | load switch from `VIN_5V` | Everything downstream. Slew-limited (§ 9.4) | 5 V |
| `SOM[VIN]` | `5V_SW` | SoM supply | `[OPEN]` — 5 V for Forlinx, `[OPEN]` for MYIR. § 3 |
| `SOM[VDD_3V3_OUT]` | SoM → carrier | **Enable signal only**, into the carrier buck's `EN` through a 100 kΩ / 10 kΩ divider and an RC | 3.3 V logic |
| `3V3` | SY8089AAAC buck from `5V_SW`, enabled by the above | DAC, opto, SD, pull-ups, 155 mA (§ 9.2) | 3.3 V ±3 % |
| `3V3_A` | `3V3` through a ferrite bead | PCM5102A `AVDD` and `CPVDD` only | 3.3 V |
| `GND` | — | Single ground pour. See § 7.5 for the split on the card build | — |

`[I]` The SY8089AAAC is already three times over in `hw/bom-testboard.csv` at
$0.078; one more instance is the cheapest way to make the carrier's 3.3 V a
known quantity instead of an `[OPEN]` about the SoM's output capability.

### 2.9 Reset and boot select

| Net | Direction | Carrier does | Electrical |
|---|---|---|---|
| `SOM[RESETn]` | carrier → SoM | Momentary button to ground, 10 kΩ pull-up to 3V3, 100 nF, series 100 Ω, ESD | **`[OPEN]`: is this an input, an output, or open-drain?** § 3. Forlinx's module releases it 92.5 ms after the 5 V input appears `[V]`, which suggests the module drives it — in which case the button must be open-drain onto it and the pull-up must be omitted |
| `WB[~RESET]` (pin 26) | host → carrier | **Card build only.** Level-shifted to a `SOM[GPIO]` input. **Not connected to `SOM[RESETn]`** — § 1.3 | 5 V logic, active low `[C-A]` |
| boot select | — | **Nothing.** No strap, no jumper | — |

**There is no boot-select circuit and that is a decision, not an omission.** The
BROM order is SD → SPI → FEL `[C, hw/HARDWARE.md § 3.4]`, the SPI NOR footprint
is deliberately unpopulated, so removing the SD card *is* the boot selector and
FEL is what you fall into. `hw/HARDWARE.md` § 3.4's "you cannot brick this
board" carries over to the carrier unchanged. `[I]`

### 2.10 USB for FEL

| Net | Direction | Carrier does | Electrical | Protection |
|---|---|---|---|---|
| `SOM[USB_DP]` / `SOM[USB_DM]` | bidirectional | USB-C receptacle, **device mode only** | 90 Ω differential, USB 2.0 full speed is all FEL needs | ESD array, ≤ 1 pF per line, placed at the connector |
| `USB_CC1` / `USB_CC2` | — | 5.1 kΩ to ground each | — | — |
| `VBUS` | in | One of the two `VIN_5V` sources | 5 V | Fuse or resettable fuse, § 9.5 |
| `SOM[USB_ID]`, `VBUS_DET` | — | `[OPEN]` — does the SoM need a VBUS sense to enter device mode? § 3 | | |

`docs/BOARD_SPEC.md` BM-2 is explicit that this is never a MIDI port. Label the
silkscreen `FEL / PWR` so nobody tries.

### 2.11 What is *not* on the carrier, and why

- **No DDR3.** It is on the module. That is the entire point of the decision and
  it deletes §§ 5.3 and 5.4 of `hw/HARDWARE.md` from the carrier's problem list
  — no length matching, no AC remapping, no VREF divider, no impedance control.
  **The carrier can be a 4-layer board with no controlled impedance.**
- **No eMMC, Ethernet, display, Wi-Fi, USB host.** `docs/BOARD_SPEC.md` NG-1,
  NG-3, NG-4.
- **No PMIC.** One buck, and the SoM owns its own rails and their sequence.
- **No on-board USB-UART.** `hw/HARDWARE.md` § 3.5 — a 4-pin header and a dongle.

---

## 3. Facts that must be read off the vendor datasheet before copper

One row per unknown. **A person with a working network can close this table in
one sitting**, and when it is closed a schematic follows mechanically from § 2.

Ordered by what it costs to be wrong.

| # | Unknown | Document that holds it | What goes wrong if it is wrong |
|---:|---|---|---|
| 1 | **SoM power-up sequence and the carrier's obligations in it** | Forlinx *FET113i-S Hardware Manual* / MYIR *MYC-YT113i Hardware Design Guide*; behind them the *T113 硬件设计指南 V1.0* | Forlinx's own manual says incorrect power-up timing causes excessive inrush, boot failure, or **irreversible damage to the processor** `[V]`. This is the reason the SoM was chosen; do not lose the benefit by ignoring the vendor's carrier rules |
| 2 | **SoM connector: part number, pitch, mating height, mechanical drawing** | SoM hardware manual | Wrong footprint = the module does not fit. Nothing else on the board can be placed until this is known |
| 3 | **SoM pin numbers for every `SOM[...]` net in § 2** | SoM pin-mux table | Transcription errors here are silent until bring-up. Every one of them is a respin |
| 4 | **`SOM[VIN]` voltage and current rating** | SoM hardware manual | Forlinx's power table is quoted "SoM powered by 5 V" `[DS-X]`; MYIR is quoted "5 V/1 A" `[DS-X]`. If either is actually 3.3 V-in, § 2.8 and the whole of § 9 change |
| 5 | **`SOM[RESETn]`: input, output, or open-drain; and its timing** | SoM hardware manual | If the module drives it and the carrier pulls it up, you have a contention. Forlinx releases it 92.5 ms after 5 V `[V]`, which reads like an output |
| 6 | **Does the SoM require a VBUS-detect input to enter USB device mode / FEL?** | SoM hardware manual + T113-i user manual | If yes and it is not wired, FEL never enumerates and the "unbrickable" argument evaporates |
| 7 | **Which pin groups can be muxed to I²S, UART2 and SMHC0 simultaneously** | T113-i user manual pin-mux tables, filtered by which of them the SoM brings out | A SoM that brings out only two of the three at once forces a different SoM. Check this *before* ordering |
| 8 | **PCM5102A`A` pinout — identical to PCM5100/5101/5102 or not** | TI `pcm5102a.pdf` | § 2.4's pin table is from KiCad's symbol for the **non-A** part `[C-A]`. Twenty pins wrong is a dead board |
| 9 | **PCM5102A MCLK-less mode: what `SCK` must actually be tied to** | TI `pcm5102a.pdf` | If it is not "tie low", § 2.2's three-wire I²S is wrong and MCLK must be routed. The test point in § 2.2 is the hedge |
| 10 | **PCM5102A `FLT` / `DEMP` / `FMT` / `XSMT` polarity** | TI `pcm5102a.pdf` | Wrong strap = wrong audio format or a permanently muted DAC. Mitigated by fitting both strap footprints (§ 2.4) |
| 11 | **PCM5102A charge-pump component values: `CAPP`/`CAPM` flying cap, `VNEG` reservoir, `LDOO` decoupling** | TI `pcm5102a.pdf`, typical-application schematic | Wrong values degrade the negative rail; too small and the ground-centred output clips asymmetrically |
| 12 | **PCM5102A supply currents (`IDD` for AVDD/DVDD/CPVDD at 48 kHz)** | TI `pcm5102a.pdf` electrical table | § 9.2 budgets **30 mA at 3.3 V as an assumption**. If it is 60 mA the budget still passes; if it is 150 mA the 2 W envelope of § 1.1 tightens |
| 13 | **PCM5102A minimum load impedance / maximum output current** | TI `pcm5102a.pdf` | § 6.2 puts a 9.4 kΩ load on it, chosen to be conservative. If the minimum is higher than 9.4 kΩ the divider must be scaled up |
| 14 | **PCM5102A interpolation-filter group delay, per `FLT` setting** | TI `pcm5102a.pdf` | `port/DESIGN.md` § 2.5 carries **~0.4 ms marked "verify against the datasheet"**. It is 8 % of the typical 4.7 ms latency budget and it is still unverified |
| 15 | **PCM5102A output DC offset and its power-up/down transient** | TI `pcm5102a.pdf` | Decides whether § 6.3's DC-blocking footprint is populated with 0 Ω or with a capacitor |
| 16 | **H11L1S pin numbering, `IF(ON)` max, `VCC` min, `tr`/`tf`, output structure** | Everlight `H11L1S` datasheet (or onsemi `H11L1M`) | § 4 is built on `IF(ON) ≤ 1.6 mA` and `VCC ≥ 3.0 V`, both `[DS-X]`. The pinout is `[C-A]` from two artefacts (§ 1.4). If `VCC` min is really 4.5 V on Everlight's part the 3.3 V argument collapses and the front end needs a 5 V island |
| 17 | **Whether the H11L1 output is open-collector or has an internal pull-up** | same | Decides whether § 4.4's external pull-up is required or merely allowed |
| 18 | **WaveBlaster pin 4 drive type: push-pull 5 V or open-drain** | None reachable. Would need a period sound-card schematic, or an oscilloscope on a real card | § 7.4 hedges with a level shifter that works either way. Get it wrong in a cheaper way and you either see nothing or you inject 5 V into a 3.3 V pin |
| 19 | **WaveBlaster header +5 V current limit, if any** | None reachable — no WaveBlaster specification document was found anywhere. § 1.1's envelope is inferred from the DB50XG's 2 W | If a given host card fuses its header feed at, say, 250 mA, the card build does not run on that card. This is the one that could still kill the daughterboard, and only a measurement settles it |
| 20 | **What the host card's mixer input actually wants** | None reachable. `docs/background.md` already flags it as unverified | § 6.2's attenuator is a re-stuffable pad precisely because of this. Too hot and it distorts; too cold and it is noisy |
| 21 | **T113-i ball pitch; DDR3 impedance and length rules** | `T113-i_Datasheet_V1.4.pdf`, `T113_硬件设计指南V1_0.pdf` | Carried over from `hw/HARDWARE.md` §§ 1.4, 5.3. **Not needed for the carrier** — only for the bare-BGA route |
| 22 | **AC remapping efuse at `0x03006228` bits [11:8]** | One `xfel read32` on the first part that arrives | Carried over from `hw/HARDWARE.md` § 5.4. **Not a carrier question** — the SoM vendor already answered it in copper |

Rows 21 and 22 are listed only so that nobody thinks the SoM decision made them
disappear; they are deferred, not resolved, and they return the day anybody lays
out the bare BGA.

---

## 4. MIDI input front end — the standalone build

Complete. Every value below is arithmetic on this page or a cited figure.

### 4.1 What the standard requires

`[DS-X]`, from search extraction of the MIDI 1.0 electrical specification and its
2014 update CA-033 (`midi.org` is blocked; so is the `mitxela.com` mirror):

- **Optically isolated 5 mA current loop**, 31 250 baud ±1 %, asynchronous,
  10-bit frames: 1 start, 8 data, 1 stop.
- **5-pin 180° DIN (DIN 41524)**, female on the device. Only three pins carry
  anything: **pin 2 shield/ground, pin 4 current source, pin 5 current sink**.
- The transmitting device sources the loop: **+5 V through 220 Ω on pin 4**, and
  its driver sinks through **220 Ω on pin 5**.
- Isolation is mandatory, not advisory. `docs/BOARD_SPEC.md` IN-2 restates it.

### 4.2 The circuit

```
                                                        3.3 V ──┬── C1 100nF ── GND
                                                                │
                                U1  H11L1S                      ├──── VCC (pin 6)
                              ┌──────────────┐                  │
  DIN pin 4 ── R1 220R ───────┤ ANODE  (1)   │                  R3
                    │         │              │                  1k
                   D1 ↑       │        VO (4)├──────────────────┴──┬── R4 0R ──> SOM[UART2_RX]
                 1N4148       │              │                     │
                    │         │ CATHODE (2)  │                    TP1
  DIN pin 5 ────────┴─────────┤              │
                              │ GND (5) ── GND               (pin 3 = NC)
                              └──────────────┘

  DIN pin 2 ── chassis / shield only, NOT signal ground
  DIN pins 1, 3 ── no connect

  Net list:
    MIDI_LOOP_A  = DIN.4, R1.1
    MIDI_LOOP_B  = R1.2, U1.ANODE(1), D1.cathode
    MIDI_LOOP_C  = DIN.5, U1.CATHODE(2), D1.anode
    MIDI_RX_3V3  = U1.VO(4), R3.2, R4.1, TP1
    3V3          = R3.1, U1.VCC(6), C1.1
    GND          = U1.GND(5), C1.2
    SOM[UART2_RX]= R4.2
```

D1 is oriented so that it conducts on the *reverse* half-cycle: its cathode to
the anode side of the IRED, its anode to the cathode side.

**Polarity — why this comes out the right way up, with no inverter.** Worth
deriving rather than assuming, because getting it wrong gives a UART that
receives 0xFF forever. The chain, each step evidenced:

1. A spec MIDI OUT sources +5 V through 220 Ω on pin 4 and *sinks* through
   220 Ω on pin 5 with its driver. In `nikitalita`'s adapter the driver is the
   host's logic-level TX, wired `WT_TX → 220 Ω → DIN pin-5 side` and
   `+5 V → 220 Ω → DIN pin-4 side` `[C-A]`. So **current flows when the sender's
   TX is low**, and the idle (mark) state is current *off*.
2. So during a start bit (logic 0), the loop is on and our IRED conducts.
3. The H11L1's output is **inverting** — KiCad's symbol marks pin 4 `output
   inverted` `[C-A]`, and the "sinks 16 mA at 0.4 V" specification `[DS-X]`
   describes a part that pulls *down* when the emitter is lit.
4. Therefore: logic 0 on the wire → LED on → `VO` low → UART sees a space.
   Idle → LED off → `R3` pulls `VO` high → UART sees a mark.

Two inversions cancel. `VO` goes straight to `SOM[UART2_RX]` with nothing
between it but the 0 Ω link.

- **R1 = 220 Ω, 1 %, 0.1 W.** The receiver's half of the loop. Spec value.
- **D1 = 1N4148**, reverse-parallel across the IRED. Protects the emitter from
  reverse voltage on a mis-wired or reversed cable, and clamps the reverse
  transient when the loop opens each bit.
- **R3 = 1 kΩ** pull-up on the output — chosen in § 4.4.
- **C1 = 100 nF** at the opto's VCC.
- **R4 = 0 Ω** plus a test point, per `hw/HARDWARE.md` § 3.3: lets a bench TTL
  source drive `SOM[UART2_RX]` with no DIN cable, which is also how the
  FPGA-fed variant feeds it.
- **DIN pin 2 goes to chassis/shield, not to signal ground.** Tying it to signal
  ground on both ends of a cable is how you rebuild the ground loop that the
  optocoupler exists to break. `[I]`

### 4.3 Loop current — the arithmetic

The loop, when the sender's driver is asserted:

```
I = (V_sender − V_f(IRED) − V_OL(driver)) / (R_source + R_receiver + R_sink)
```

| Case | V | Resistors (Ω) | V_f | I |
|---|---:|---|---:|---:|
| **Spec-compliant 5 V sender**, our 220 Ω | 5.0 | 220 + 220 + 220 = 660 | 1.5 | **5.00 mA** |
| same, V_f at the low end | 5.0 | 660 | 1.2 | 5.45 mA |
| same, plus 20 Ω of long cable | 5.0 | 680 | 1.5 | 4.85 mA |
| **CA-033 3.3 V sender** (33 Ω source, 10 Ω sink) `[DS-X]` | 3.3 | 33 + 220 + 10 = 263 | 1.5 | **6.46 mA** |
| **Naive 3.3 V sender** that kept 220 Ω both legs | 3.3 | 660 | 1.5 | **2.58 mA** |
| same, V_f at the low end | 3.3 | 660 | 1.2 | 3.03 mA |

`V_OL` taken as 0.2 V (5 V logic) / 0.1 V (3.3 V logic). The first row landing
on exactly 5.00 mA is not a coincidence — it is the arithmetic the 1983
specification was written around, and reproducing it is the check that the
resistor value is right.

**Against the H11L1's `IF(ON)` of 1.6 mA maximum** `[DS-X]`:

| Sender | Margin |
|---|---:|
| 5 V spec | 5.00 / 1.6 = **3.1×** |
| CA-033 3.3 V | 6.46 / 1.6 = **4.0×** |
| Naive 3.3 V with 220 Ω legs | 2.58 / 1.6 = **1.6×** |

That last row is the one worth having. **Even a lazily built 3.3 V MIDI out
that never read CA-033 still triggers this receiver**, with 60 % margin. A
6N138-based receiver, needing its 5 mA to get a usable current transfer ratio,
would not. That is the strongest practical argument for the H11L1 and it is not
the usual one.

Upper end: 6.46 mA is about 11 % of the 60 mA continuous IRED rating typical of
this package class `[C]` — no thermal question.

### 4.4 Rise and fall at 31 250 baud

Bit period = 1 / 31 250 = **32.0 µs**. A 10-bit frame is 320 µs.

Two things add edge time, and both are budgeted against half a bit (16.0 µs),
which is the point at which the UART's mid-bit sample lands in the wrong bit:

| Contribution | Value | As a fraction of a bit |
|---|---:|---:|
| H11L1 internal `tr`/`tf`, typical 100 ns `[DS-X]` | 100 ns | **0.31 %** |
| Output RC with the pull-up, 10–90 %, into ~15 pF of pin + trace | see below | |

```
t(10-90%) = 2.2 × R × C
R = 270 Ω  → 2.2 × 270 × 15p  =   9 ns   (0.03 % of a bit),  sink 3.3/270  = 12.2 mA
R = 1 kΩ   → 2.2 × 1000 × 15p =  33 ns   (0.10 %),           sink 3.3/1k   =  3.3 mA
R = 4.7 kΩ → 2.2 × 4700 × 15p = 155 ns   (0.49 %),           sink 3.3/4.7k =  0.7 mA
```

**Choose R3 = 1 kΩ.** 270 Ω is the datasheet's own test-circuit value `[C]` but
wastes 12 mA; 4.7 kΩ (what `hw/HARDWARE.md` § 3.3 suggested) is still fine but
gives away 5× the edge time for 2.6 mA. 1 kΩ costs 3.3 mA — which § 9.2 budgets
— and puts the total edge contribution at

```
100 ns + 33 ns ≈ 0.13 µs  against a 32 µs bit and a 16 µs sampling margin
```

that is **120× inside** the margin, and the UART's own 16× sample tick is
32/16 = 2.0 µs, so the edge is not even resolvable by the sampler.

The point of the whole paragraph: **at 31 250 baud the edge budget is not a
constraint, it is a rounding error — provided you do not use a photo-Darlington.**
The 6N138's turn-off is measured in microseconds and load-dependent, which is
why its pull-up is a tuning exercise. The H11L1's is not.

### 4.5 The 3.3 V argument, stated precisely

- The H11L1 is specified to operate from **3.0 V to 16 V** `[DS-X]`. At 3.3 V we
  are 0.3 V above the minimum — in specification, near its edge.
- The 6N137 would also give clean edges but wants **4.5–5.5 V** on the output
  side `[C]`, which means a level shifter into a 3.3 V SoC and an extra part.
- **The current loop is powered by the transmitting device, not by us.** Our
  3.3 V rail supplies the opto's `VCC` and its pull-up and nothing else — see
  § 9.2, where MIDI IN costs 3.3 mA + a few mA of `ICC` and not 5 mA of LED.

**Risk, recorded honestly:** every number in this subsection is `[DS-X]`, and
§ 3 row 16 says so. If Everlight's `H11L1S` turns out to specify 4.5 V minimum
where onsemi's `H11L1M` specifies 3.0 V, the fix is a 5 V island for the opto
plus a divider on its output — two resistors — not a redesign. Fit the divider
footprints.

---

## 5. MIDI THRU

### 5.1 Should the standalone box have one? Yes.

`docs/BOARD_SPEC.md` NG-5 rules out MIDI **OUT** — the synth has nothing to say —
and explicitly leaves THRU to `hw/`. The answer is yes, for three reasons:

1. **It is the historical use case.** An MT-32 in a DOS setup is very often the
   first box in a chain, with a Sound Canvas or a drum machine behind it. A
   module with no THRU forces a splitter box.
2. **It costs almost nothing** (§ 5.2).
3. **This design can offer one with a clear conscience, and a 6N138 design
   cannot.** THRU regenerates the stream from the receive optocoupler's output,
   so every hop adds that optocoupler's edge asymmetry to the bit widths, and
   the errors accumulate down a chain. § 4.4 puts our contribution at ~0.13 µs
   out of a 32 µs bit — **0.4 % per hop**, so twenty hops before you are at half
   a bit. A photo-Darlington front end contributes microseconds per hop and the
   chain length that survives is much shorter. `[I]`

### 5.2 The circuit and its cost

THRU is driven from the **receive optocoupler's output**, not from a UART, so it
works even when the firmware is not running — which is what a THRU is for.

```
  U1 VO (H11L1 pin 4) ──> U2 in    74LVC1G07     U2 out ── R5 220R ──> THRU pin 5
                          (non-inverting,
                           open drain,
                           VCC = 3.3 V,
                           output tolerant to 5.5 V)

                +5 V ──── R6 220R ──────────────────────────────────> THRU pin 4

                                                   THRU pin 2 ── chassis
                                                   THRU pins 1, 3 ── no connect
```

**Two details that are the whole of the circuit.**

*It must be non-inverting.* `U1`'s `VO` is already inverted with respect to the
wire (§ 4.2), so a non-inverting buffer reproduces the original loop polarity:
`VO` low → buffer output low → outgoing loop on. A `74LVC1G06` — the inverting
member of the same family, one digit away in the order code — gives a THRU port
that transmits the complement of everything, which looks like a dead port.

*It does not need a 5 V supply.* An open-drain output in the LVC family is
tolerant to 5.5 V regardless of its own `VCC` `[C]`, so `U2` runs from the
carrier's 3.3 V and only the loop's source resistor `R6` touches 5 V. No 5 V
logic island, no level shifter.

| Part | Value | Cost |
|---|---|---:|
| U2, 74LVC1G07 **non-inverting** open-drain buffer | SOT-23-5 | ~$0.05 `[GUESS]` |
| R5, R6 | 220 Ω 1 % | ~$0.01 |
| DIN-5 180° PCB jack | | ~$0.40 `[GUESS]`, same as J3 in the BOM |
| **Total** | | **≈ $0.46** plus one panel cut-out |

**Drive it from 5 V, not 3.3 V.** The arithmetic says why:

```
5 V, classic 220 Ω both legs, into a 220 Ω + LED receiver:
    I = (5.0 − 1.5 − 0.2) / (220 + 220 + 220) = 5.00 mA     ← spec value

3.3 V, keeping 220 Ω both legs:
    I = (3.3 − 1.5 − 0.1) / 660 = 2.58 mA                   ← half the spec
        works into an H11L1 (1.6 mA), marginal into a 6N138

3.3 V per CA-033, RA = 33 Ω and RC = 10 Ω:
    I = (3.3 − 1.5 − 0.1) / (33 + 220 + 10) = 6.46 mA       ← correct, but
        R_A dissipates 3.3² / 33 = 0.33 W into a shorted cable,
        so R_A must be a 0.5 W part or four paralleled 130 Ω `[DS-X]`
```

The standalone box has 5 V at the input (USB-C VBUS), so the classic 5 V/220 Ω
loop is available for free, is exactly spec-compliant, drives *anything*
downstream including 1983 hardware, and avoids stocking a 0.5 W resistor. `[I]`

### 5.3 THRU on the daughterboard: no

The card build has no panel and no DIN jacks, and § 7 shows its MIDI input is a
5 V logic line shared with the host, not an isolated loop. Fit the footprints,
depopulate them. One PCB, two builds.

---

## 6. Analogue output stage

### 6.1 What the PCM5102A gives us

All `[DS-X]` — TI's site is blocked, as is every mirror I tried (§ 10):

- **2.1 V RMS, ground-centred**, via "DirectPath" charge-pump technology.
- **"No DC blocking capacitors required"**, with an integrated negative charge
  pump. That is the part's headline feature and the reason it costs $0.84.
- Internal PLL generates the system clock from BCK, so MCLK is unnecessary
  (§ 2.2).

2.1 V RMS is 2.970 V peak, **5.940 V peak-to-peak centred on 0 V**. For context:
consumer line level (−10 dBV) is 0.316 V RMS, the CD/DAC convention is 2 V RMS,
professional +4 dBu is 1.228 V RMS.

**This changes `docs/PLAN.md` and `docs/BOARD_SPEC.md`.** Both say the output is
"DC-blocked". With a ground-centred DirectPath output, a DC-blocking capacitor
is **not required and is mildly harmful**: at 2.1 V RMS the signal swings ±3 V
about ground, so a blocking cap must be bipolar, and a 4.7 µF X7R in that
position contributes voltage-coefficient distortion for no benefit. See § 6.3
for what to do instead.

### 6.2 The WaveBlaster attenuator

The host card's wavetable mixer input wants something like line level and **what
it actually wants is `[OPEN]`** (§ 3 row 20; `docs/background.md` already flags
it as unverified). So this is built as a **re-stuffable resistive pad**, and the
default is chosen against the one piece of hard evidence available.

The evidence: `ivop/cs9236-waveblaster`, an open design that has been
commercialised as the CrystalBlaster C1/C2 and therefore demonstrably works in
real host cards. Its output stage, read directly from its netlist `[C-A]`:

```
CS4333 AOUT ── C10 10µF ──┬── R4 2k4 ──┬── WaveBlaster pin 24 (left)
                          │            │
                        R2 2k4       C5 750pF
                          │            │
                        GNDA         GNDA
```

— that is, a **2.4 kΩ source impedance and essentially no attenuation**, with an
88 kHz RC (1/(2π × 2400 × 750 p) = 88.4 kHz) to kill RF. Whatever the host wants,
a ~2.4 kΩ source delivering roughly 1 V RMS is known to satisfy it.

Our default, therefore, targets the same thing from a 2.1 V RMS source:

```
OUTL (pin 6) ──┬── R7 4k7 1% ──┬── R9 33R ──> WB pin 24 (left audio out)
               │               │
             R8 4k7 1%       C4 680pF
               │               │
             AGND            AGND
```

| Quantity | Arithmetic | Value |
|---|---|---|
| Divider ratio | 4700 / (4700 + 4700) | 0.5000 = **−6.02 dB** |
| Open-circuit output | 2.1 × 0.5 | **1.050 V RMS** (2.970 V pp) |
| Source impedance | 4700 ∥ 4700 | **2 350 Ω** — within 2 % of the CS9236 reference's 2 400 Ω |
| Load presented to the DAC | 4700 + 4700 | **9 400 Ω** |
| RC corner | 1 / (2π × 2350 × 680 p) | **99.6 kHz** |
| Droop at 20 kHz | −20 log₁₀ √(1 + (20/99.6)²) | **−0.17 dB** |
| Into a 10 kΩ host input | 1.050 × 10000/(10000 + 2350) | **0.850 V RMS** |
| Into a 20 kΩ host input | | 0.940 V RMS |
| Into a 47 kΩ host input | | 1.000 V RMS |

The RC corner is quoted open-circuit, which is the **lowest** it gets: loaded by
a 10 kΩ host, C4 sees 2350 ∥ (33 + 10000) = 1 917 Ω and the corner rises to
122 kHz. Quoting the worst case is the point.

**Re-stuffing table**, if a real card says it is too hot or too cold. Each row
keeps the DAC load near 10 kΩ:

| R7 / R8 | Ratio | dB | Open-circuit | Z_out | Into 10 kΩ |
|---|---:|---:|---:|---:|---:|
| 4k7 / 4k7 | 0.5000 | −6.02 | 1.050 V | 2 350 Ω | 0.850 V |
| 5k6 / 3k3 | 0.3708 | −8.62 | 0.779 V | 2 076 Ω | 0.645 V |
| 6k8 / 3k3 | 0.3267 | −9.72 | 0.686 V | 2 222 Ω | 0.561 V |
| 6k8 / 2k2 | 0.2444 | −12.24 | 0.513 V | 1 662 Ω | 0.440 V |
| 8k2 / 2k2 | 0.2115 | −13.49 | 0.444 V | 1 735 Ω | 0.379 V |
| 9k1 / 1k5 | 0.1415 | −16.98 | 0.297 V | 1 288 Ω | 0.263 V |

`docs/background.md` § 3 guessed "roughly 10–12 dB of attenuation", which is
rows 3–4. The default here is −6 dB instead, on the grounds that it lands on the
same open-circuit level and the same source impedance as a design known to work,
and that the mixer's own "wavetable" level control exists — `docs/background.md`
already says to set it from DOS before concluding the board distorts.
**Attenuating in the analogue domain also costs signal-to-noise**, so the
correct default is the least attenuation that does not clip. `[I]`

Right channel is identical from `OUTR` (pin 7) to WB pin 20.

### 6.3 DC blocking: a footprint, populated with 0 Ω

`R9 = 33 Ω` in the diagram above sits where a series capacitor would go.

- **Default: 0 Ω** (or the 33 Ω, which also damps cable capacitance). The
  DirectPath output is ground-centred by design and the whole point of the part
  is that it needs no coupling capacitor `[DS-X]`.
- **If measurement shows a DC offset or an objectionable power-up thump** —
  which is § 3 row 15, unverified — populate a bipolar capacitor instead and
  accept the corner it brings:

| C | Series resistance it sees | f(−3 dB) |
|---:|---|---:|
| 1.0 µF | 9 400 Ω (open circuit) | 16.9 Hz |
| 2.2 µF | 9 400 Ω | 7.7 Hz |
| 4.7 µF | 9 400 Ω | 3.6 Hz |
| 10 µF | 9 400 Ω | 1.7 Hz |
| 4.7 µF | 12 350 Ω (into a 10 kΩ host) | 2.7 Hz |

`f = 1/(2πRC)`. **4.7 µF bipolar is the right hedge value**: 3.6 Hz is two and a
half octaves below 20 Hz, so the passband is untouched, and it is small enough
that a bipolar film or electrolytic part is available in an 0805-ish size.

### 6.4 The standalone line output

No attenuator. 2.1 V RMS is inside what any line input accepts.

```
OUTL (pin 6) ──┬── R10 100R ──> TRS tip
               │
             C6 2n2
               │
             AGND
```

- **R10 = 100 Ω** — short-circuit protection for a hot-plugged jack and
  isolation from cable capacitance.
- **C6 = 2.2 nF**: 1/(2π × 100 × 2.2 n) = **723 kHz**, which is −0.003 dB at
  20 kHz — an RF filter that the audio band cannot see.
- DC block: the same 0 Ω/4.7 µF footprint as § 6.3.
- ESD array on tip and ring at the jack.

### 6.5 Two things about this stage that are still open

- **`FLT`** selects the interpolation filter and therefore the group delay.
  `port/DESIGN.md` § 2.5 budgets **~0.4 ms** for it and marks it "verify against
  the datasheet". I could not. It is 8 % of the 4.7 ms typical latency and it is
  the only line in that budget nobody has checked. § 3 row 14.
- **The minimum load.** 9.4 kΩ was chosen to be conservative and to match a
  known-good reference's source impedance, not because a datasheet permitted it.
  § 3 row 13.

---

## 7. The WaveBlaster connector

### 7.1 Evidence class, stated first

**No WaveBlaster specification document exists in any form I could reach.**
There is no Creative or Yamaha document, no standards body, nothing. What there
is:

| Source | Class | How it was obtained |
|---|---|---|
| `nikitalita/waveblaster-to-midi-module-adapter` — KiCad **PCB netlist of a fabricated, tested board** | `[C-A]` | cloned from GitHub, pad-to-net mapping parsed here |
| `ivop/vs1053-waveblaster` (rev A and rev C) — KiCad **netlists of a fabricated board** | `[C-A]` | cloned, netlist parsed here |
| `ivop/cs9236-waveblaster` — KiCad netlist; this design was commercialised as the CrystalBlaster C1/C2 | `[C-A]` | cloned, netlist parsed here |
| epanorama.net "Waveblaster pinout", os2museum, VOGONS threads, soundprogramming.net | `[C]` | **search summaries only — all four domains are blocked** (§ 10) |

**Three independently authored, independently fabricated boards agree on every
pin.** That is a materially different kind of evidence from a web table, and it
is the reason this section can be called finished. It is still not a vendor
document, and `docs/background.md`'s instruction to verify against a real card
before laying out a footprint stands.

### 7.2 The pin assignment

Connector: **2×13, 2.54 mm pitch, 26-way**, straight or right-angle; the adapter
board above uses `IDC-Header_2x13_P2.54mm` `[C-A]`. Odd pins are one row, even
the other.

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | GND (digital) | 2 | **not connected** |
| 3 | GND | 4 | **MIDI IN** — serial data, host → daughterboard |
| 5 | GND | 6 | **+5 V** |
| 7 | GND | 8 | **MIDI OUT** — daughterboard → host. *Usually not connected on the host card* |
| 9 | GND | 10 | **+5 V** |
| 11 | GND | 12 | **Audio RIGHT IN** — host → daughterboard |
| 13 | **not connected** | 14 | **+5 V** |
| 15 | AGND (analogue) | 16 | **Audio LEFT IN** — host → daughterboard |
| 17 | AGND | 18 | **+12 V** |
| 19 | AGND | 20 | **Audio RIGHT OUT** — daughterboard → host mixer |
| 21 | AGND | 22 | **−12 V** |
| 23 | AGND | 24 | **Audio LEFT OUT** — daughterboard → host mixer |
| 25 | AGND | 26 | **`~RESET`**, active low |

Note pin 13: it is the one odd pin that is *not* a ground, and all three
artefacts leave it unconnected `[C-A]`.

### 7.3 What this design drives, ignores, and must tolerate

| Pin(s) | This design |
|---|---|
| 4 | **Input.** Level-shifted to `SOM[UART2_RX]` (§ 7.4) |
| 20, 24 | **Drives.** § 6.2's attenuator, right and left |
| 6, 10, 14 | **Consumes.** All three in parallel — § 9.3 |
| 1, 3, 5, 7, 9, 11 | Digital ground return |
| 15, 17, 19, 21, 23, 25 | Analogue ground return — § 7.5 |
| 26 | **Input, to a GPIO, not to the SoM's reset** — § 1.3 |
| 8 | **Ignored.** `docs/BOARD_SPEC.md` NG-5: no MIDI OUT. Fit the pin, do not connect it |
| 12, 16 | **Ignored.** Audio *into* the daughterboard. Only one host card is reported to drive them — the MediaTriX AudioTriX 3D-XG — and the author of the adapter board advises against connecting them `[C]` |
| 18, 22 | **Ignored, deliberately, and this is a compatibility win.** We use neither ±12 V rail. Nothing in this design needs a bipolar analogue supply: the PCM5102A makes its own negative rail with an on-chip charge pump. All three reachable artefacts leave 18 and 22 unconnected too `[C-A]` |
| 2, 13 | Not connected |

**Why ignoring ±12 V matters.** It is the difference between "works on a 1994
ISA card" and "works on anything with the header". A daughterboard that needs
−12 V is at the mercy of whether a given host card, riser, or modern
retro-reproduction card actually routes it. We are not.

### 7.4 MIDI on pin 4 is 5 V logic, not a current loop

This is the electrical fact that separates the two builds, and it is well
evidenced `[C-A]`:

- `cs9236-waveblaster` wires pin 4 **straight to a 3.3 V-supplied synth chip's
  input** with no series resistor and no shifter.
- `vs1053-waveblaster` puts a **BSS138 level shifter** on it: gate to +3V3,
  drain to pin 4 with a 10 kΩ pull-up to **+5 V**, source to the synth's UART RX
  with a 10 kΩ pull-up to +3V3 — the textbook AN97055 topology.
- `waveblaster-to-midi-module-adapter` takes pin 4 and drives a **standard 5 V
  MIDI OUT loop** off it through a single 220 Ω resistor, which only works if
  pin 4 is a logic-level source.

So: **31 250-baud UART data at 5 V CMOS/TTL levels, referenced to the host's
ground.** No isolation is possible or wanted — the daughterboard already shares
ground with the host through twelve connector pins.

**But the two artefacts disagree about the drive structure**, and this is § 3
row 18. `vs1053`'s 10 kΩ pull-up to +5 V says its designer expected the host
might be open-drain; `cs9236` and the adapter board assume push-pull. Nobody
reachable states which it is.

**Therefore: use the BSS138 shifter, which works either way.**

```
          +5 V                                  +3V3
           │                                      │
         R11 10k                                R12 10k
           │            Q2  BSS138                │
  WB.4 ────┴────────── D ──────── S ──────────────┴──── R13 0R ──> SOM[UART2_RX]
                             │                                       │
                             G ───── +3V3                           TP1

  Net list:
    WB_MIDI_5V   = J7.4, R11.2, Q2.drain
    MIDI_RX_3V3  = Q2.source, R12.2, R13.1, TP1
    +5V          = R11.1
    +3V3         = R12.1, Q2.gate
    SOM[UART2_RX]= R13.2
```

- Pulls low correctly whether the host is push-pull or open-drain.
- Clamps the SoC side to 3.3 V in both cases — a divider would not, if the host
  is open-drain, and a bare wire would not at all.
- Rise time: 2.2 × 10 kΩ × 20 pF = **440 ns = 1.4 % of a 32 µs bit.** Slower
  than § 4.4's opto path, still 36× inside the 16 µs sampling margin. If it
  ever matters, 4.7 kΩ takes it to 207 ns.
- `R13` is the same 0 Ω link and test point as § 4.2. **Exactly one of the three
  front ends is populated on a given board**: the opto (§ 4), this shifter, or a
  bare link from a 3.3 V FPGA.

### 7.5 Grounds: two of them, joined once

The header separates six digital-ground pins from six analogue-ground pins.
Both reachable multi-chip designs treat this the same way `[C-A]`:

- `cs9236-waveblaster`: nets `GND` and `GNDA` are separate, joined by **`R6`, a
  0 Ω resistor**.
- `vs1053-waveblaster`: nets `GND` and `GNDA` are separate, joined by **`R2`, a
  0 Ω resistor** (and `+5V`/`+5VA` likewise through a 0 Ω `R7`).

Adopt the same rule: **two pours, joined at exactly one 0 Ω link near the
connector.** The DAC's `AGND`, its analogue decoupling, the attenuator's shunt
legs and the RC capacitors all return to the analogue pour; everything else to
the digital pour. Fit the link. `[I]`

The failure mode it guards against: the host card may or may not tie its own
GND and AGND together at a low impedance, and if it does not, a single-pour
daughterboard becomes the bridge and carries the host's digital return current
through its own analogue ground.

### 7.6 Reset on pin 26

Both reachable designs treat pin 26 as an input with a local pull-up and an RC:
`cs9236` uses 10 kΩ to +3V3 with 100 nF (τ = 1 ms), `vs1053` uses 100 kΩ with
100 nF (τ = 10 ms) `[C-A]`.

Note what that implies and that neither designer stated it: **they both pull it
up to 3.3 V**, which is only safe if the host drives pin 26 open-drain. If a
host drives it push-pull at 5 V into a 3.3 V-pulled-up node, something is being
overstressed. Since § 1.3 says we are not wiring it to the SoM's reset anyway,
route it through the same kind of level shifter as § 7.4 into a GPIO, and the
question stops mattering. `[I]`

---

## 8. One board, two builds

Everything above collapses into a single PCB with three depopulation groups.

| Group | Standalone box | Daughterboard |
|---|---|---|
| J_DIN_IN + R1 + D1 + U1 + R3 + C1 (§ 4) | **fit** | omit |
| J_DIN_THRU + U2 + R5 + R6 (§ 5) | **fit** | omit |
| J_WB 2×13 header + R11 + R12 + Q2 (§ 7) | omit | **fit** |
| Attenuator R7/R8/C4 (§ 6.2) | omit | **fit** |
| Line-out jack + R10/C6 (§ 6.4) | **fit** | omit |
| USB-C receptacle (§ 2.10) | **fit** | fit — FEL is still how you recover it |
| Load switch + inrush network (§ 9.4) | fit | **fit, mandatory** |
| R4 / R13, the 0 Ω MIDI link | exactly one populated | exactly one populated |

The firmware difference between the two, per `docs/BOARD_SPEC.md` § 1, is the
MIDI front end and the output attenuation — **and both of those are now
resistor-stuffing decisions, not firmware decisions.** The firmware sees the
same UART and the same I²S in both builds. `[I]`

---

## 9. Power budget

### 9.1 The 5 V side

| Load | Figure | Source |
|---|---:|---|
| SoM, no-load standby | 0.40 W | `[DS-X]` Forlinx power table |
| SoM, **CPU stress + memory + eMMC read/write stress** | **1.05 W** | `[DS-X]` same table, 512 MB + 8 GB eMMC configuration, SoM powered at 5 V |
| SoM, **no-load starting peak** | **3.25 W** | `[DS-X]` same table |
| MYIR module supply spec, for cross-check | 5 V / 1 A | `[DS-X]` |

The 1.05 W row is the right one for us: our workload is one Cortex-A7 rendering
out of DDR3 continuously, with the SD card idle after boot
(`docs/BOARD_SPEC.md` ST-2). It is if anything pessimistic, because it includes
eMMC write traffic we will never generate.

```
1.05 W / 5 V = 210 mA
3.25 W / 5 V = 650 mA
```

### 9.2 The 3.3 V side

| Load | Current | Basis |
|---|---:|---|
| PCM5102A, all three supplies at 48 kHz | 30 mA | **`[I]` ASSUMPTION.** § 3 row 12: the datasheet's `IDD` table is unreadable from here |
| H11L1S `VCC` (standalone build only) | 5 mA | **`[I]` ASSUMPTION.** § 3 row 16 |
| H11L1S output pull-up R3 = 1 kΩ, worst case output low | 3.3 mA | arithmetic: 3.3 V / 1 kΩ, § 4.4 |
| microSD, read | 100 mA | `[C]` SD card read-current class figure. Only during boot (ST-2) |
| SD pull-ups, power LED, strap resistors, misc | 20 mA | `[GUESS]` |
| **Total** | **≈ 158 mA** | |

```
158 mA × 3.3 V = 0.52 W at the load
0.52 W / 0.85 (buck efficiency) = 0.61 W drawn from 5 V = 123 mA
```

The card build drops the opto's 8.3 mA and adds the shifter's ~0.7 mA, so it is
within rounding of the same number.

### 9.3 The total, and what the header gives

| | Steady | Startup |
|---|---:|---:|
| SoM | 1.05 W / 210 mA | 3.25 W / 650 mA |
| Carrier 3.3 V, reflected to 5 V | 0.61 W / 123 mA | 0.61 W / 123 mA |
| **Total** | **1.66 W / 333 mA** | **3.86 W / 773 mA** |

What the WaveBlaster header can actually supply is **`[OPEN]` — no
specification document exists** (§ 3 row 19). The available evidence:

- **Yamaha DB50XG: 2 W** `[DS-X]`, from its owner's manual specifications page.
  A shipping, mass-market board that every WaveBlaster host was built to accept.
- **Three +5 V pins in parallel** (6, 10, 14) and twelve ground pins. A 0.64 mm
  square post in a 2.54 mm housing is conventionally rated at several amps, so
  **the connector is nowhere near the limit** `[I]`.
- The +5 V on the header is, on every design examined, a plain trace from the
  card's bus +5 V `[I]` — ISA and PCI both supply +5 V from the slot, which is
  not the constraint either.

**Verdict:** steady 1.66 W is **0.34 W inside** the only proven envelope we
have. The card build lives. The startup transient is the thing to engineer, not
the steady state — which is the opposite of what one might have assumed, and is
why this section exists.

### 9.4 Inrush — a hard requirement on the daughterboard

Charging bulk capacitance is the whole problem:

```
uncontrolled, C = 100 µF through 0.1 Ω of trace and connector:
    first-instant peak = 5 V / 0.1 Ω = 50 A,  τ = 10 µs

behind a load switch with a controlled slew:
    I = C × dV/dt
    100 µF × 5 V / 0.5 ms = 1 000 mA
    100 µF × 5 V / 1 ms   =   500 mA
    100 µF × 5 V / 5 ms   =   100 mA
    100 µF × 5 V / 10 ms  =    50 mA
```

**Requirement: a slew-rate-controlled load switch between `VIN_5V` and
`5V_SW`, with the slew set to ≥ 5 ms**, so that the capacitive inrush is
≤ 100 mA and the only remaining transient is the SoM's own. Combined with the
SoM's 650 mA starting peak, the worst case the header sees is then ~770 mA for
a few milliseconds rather than tens of amps for tens of microseconds.

`docs/background.md` § 3 already said "put bulk capacitance and a soft-start
load switch on it so the inrush does not brown out a 1995 sound card". This is
that instruction with the arithmetic attached, and it is now a requirement
rather than advice.

Bulk capacitance: **100 µF on `5V_SW`** (after the switch, so the switch controls
its charging), plus 10 µF at the SoM connector and 100 nF per supply pin.

### 9.5 The two power inputs must never fight

The standalone build takes 5 V from USB-C VBUS. The card build takes it from
header pins 6/10/14. **Both connectors are fitted on the same PCB** (§ 8: USB-C
stays on the daughterboard so FEL recovery still works).

So a daughterboard plugged into a live sound card *and* into a USB cable has two
5 V sources tied together. Requirement `[I]`:

- **An ideal-diode or Schottky OR on the USB VBUS leg only**, so the host's 5 V
  can never be back-driven into a PC's USB port.
- A resettable fuse on the USB leg.
- The load switch of § 9.4 sits downstream of the OR, so there is exactly one
  soft-start regardless of which source is present.

`docs/background.md` § 4 already lists "both inputs diode-OR'd or switched,
never both driving" as a requirement. Same conclusion, reached independently.

---

## 10. What I tried to fetch and could not

Recorded so that the next person with a working network knows exactly what to
get. All of these returned `403` at the egress gateway or `EGRESS_BLOCKED`.

**Needed to close § 3 — fetch these into `hw/ref/`:**

| Document | URL tried |
|---|---|
| TI PCM5102A datasheet | `https://www.ti.com/lit/ds/symlink/pcm5102a.pdf` |
| PCM5102A mirror | `https://www.kyohritsu.com/eclib/OTHER/DATASHEET/TI/pcm5102a.pdf` |
| PCM5102A-Q1 mirror (Mouser) | `https://www.mouser.com/datasheet/2/405/pcm5102a-q1-453568.pdf` |
| onsemi H11L1M datasheet | `https://www.onsemi.com/pdf/datasheet/h11l1-d.pdf`, `https://www.mouser.com/datasheet/2/149/H11L1M-1010369.pdf` |
| H11L1/L2/L3 datasheet (QT-Brightek, Fairchild mirror) | `https://www.qt-brightek.com/datasheet/H11L1_H11L2_H11L3.pdf`, `https://media.digikey.com/pdf/Data Sheets/Fairchild PDFs/H11L1, H11L2, H11L3.pdf` |
| Everlight H11L1S, LCSC mirror | `https://datasheet.lcsc.com/lcsc/Everlight-Elec-H11L1S-TA_C78589.pdf` |
| MIDI 1.0 Electrical Specification | `https://midi.org/midi-1-0-electrical-specification`, `https://midi.org/5-pin-din-electrical-specs` |
| **CA-033**, MIDI 1.0 Electrical Specification Update 2014 | `https://www.midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf`, `https://mitxela.com/other/ca33.pdf`, `https://amei.or.jp/midistandardcommittee/Recommended_Practice/ca33-j.pdf` |
| MIDI hardware spec (Teragon mirror) | `http://midi.teragonaudio.com/tech/midispec/hardware.htm` |
| Forlinx FET113i-S / OK113i-S brief | `https://forlinx.net/download/FET113i-S-SoM-OK113i-S-SBC-Brief.pdf` |
| Forlinx T113-i SoM power-consumption article | `https://www.forlinx.net/industrial-news/allwinner-t113-som-performance-528.html`, `https://medium.com/@forlinx2021/...` |
| MYIR MYC-YT113i datasheet | `https://www.myirtech.com/download/allwinner/MYC-YT113i.pdf`, `https://www.mouser.com/datasheet/2/951/MYC_YT113i-3359804.pdf` |
| WaveBlaster pinout (epanorama) | `https://www.epanorama.net/documents/pc/waveblaster.html` |
| WaveBlaster/DB50XG pinout | `https://soundprogramming.net/soundcards/waveblasterdb50xg-daughterboard-pinout/` |
| OS/2 Museum, "Deeper Into Wave Blaster" | `http://www.os2museum.com/wp/deeper-into-wave-blaster/` |
| DB50XG owner's manual (specifications page) | `https://retronn.de/ftp/driver/Yamaha/db50xg/DB50XGE.pdf`, `https://usa.yamaha.com/files/download/other_assets/5/318075/DB50XGE.pdf` |
| DB50XG, Synth DIY Wiki | `https://sdiy.info/wiki/Yamaha_DB50XG` |
| Wikipedia (Creative Wave Blaster, Ensoniq SoundscapeDB) | `https://en.wikipedia.org/wiki/...` |

Plus everything already listed in `hw/HARDWARE.md` § 0 and § 1.5, still blocked.

**What *was* reachable, and is the reason §§ 4–9 exist:**

- `github.com` clones over the anonymous git lane, and `raw.githubusercontent.com`.
  Three WaveBlaster daughterboard designs and KiCad's official symbol library
  were cloned and their netlists parsed here:
  - `nikitalita/waveblaster-to-midi-module-adapter`
  - `ivop/vs1053-waveblaster`
  - `ivop/cs9236-waveblaster`
  - `KiCad/kicad-symbols` (`Audio.lib`, for the PCM5100/5101/5102 pin table)
- The search index, which returns extracted text from documents whose own
  domains are blocked. Everything tagged `[DS-X]` came this way.

One document arrived intact and is worth knowing about: the adapter repository
vendors **MMA/AMEI RP-054**, the TRS-MIDI recommended practice, as
`references/rp54public.pdf`. Its text was extracted and read here. It is not
needed by this design (we use DIN-5) but it names **CA-033** as the normative
electrical specification and would be the document to follow if a 3.5 mm TRS
MIDI jack is ever wanted instead of a DIN — which, on a slot-bracket build, it
might be.

---

## 11. What this changes elsewhere

### In `hw/HARDWARE.md` — applied

| § | Change |
|---|---|
| 3.3 | **H11L1 output is pin 4, not pin 6.** Pull-up value changed from 4.7 kΩ to 1 kΩ with the arithmetic in § 4.4. Pointer to § 4 for the full circuit |
| 3.5 | Note that the PCM5102A needs no MCLK *and* no DC-blocking capacitor, with the § 6 pointer |
| 6.2 | The form-factor objection is retired — § 1.2. The SoM route now has no stated blocker left |
| 7 | New action items for the carrier: the § 3 table |

### In `hw/bom-testboard.csv` — applied

New rows for the parts §§ 4–7 name that were not costed: the THRU buffer, the
BSS138, the 2×13 header, the load switch, and the attenuator/filter passives.

### For `docs/` — for whoever owns it, not edited here

1. **`docs/PLAN.md` § 1, "Audio out" row** says *"line level, DC-blocked and
   attenuated for a WaveBlaster mixer input"*. The PCM5102A's output is
   ground-centred and **needs no DC blocking** (§ 6.1). Suggested wording: "line
   level, ground-centred, attenuated 6 dB for a WaveBlaster mixer input".
2. **`docs/BOARD_SPEC.md` OUT-1** carries the same "DC-blocked" phrase, same fix.
3. **`docs/BOARD_SPEC.md` § 1** says the daughterboard's MIDI in is "3.3 V TTL
   from the host card's MPU-401 decode". Via the **WaveBlaster header** it is
   **5 V**, not 3.3 V, and needs a level shifter (§ 7.4). Via a direct FPGA link
   it is 3.3 V. Those are two different front ends and the document should say
   which it means.
4. **`docs/BOARD_SPEC.md` § 1 and NG-5**: MIDI THRU is decided — **yes on the
   standalone box, no on the card** (§ 5), ~$0.46.
5. **`docs/BOARD_SPEC.md` needs a requirement about host reset.** § 1.3: the
   daughterboard takes seconds to boot and cannot honour WaveBlaster pin 26 the
   way a DB50XG does. That is observable behaviour and belongs in a behavioural
   specification.
6. **`docs/PROCUREMENT.md`**: when the SoM dev board is ordered, add *"and read
   the SoM hardware manual's pin-mux table and power-sequencing section
   into `hw/ref/`"* — § 3 rows 1–7 are the whole of what stands between this
   document and a schematic, and they arrive in the same box.
7. **`docs/PROCUREMENT.md` § 3** should add a **BSS138** (or any small-signal
   logic-level N-FET) and a **74LVC1G07** to the bench list, so the two MIDI
   front ends can both be breadboarded before the carrier exists.
8. `docs/background.md` § 3's WaveBlaster pinout is **confirmed correct** by
   three fabricated boards (§ 7.2), with two additions it did not have: pin 8 is
   MIDI OUT (not "unused"), and pins 12 and 16 are audio *inputs* (not
   "unused"). Its 10–12 dB attenuation guess is revised to 6 dB with reasons
   (§ 6.2).
