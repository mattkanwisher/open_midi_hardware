# Plan

Status: 2026-09-17, start of work. Nothing below is measured yet; the first
milestone exists to make one number true or false.

## State, 2026-09-17

All four workstreams have reported once. What is settled and what is not:

| | |
|---|---|
| **The gate is open, and now has numbers either side of it.** Still no measured RTF — that needs ROMs and silicon. But the armv7 instruction count is measured exactly, so the gate reduces to one unknown: whether an A7 sustains **≈0.75 IPC** on this code. Floor of 0.225 regardless. § 0 | **blocking everything** |
| DRAM parameters: resolved **and now compiled**. GPL source in mainline U-Boot, density auto-detected, four Kconfig numbers separate external from co-packaged DDR3. `boot/` builds three U-Boot targets green, ours included, and the defconfig that was published here did not build until this pass | closed |
| **Console pins gate copper.** Mainline's SPL does its console pinmux in C with exactly two arms — UART0 on **PE2/PE3** or UART3 on **PB6/PB7**; anything else is `#error`. U-Boot proper is DT-based and *will* drive PB8/PB9, so a board wired there gets a silent SPL and a talkative U-Boot — losing precisely the DRAM debug output you need when DRAM fails | **new, constrains the schematic** |
| DDR3 address/command routing: straight through, no swizzle, with the reasoning and the one-command efuse check in `hw/HARDWARE.md` § 5.4 | closed, pending that check |
| Power-up sequencing: **unread**. The vendor warns wrong timing destroys the part. Four PDFs to fetch, none reachable from this session | **gates copper** |
| Platform layer: designed, and its host harness builds and passes its own tests | closed for now |
| **The structure's own cliff is measured: RTF 1.00 ± 0.06**, and neither block size nor ring depth moves it. Transient tolerance is exactly `(depth − 1)` block periods. So when `bench/` finally returns a real RTF, the design decision is already made rather than needing another experiment | `emu/`, closed |
| The T113 implementation of the seam now **exists and cross-compiles** — `port/t113/`, every address double-sourced against mainline and against a bare-metal T113 firmware that runs on the part. Not one register has been written on silicon | written, unrun |
| Board versus module: **buy a vendor SoM first**. Within noise of our own board at qty 5, and the module vendor has already answered the routing and sequencing questions we cannot read | decided |
| The carrier for that module: specified as an interface contract in `hw/CARRIER.md`, with the SoM-independent half — MIDI front end, analogue stage, WaveBlaster connector, power budget — finished at component level, and 22 named facts that need a vendor document before copper | **as far as this network goes** |
| Daughterboard power: steady 1.66 W sits inside the only proven header envelope (a DB50XG's 2 W), but the 3.86 W turn-on peak does not. **Inrush limiting is a requirement, not a nicety** | new, `hw/CARRIER.md` § 9 |

Next three actions, in order: get ROM dumps and a T113 board and run `bench/rtf`;
read the efuse at SID+0x28 over FEL on whatever board arrives; fetch the four
hardware PDFs from a network that can reach them.

## 0. The gate

**Does `mt32emu` render MT-32 audio faster than real time on one Cortex-A7 at
1.2 GHz?**

Everything else is downstream of that number. The target is a real-time factor
of 0.5 to 0.6 or better on the *busiest* passage — not the average — so that
USB-free I/O, the display and MIDI parsing still fit, and so that a second synth
(FluidSynth, small font) remains conceivable later.

- RTF under ~0.6: proceed to a board.
- RTF 0.6 to 1.0: proceed, but single-synth only, and revisit the sample rate.
- RTF over 1.0: stop. Either the part is wrong or the project is a Pi again.

### What is now known, 2026-09-18

**The gate is still open, but it is no longer evidence-free.** Workstream A
measured the *instruction count* exactly, which is not timing but is not
nothing. On fabricated ROMs, with the partial count asserted rather than
assumed:

```
armv7-a instructions per output frame = 489.9 + 511.5 x (sounding partials)
```

— least squares over seven points, worst residual 0.03 %. At the MT-32's
maximum of 32 partials that is **16 859 instructions per frame** (reproduced
independently: 16 859.4 against the harness's 16 859.6).

One A7 core at 1.2 GHz has 37 500 cycles per frame at 32 kHz. So the whole
gate now reduces to **one unknown multiplier, the sustained IPC**:

| | Required IPC |
|---|---|
| RTF ≤ 0.6 (§ 0's target) | **0.749** — 0.61–0.80 across timbre mixes; **0.79** at 48 kHz |
| Real time at all (RTF ≤ 1.0) | **0.450** |
| Hard floor: RTF cannot be below this, since an A7 is at best partial-dual-issue | **0.225** |

And the fallback ladder is quantified by inverting the cost line — at IPC 0.80
all 32 partials fit inside RTF 0.6; at 0.70 the ceiling is about 30 partials;
at 0.60, 25; at 0.50, 21; at 0.40, 17.

**Every identified bias points the same way: the real figure will be worse.**
QEMU counts an L1 hit and a DRAM miss identically, and the synthetic workload is
unrealistically cache-friendly — 32 partials share one timbre and the fabricated
PCM ROM points every wave-map entry at a single 2 KiB window, where real partials
stride a decoded 512 KiB–1 MiB array. `bench/ANALYSIS.md` § 9 tabulates the
biases with their directions and is titled so that nobody quotes § 8 without it.

Measure on real silicon as soon as one is on the desk — a $15 to $25 T113 board
(100ask DongshanPI, MangoPi, a Forlinx eval, whatever is in stock) under Linux
is a perfectly good proxy for this measurement, because the question is cycles,
not the OS.

**And there is now a cheaper first step that needs no ROMs at all.** Running
`bench/rtf-synth` on any Cortex-A7 and dividing elapsed time by the instruction
counts above **yields the measured IPC directly** — the one missing multiplier —
on day one, with zero copyrighted material involved. The caveat travels with the
number: that IPC is itself an upper bound, because the synthetic working set is
far smaller than a real score's.

## 0.5 The method: emulate everything above the seam

Decided 2026-09-18. The project is built as two layers with a narrow interface
between them, and **only the lower layer requires hardware to test.**

| | Where it runs | How it is tested |
|---|---|---|
| **Above the seam** — `mt32emu`, the render loop, the MIDI parser, sysex reassembly, ROM and config loading, the ring and its underrun accounting, the control logic | Anywhere | Host build, armv7 under `qemu-user` (`port/host/test-armv7.sh`), bare-metal Cortex-A7 under `qemu-system-arm -M virt` (`emu/`) |
| **Below the seam** — DRAM init, I²S with DMA, the SMHC/SD controller, UART, the generic timer, GIC wiring, pinmux, clocks | T113 only | Real silicon. QEMU has no model of this SoC and never will |

The interface is `port/include`: nine headers, about forty functions, with two
implementations — one for QEMU `virt` (PL011, generic timer, a timer-driven
audio sink) and one for the T113. The RTOS-shaped part of it is a single
function, `mtp_audio_wait()`.

Three rules follow, and they are what make this work rather than merely sound
tidy:

1. **Nothing above the seam may know which platform it is on.** No `#ifdef T113`
   above the line. If something needs to know, the seam is in the wrong place.
2. **Every implementation passes the same conformance tests** — the counter
   contract in `port/host/test.sh`: messages parsed, sysexes reassembled,
   underruns, orphan bytes, oversize sysex refused. A platform that passes those
   in QEMU and fails them on silicon has a driver bug, and the test says which.
   **And the rendered audio is compared sample-for-sample, not just the
   counters**, by `desktop/conform.sh`, which drives the same MIDI bytes through
   the host, the armv7 cross build under `qemu-user`, the bare-metal image under
   `qemu-system-arm`, and the desktop build, and diffs the PCM. That is worth
   having as a separate rule because it has already caught something the counter
   contract could not: a floating-point contraction difference that made ARM and
   x86 disagree by 1 LSB (§ 4, and `port/PORTING.md` § 4.2).

   **Rule 2 has a sibling, added 2026-09-18.** All four builds are the same code,
   so a mistake they *share* is invisible to them. `desktop/ab.sh` renders the
   same events through an **independent front end** — one that uses the engine
   seam directly and none of the MIDI parser, the render loop or the ring — and
   compares the audio. They currently agree sample for sample. It is the only
   check that can catch a bug living in the shared code itself, and it is free.

   That rig also settled something worth knowing before anyone proposes "play it
   through both and listen": **two real-time renderings of the same file cannot
   be subtracted.** Three identical runs gave two bit-identical results and one
   differing on 16.9 % of samples, because an event landed in a different
   128-frame block — the MIDI source is paced by the clock and the render loop
   by the device. Offline, event-exact rendering is the only repeatable
   comparison.
3. **Keep the lower layer thin, because it is the expensive one.** Every function
   added below the seam is a function that can only be debugged with a scope and
   a board in hand. When there is a choice, push logic upward.

What this buys: by the time hardware arrives, everything except the drivers and
the speed has already been wrong once and fixed. What it does not buy: QEMU's
TCG models neither the A7 pipeline nor its caches, so **no timing evidence comes
out of emulation at all**, and § 0's gate still needs a real board and real ROMs.

## 1. What we are building

| | |
|---|---|
| SoC | Allwinner T113-i: 2× Cortex-A7 @ 1.2 GHz, HiFi4 DSP, RISC-V E907, external DDR3 up to 2 GB, LFBGA, industrial temperature |
| DRAM | One x16 4 Gbit DDR3L (512 MB). 256 MB is likely enough — MT-32 alone wants about 16 MB, the rest is SoundFont |
| Audio out | I²S to a PCM5102A, line level, **ground-centred** (the DirectPath output needs no DC block — `hw/CARRIER.md` § 6.3) and attenuated 6 dB for a WaveBlaster mixer input |
| MIDI in | UART at 31 250 baud, in **three** front ends, not two: DIN via optocoupler on the standalone build; **5 V** TTL on WaveBlaster pin 4, level-shifted, when the card sits in a generic host; 3.3 V direct from the FPGA's MPU-401 decode when it sits on this project's own card. The middle one was previously conflated with the last — `hw/CARRIER.md` § 7.4. **No USB**, by choice — that is what keeps a non-Linux platform tractable |
| Storage | microSD: ROMs, SoundFonts, config |
| Software | U-Boot or awboot for DDR and clocks; RT-Thread or bare metal above it; `mt32emu` as the synth |

## 2. Workstreams

Four, run in parallel, each owning its own directory.

### A. `bench/` — the real-time-factor harness
Vendor Munt. Build `mt32emu` as a static library for the host and cross-built for
`armv7-a`, NEON, hard float. Write a headless renderer that takes a SMF or a
captured MIDI stream plus ROM paths and reports RTF, peak partial count, and
per-buffer worst case. Report the static footprint — text, data, bss, heap high
water — because it decides whether the ROMs and state fit where we want them.
No ROMs in the repo; the harness takes paths and skips cleanly without them.

### B. `hw/` — the test board
Minimum viable T113-i board: SoC, one DDR3, PMIC or discrete rails in the right
sequence, microSD, USB for FEL, UART console, I²S header, MIDI in. Read the
reference designs that exist (Forlinx SoM, 100ask, MangoPi) rather than deriving
power sequencing from the datasheet. Answer: layer count, whether JLCPCB will
assemble the LFBGA and at what setup cost, and what the board comes to at qty 5.

### C. `boot/` — bring-up path
What actually boots a T113-i with *external* DDR3: mainline U-Boot (T113 support
landed in v2024.01), awboot, xboot, and the vendor route. **Answered** in `boot/BRINGUP.md`: mainline U-Boot
v2024.01 or later, payload wrapped with `mkimage` **as `-O linux`** and started
with `bootm` rather than `go` — measured 2026-09-18, not merely read: after `go`
SCTLR reads `0x00c5187d` with the MMU and both caches on, after `bootm`
`0x00c50078` with them off. **And the payload arrives in non-secure Hyp unless
`CONFIG_ARMV7_BOOT_SEC_DEFAULT=y`**, which is the qualification the first
reading missed and which sent two `start.S` files astray. Developed over `xfel`
without touching an SD card.
Bare metal on day one, taking the FreeRTOS T113 port's GIC, timer, MMU and SMHC
scaffolding, which already runs on this silicon under a clean licence, and
writing the one driver nobody has: I²S with DMA.

### D. `port/` — the platform layer
What `mt32emu` needs from a platform: C++ runtime, allocator, float, and nothing
else. Then design the four things around it — an I²S DMA ring that never
underruns, a 31 250-baud UART MIDI parser, FAT on SD for ROMs, and the render
loop and its latency budget. Decide RT-Thread versus a superloop on evidence, not
taste. Produce the header-level platform interface and a stub implementation that
builds against the host, so `port/` can be exercised before silicon exists.

## 3. Sequencing

1. **A alone gates everything.** B, C and D are research and design that can
   proceed in parallel, but no board is ordered until A reports.
2. Buy a cheap T113 dev board early, whatever the paper says.
3. First silicon target is that dev board, not our own: console over UART, then
   audio out of its I²S, then MIDI in. Our board only has to be right after the
   software already works on someone else's.
4. Our board, qty 5, once B and C agree on power, boot and DDR.

Note on 3 and 4: with the SoM carrying the DDR3, **the carrier is a 4-layer
board with no impedance control and no length-matched routing** — materially
cheaper and faster to build than the bare-BGA test board `hw/HARDWARE.md` § 4.2
was costed for. The classic way to lose a month (§ 4 below) is deleted along
with it.

## 4. Risks

- **Munt is too slow on an A7.** The gate. Nothing else matters first.
- **DDR3 on a 4-layer board.** Length matching and impedance on a cheap stack-up
  is the classic way to lose a month. Copy a reference layout; do not invent.
- ~~**External-DDR3 T113-i has thinner community ground truth than the T113-S3.**~~
  **Resolved, 2026-09-17, by workstream C.** The DRAM init is GPL source in
  mainline U-Boot (`drivers/ram/sunxi/dram_sun20i_d1.c`, T113 landed in v2024.01),
  not an Allwinner blob. Density and geometry are auto-detected at training time,
  so a 2 Gbit and a 4 Gbit part share one defconfig, and the external-DDR3 delta
  against the in-package part is four Kconfig numbers. Two published external-DDR3
  T113-i boards use *different* values and both work, which means these are board
  trim rather than part constants. See `boot/BRINGUP.md`.
- **AC remapping now gates the PCB, and replaces the risk above.** `dram_tpr13`
  bit 18 selects which address/command swizzle table the PHY uses, overriding an
  efuse that only describes bond wiring on a co-packaged part. Our DDR3
  address/command routing must match whatever table we program. That is a
  schematic decision taken before fab, not a software tune, and it needs either a
  published T113-i board schematic or a part in hand to read the efuse.
- **The T113's audio PLL has no documented 48 kHz recipe, and the fallback
  everyone assumes exists does not help.** Found 2026-09-18 by workstream D.
  The I²S module clock's only parents are the four audio clocks — there is no
  path from PLL_PERIPH0. PLL_AUDIO1 is integer-N off 24 MHz and 24.576/24 =
  128/125, so the smallest integer multiple needs N = 128, i.e. 3072 MHz, above
  the driver's own 3000 MHz ceiling: it *cannot* reach the family. PLL_AUDIO0 is
  fractional-N, but mainline drives its sigma-delta modulator from a lookup
  table and the D1/T113 table has exactly one entry —
  `{ 90316800, 0xc001288d, m=6, n=22 }` (`ccu-sun20i-d1.c:172`), which is
  90.3168 MHz = 4 × 512 × 44 100. **The 44.1 kHz family is in the table and the
  48 kHz family is not**, on this part or on the D1.

  The port infers the missing entry — N = 40, fraction 0.96, pattern
  `0xc001eb85`, M = 10 (even, as `ccu-sun20i-d1.c:168` requires) → 983.04 MHz
  → 98.304 MHz → 24.576 MHz = 512 × 48 000 — from the fact that across four
  sunxi CCU drivers the pattern word tracks only the fractional part of N and
  never M, and that `ccu-sun50i-h616.c:227` contains that exact rate with that
  exact pattern and N. It is tagged `MTP_T113_UNVERIFIED`, printed at boot, and
  one `xfel write32` retries it on the first board.

  **Why this is a risk and not a to-do:** `port/DESIGN.md` § 2.1 names
  `AnalogOutputMode_COARSE` at 32 kHz as the fallback if the real-time factor
  disappoints — but **32 kHz is in the same 24.576 MHz family**, so it is no
  escape from a PLL problem at all. 44.1 kHz would need the `SampleRateConverter`
  that § 2.1 rules out on RTTI, `<iostream>` and a 32 KB stack buffer. If the
  inference is wrong, the options are: find the manual; take the resampler and
  its costs; or **clock the PCM5102A from its own crystal rather than from the
  SoC** — and that last one is copper. **So this is a decision for workstream B
  before the board is drawn, not a software tune afterwards.**

- **Bit-comparability across implementations is a compiler flag away from being
  lost, and was.** GCC's default `-ffp-contract=fast` fuses a multiply-accumulate
  on armv7 that x86 does not fuse, inside the one FIR every output sample passes
  through. Measured: 69 samples in 96 256 differing by 1 LSB, inaudible, and it
  would have quietly cost the cheapest strong test this project has — "the board
  renders the same bytes as the bench". `-ffp-contract=off` on every ARM build,
  `port/PORTING.md` § 4.2. Watch for the same class of thing whenever a new
  target or a new compiler arrives.

- **No USB host means no USB MIDI, ever, on this design.** That is a deliberate
  trade (§ 2.6 of the parent document) and it must stay deliberate.
- **Licensing.** `mt32emu` is LGPL 2.1; static linking carries obligations, so
  keep the boundary clean and publish what the licence requires. ROMs are
  Roland's and never enter this repository.
