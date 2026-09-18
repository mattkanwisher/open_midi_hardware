# How this project got here

One session, 2026-09-17 to 18. It began as the question "what would it take to
build a dedicated MIDI board emulator like mt32-pi?" and ended with a bare-metal
Cortex-A7 image running Munt, a costed board design, and a list of what only
hardware can answer. This is the record, including the parts that were wrong on
the way.

## 1. The question, and the finding that shaped everything

mt32-pi is a bare-metal Raspberry Pi image: Circle for the platform, **Munt**
for MT-32 and CM-32L emulation, **FluidSynth** for SoundFonts. The first real
finding was structural:

> **This is a CPU problem, not an FPGA problem.** Munt is a behavioural model of
> the LA32 plus the MT-32's firmware ROM, not a netlist you can put in fabric. A
> cycle-level FPGA MT-32 would need an MCS-96 core and the LA32 gate array, and
> no finished open core exists. Every shipping MT-32 emulator is Munt on a CPU.

The corollary, confirmed later: the MiSTer community's answer to MT-32 is to
bolt a Raspberry Pi onto the MiSTer. FPGA fabric earns its keep on the *bus*
side — MPU-401 decode, OPL3, a sample engine — not on LA synthesis.

Surveying the field showed something else. Almost every "MT-32 emulator" product
on sale — Serdaco's WP32 McCake and MP32L, BulkyMIDI-32, MIDI FORGE Maestro, the
MiSTer minis — is mt32-pi in a different box. They differ in connectors and case,
not synthesis. And **upstream mt32-pi is discontinued**: the author stepped back
citing sustained harassment and code theft. The live line is the
`metaneutrons` fork. Nuked-SC55, the best Sound Canvas emulation, was **archived
in September 2026** for similar reasons; its forks carry on.

## 2. Why not a Raspberry Pi

The Pi was the obvious answer until two 2026 realities collided with it.

**Supply.** The Zero 2 W was sold out at every usual retailer, with third-party
sellers asking over €100 for a $15 part.

**The DRAM crisis.** LPDDR4 up roughly sevenfold since 2025; three Raspberry Pi
price rises in four months; the 16 GB Pi 5 from $120 to $305. The CM4 Lite that
looked like the answer at its $25 launch price now lists at **$41.25** and
retails near **$84**. Only the older LPDDR2 parts — Zero 2 W, Pi 3 A+, 3 B+ —
kept their prices, and those are the scarce ones.

Three corrections happened here, all of them worth recording because each was
stated confidently first:

| Claimed | Actually |
|---|---|
| "A 2 Gbit DDR3 part is cheaper, and in this market that matters" | The 2 Gbit part is **$10.46–12.19** against **$6.53** for the 4 Gbit. Half the memory costs more than twice the memory |
| "SF3 compression cuts SoundFont RAM five to ten fold" | It cuts the *file*. FluidSynth decompresses and caches the samples, so residency is unchanged unless dynamic sample loading is on |
| "Munt's working set is 1–2 MB, inside an i.MX RT1170's SRAM" | Measured: **1.49 MiB** for MT-32, **2.49 MiB** for CM-32L. An RT1170 holds one and not the other |

The RAM question also resolved cleanly: **MT-32 alone is happy in about 16 MB.**
Everything above that is the SoundFont, because FluidSynth loads sample data into
RAM — so 512 MB covers every font in common use, and a gigabyte buys only the
419 MB giants. The CM4's 1 GB was never a requirement; it is the smallest SKU
that part is sold in.

## 3. Why not an RTOS port of mt32-pi

Two questions came up in sequence, and both have the same shape of answer.

**"Why can't mt32-pi be ported to another ARM chip?"** Because the portable part
is already portable. Munt and FluidSynth are ordinary C++. What is Pi-specific is
**Circle**, which is not a HAL but a whole bare-metal environment, and mt32-pi
calls its classes directly. A port means writing startup, MMU, interrupts, I²S
DMA, SD and FAT, and a **USB host stack** for the new SoC.

**"Would FreeRTOS be better?"** FreeRTOS is a scheduler, not a BSP. It supplies
none of that list. It removes the easy tenth of the port and leaves the rest —
and for a single hard-real-time render loop, preemption adds jitter rather than
removing it.

The decision rule that fell out, and that the whole project now rests on: **USB
is the only expensive driver.** Drop USB MIDI — which a WaveBlaster daughterboard
fed by an FPGA's MPU-401 decode does not need — and a non-Linux platform becomes
tractable. Keep it, and take Linux.

## 4. The part: Allwinner T113-i

Priced from LCSC in September 2026: **T113-i at $4.84** (dual Cortex-A7 1.2 GHz,
HiFi4 DSP, RISC-V E907, external DDR3, LFBGA) against **RK3308B at $4.08**,
H616 at $6.73, H3 at $7.08, i.MX6ULL at $10.80–16.30. The T113-i won on family:
its sibling was already qualified in the parent project's BOM.

The comparison that says the most about 2026: the **T113-S3 costs $18.39** with
128 MB co-packaged, while the **T113-i is $4.84** and takes 512 MB beside it for
$6.53. Roughly $13.50 of that part is its in-package DRAM.

## 5. What five agents found

Work was split across parallel Opus 5 agents, each owning one directory.

### `bench/` — the gate
A headless real-time-factor harness; mt32emu built for host and cross-built for
Cortex-A7 with NEON. **No RTF number was produced, and none was invented**: MT-32
ROMs are copyrighted and absent, so the harness refuses to run. From reading the
render path: the **float renderer is disqualified** (nine `exp`, three `sin`,
three `cos` and an `fmod` in the per-sample function), and **NEON does not help
the synthesis** — GCC emits no vector registers on the render path, because the
LA32 is a sequential state machine whose core operation is a table gather.

> **Superseded in part, 2026-09-18, by a second pass that could run things.**
> Three corrections, in the house style of § 2:
>
> | Claimed | Actually |
> |---|---|
> | "Emulation produces no timing evidence at all" (§ 7 below) | Too strong. Emulation produces an exact **instruction count**, which is not timing but is not nothing. 16 859 armv7 instructions per frame at 32 partials, so the gate reduces to one unknown multiplier — whether an A7 sustains ≈0.75 IPC |
> | The float renderer is "several times slower" | **1.6×** the instructions, not several times. The conclusion survives on other grounds — a serial double-precision VFP dependency chain on an in-order core, and double the temp buffers and reverb lines — but the sentence overstated it |
> | "NEON does not help" | True of the render path (−0.03 % at 32 partials, and what little it does is in `muteSampleBuffer`). But it gives a **5.76× speed-up on PCM ROM loading** — 4.25 against 24.50 instructions per ROM byte. NEON pays for itself once, at boot, and never again |
>
> Two of the second pass's own bugs are worth recording, because both produced
> *plausible* numbers: `setReverbEnabled(false)` right after `open()` is
> silently undone by a later System Area write, so reverb-on and reverb-off
> first measured identical; and `getPartialStates()` packs four partials per
> byte, which made a 32-partial workload read as 8. The harness now asserts its
> own configuration rather than trusting it.

### `hw/` — the board
About **$19 of parts**, $38–47 a board at qty 5. Single-channel 16-bit DRAM
controller means one chip point-to-point, **no VTT rail and no termination**. The
BROM's SD → SPI → FEL order makes boot flash optional and the board unbrickable.
JLCPCB's *default* 4-layer stack-up cannot escape a fine-pitch BGA at 50 Ω; the
JLC2313 stack-up must be named. Recommendation: **a vendor SoM first**, because
at qty 5 it costs the same and deletes three risks at once.

### `boot/` — bring-up
The project's stated biggest risk dissolved: T113 DRAM init is **GPL source in
mainline U-Boot since v2024.01**, not an Allwinner blob. Density and geometry are
auto-detected, so 2 Gbit and 4 Gbit share one defconfig, and the external-DDR3
delta is four Kconfig numbers — and two published external-DDR3 boards use
*different* values and both work, so they are board trim, not part constants.
Handover must be **`bootm`, not `go`**: only `bootm` calls
`cleanup_before_linux()`, so only `bootm` arrives with the MMU and D-cache off.

A sharper risk replaced it: **AC remapping**. A `tpr13` bit selects which
address/command swizzle the PHY uses, overriding an efuse that only describes
bond wiring on a co-packaged part. No published T113-i schematic was reachable,
so the rule was derived from the driver instead — **route A0–A15, BA0–BA2, RAS,
CAS, WE straight through** — on the reasoning that the PHY's no-write state is
straight-through, and that a wrong table costs a one-line driver patch while
wrong copper costs a respin. One command on the first board settles it.

### `port/` — the platform layer
mt32emu needs less than feared: **C++98, no STL containers, twenty external
symbols**, exceptions and RTTI both disposable. Two things bite. Its MIDI queue
synchronises with `volatile` alone — fine on x86, **not on a weakly-ordered
dual-core A7** — so MIDI and rendering stay on one core. And it **allocates on
the render thread** when a game changes reverb mode; preallocating takes that to
exactly zero. Latency budget: **~4.7 ms typical, ~8.8 ms worst**, of which only
0.75 ms is non-negotiable.

### `emu/` and `desktop/` — two more implementations of the same seam
The bare-metal image **boots under `qemu-system-arm -M virt -cpu cortex-a7`**,
and **mt32emu opens a `Synth` and renders on it** — 194 KB image, 1571 KiB heap,
**zero bytes of heap growth while rendering**. The one-second render is
**byte-identical PCM** on x86-64 and on bare-metal ARM. A virtio-sound driver
(written after the user asked for a real audio device, overruling an earlier call
of mine) produced a finding that transfers to hardware: **ring depth has a second
constraint** — the consumer's service granularity — that the design had not
accounted for.

The desktop build runs the same code on a sound card with four MIDI sources,
including a **tty at 31250 baud**, which is exactly what the hardware will do.

## 6. The method that emerged

The architecture rule now in `docs/PLAN.md` § 0.5, which the user framed and the
work then validated:

> Everything above the platform seam is tested on the host, as armv7 under
> qemu-user, and bare metal under QEMU. Only what is below it — DRAM init, I²S
> and DMA, SD, UART, timer, GIC, pinmux, clocks — needs silicon.

Four implementations of that seam now exist: host, bare-metal QEMU, desktop, and
the T113 one that does not. The same conformance assertions pass on all three
that do, so when the fourth disagrees, the difference is a driver and the test
names it.

## 7. What is still unknown

1. **The gate.** Does mt32emu render faster than real time on one 1.2 GHz
   Cortex-A7? Needs ROM dumps and a board. Nothing else matters first, and no
   amount of further *reading* will settle it — though **running** it under
   emulation turned out to settle more than this sentence originally allowed:
   the instruction count is exact, and the question is now precisely "does an
   A7 sustain 0.75 IPC on this code?" See `docs/PLAN.md` § 0 and the correction
   above. A cheap first measurement needs no ROMs at all.
2. **The power-up sequence**, unread. The vendor warns that wrong timing destroys
   the part. Four PDFs, none reachable from this session's network.
3. **The AC remapping table**, one `xfel read32` away on the first board.
4. **Audibility.** Nothing has been through a speaker, and no ROM has been loaded.

## 8. Honest limits of this record

Prices come from search-result extraction, not from LCSC or JLCPCB pages —
both are blocked from this environment, as are the vendor sites holding the
schematics and the datasheet. The macOS half of the desktop build has never been
compiled. The ARM binaries ran under QEMU, which models neither the A7 pipeline
nor its caches. Every document in this repository tags its claims by evidence
class; where something is inferred, it says so.
