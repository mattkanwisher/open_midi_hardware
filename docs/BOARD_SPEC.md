# What the board does

A behavioural specification of the mt32-t113 sound module, written to be read by
someone — or something — that did not sit through the design sessions.

This document says **what the finished device does**, observably, from outside.
It does not say how. Where a requirement exists because of an implementation
finding, it cites the document that holds the evidence, and you should go read
that before arguing with the requirement.

- `docs/PLAN.md` — the plan, the gate, and the platform seam
- `docs/HISTORY.md` — how the project got here, including what was wrong on the way
- `port/DESIGN.md` — the platform design: audio path, MIDI path, latency budget
- `port/PORTING.md` — what `mt32emu` needs and what it does that is awkward
- `hw/HARDWARE.md` — the board
- `boot/BRINGUP.md` — how it starts
- `bench/ANALYSIS.md` — the real-time-factor gate

---

## 0. How to read this, if you are an AI continuing the work

Four things about this repository, before anything else.

**1. It has an evidence culture, and it is the most important thing here.**
Every claim is tagged by how it is known. Requirements in this document carry the
same tags:

| Tag | Means |
|---|---|
| `[measured]` | A command was run in a container and produced this number. Reproducible |
| `[host]` | Verified by `port/host` on x86-64 |
| `[qemu]` | Verified bare-metal on Cortex-A7 under `qemu-system-arm -M virt` |
| `[read]` | Established by reading source, with a file:line citation |
| `[inferred]` | Reasoned from other facts. The reasoning is shown. May be wrong |
| `[silicon]` | Cannot be known until a T113 board exists. **No one may assert it** |
| `[open]` | Not decided yet. Do not quietly decide it — say you are deciding it |

**Never invent a number.** If you do not have one, the correct output is
`[open]` and a sentence saying what would settle it. `docs/HISTORY.md` § 2
records three confident claims that were wrong, and § 8 lists what this project
does not know. That honesty is load-bearing: it is why the findings can be
trusted at all.

**2. The gate is still open.** Nobody has measured whether `mt32emu` renders
faster than real time on a 1.2 GHz Cortex-A7, because that needs ROM dumps and a
board. Everything in this document is conditional on that number. Do not write
an RTF figure anywhere unless you ran the harness on real Cortex-A7 silicon.

**3. There are no ROMs and there will never be any.** MT-32 and CM-32L ROMs are
Roland's copyright. They are not in this repository, they may not be added, and
`mt32emu` verifies them by SHA-1 so an approximation does not work. Code paths
that synthesise fake ROMs exist for structural testing and must be labelled as
such everywhere they appear.

**4. Respect the seam.** `docs/PLAN.md` § 0.5 splits the system in two, and
`port/include` is the interface. Nothing above the seam may know which platform
it is on — no `#ifdef T113` above the line. If you find you need one, the seam is
in the wrong place, and saying so is a better contribution than the `#ifdef`.

---

## 1. What the thing is

An MT-32 sound module. A game or a sequencer sends it MIDI; it makes the noises
a Roland MT-32 would have made, in analogue, out of a jack.

It is built on an Allwinner T113-i — two Cortex-A7 cores at 1.2 GHz with
external DDR3 — running `mt32emu` (Munt, the same engine mt32-pi uses) on bare
metal. No Linux. No USB.

Two intended physical shapes, sharing one firmware:

| | **Standalone** | **Daughterboard** |
|---|---|---|
| Form | A box | WaveBlaster-format card on a host sound card |
| MIDI in | DIN-5, opto-isolated | **5 V** TTL on WaveBlaster pin 4, level-shifted. (A *direct* FPGA link, bypassing the header, is 3.3 V — two different front ends, `hw/CARRIER.md` § 7.4) |
| Audio out | Line-level jacks | Analogue pins into the host card's mixer, attenuated 6 dB |
| Power | Its own supply | From the host connector — a hard constraint, see `hw/` |

The firmware difference between them is the MIDI front end and the output
attenuation. Both are `[open]` at board level until `hw/CARRIER.md` closes.

### 1.1 Non-goals, deliberately

These are not omissions. Each is a decision with a reason, and reversing one is
a project-level decision, not a feature request.

| | |
|---|---|
| **NG-1. No USB, ever, on this design** | USB host is the only expensive driver in a non-Linux port. Dropping it is precisely what makes bare metal tractable — `docs/HISTORY.md` § 3. It also means **no USB MIDI**, forever. If someone wants USB MIDI, they want a different architecture, and they should be told so plainly |
| **NG-2. No General MIDI in v1** | FluidSynth and a SoundFont are a second synth and a second memory problem. The RAM is specified for it (§ 7) but the firmware does not do it yet |
| **NG-3. No network, no FTP** | mt32-pi has these. They are why mt32-pi needs USB and a network stack |
| **NG-4. No display, no buttons, no encoder in v1** | `port/DESIGN.md` § 5 keeps them out of the platform interface on purpose: an abstraction for a feature that does not exist is how a 40-function interface becomes a 200-function one nobody can port. § 6.3 names the display as the trigger for adopting an RTOS, when it comes |
| **NG-5. No MIDI OUT** | Not in the interface, and nothing above the seam can produce one |
| **NG-6. MIDI THRU: yes on the standalone box, no on the card** | Decided 2026-09-18. THRU is pure hardware — it never enters the firmware — and costs ≈ $0.46. It is worth having here specifically *because* of the H11L1: a Schmitt-output opto adds ~0.4 % of a bit time per hop where a 6N138 design adds microseconds, so this box can be daisy-chained with a clear conscience. The card has no DIN to THRU to. `hw/CARRIER.md` § 5 |

---

## 2. External interfaces

### 2.1 MIDI in

| ID | Requirement | Evidence |
|---|---|---|
| **IN-1** | Accepts serial MIDI at **31 250 baud, 8N1**, per the MIDI 1.0 electrical specification | `[read]` |
| **IN-2** | The standalone build's DIN input **MUST be opto-isolated**. This is mandatory in the MIDI specification, not a nicety — it is what stops a ground loop between a PC and an amplifier | `[read]` |
| **IN-3** | The device MUST tolerate a stream that starts mid-message, i.e. being powered up while a host is already transmitting | `[host]` |
| **IN-4** | The device MUST NOT require any handshake, initialisation message, or device-ID query before it will sound | `[host]` |

### 2.2 Audio out

| ID | Requirement | Evidence |
|---|---|---|
| **OUT-1** | Stereo, **48 kHz**, 16-bit, via I²S to a PCM5102A, at line level. The PCM5102A's DirectPath output is **ground-centred and needs no DC-blocking capacitor** — fitting one would require a bipolar part at ±3 V and would add distortion for nothing. Keep the footprint, populate it with 0 Ω | `[read]` `hw/CARRIER.md` § 6.3. **Corrected 2026-09-18**; this table and `docs/PLAN.md` § 1 both said "DC-blocked" and both were wrong |
| **OUT-2** | 48 kHz is produced by `mt32emu` **directly**, using `AnalogOutputMode_ACCURATE`, which upsamples inside the emulated analogue stage. **There MUST be no sample-rate converter anywhere in the system** | `[read]` `Synth.cpp:294-298` |
| **OUT-3** | Fallback **if the real-time factor demands it**: `AnalogOutputMode_COARSE` at 32 kHz, I²S at 32 kHz, still with no resampler | `[read]` |
| **OUT-4** | **That fallback does not rescue a clock problem.** 32 kHz and 48 kHz are both in the 24.576 MHz family, so if the T113's audio PLL cannot be made to produce it, dropping to 32 kHz changes nothing. The escape routes are a resampler (ruled out on RTTI and a 32 KB stack buffer) or clocking the DAC from its own crystal — **which is copper, so it is a decision before the board, not after** | `[inferred]` `docs/PLAN.md` § 4; the PLL recipe is `MTP_T113_UNVERIFIED` in `port/t113/src/ccu.c` |
| **OUT-5** | The I²S output **MUST run continuously from boot**, at the configured rate, in every state including every failure state | `[read]` `port/DESIGN.md` § 4.3 |
| **OUT-6** | When there is nothing to play the output **MUST be digital silence, never a stuck DC level**, and never a hang | `[read]` |

### 2.3 Storage

| ID | Requirement | Evidence |
|---|---|---|
| **ST-1** | One microSD card, FAT, holding ROMs and configuration | `[read]` |
| **ST-2** | The card is read **at boot only**. Nothing reads the SD card while audio is rendering | `[read]` `port/DESIGN.md` § 6.3 |
| **ST-3** | The device MUST boot, run, and accept MIDI **with no card present at all** | `[read]` § 5 below |

### 2.4 Console

| ID | Requirement | Evidence |
|---|---|---|
| **CON-1** | A UART console MUST come up before the SD card is touched, and MUST stay up in every failure state | `[read]` `port/DESIGN.md` § 4.3 |
| **CON-2** | The console is the only diagnostic channel this device has. Anything a user might need to know MUST be visible there | `[inferred]` — follows from NG-4 |
| **CON-3** | **The console MUST be on UART0 at PE2/PE3, or UART3 at PB6/PB7. Nothing else.** This is a schematic constraint, not a software preference. Mainline's SPL sets its console pinmux in C, not from the device tree, and has exactly those two arms for this SoC — anything else is a `#error`. U-Boot *proper* is device-tree driven and will happily use other pins, so a board wired elsewhere boots with a **silent SPL and a talkative U-Boot**: you lose exactly the DRAM training output you need at the moment DRAM is what failed | `[read]` `arch/arm/mach-sunxi/board.c:160-195`, `boot/BRINGUP.md` § 6.2 |

### 2.5 Boot media

| ID | Requirement | Evidence |
|---|---|---|
| **BM-1** | The SoC BROM tries **SD, then SPI, then FEL**. Because FEL is last and always available over USB, boot flash is optional and **the board cannot be bricked by a bad image** | `[read]` `boot/BRINGUP.md` |
| **BM-2** | The USB port is for FEL recovery and development only. It is **not** a MIDI port and never will be — see NG-1 | `[read]` |

### 2.6 Host reset — daughterboard only

WaveBlaster pin 26 is an active-low reset, and every reachable daughterboard
design ties it to its synth chip's reset. A DB50XG-class synth is making sound
again milliseconds after the line is released.

**This device cannot do that, and must not pretend to.** It is a SoC that boots
a bootloader, initialises DDR3, reads an SD card, SHA-1s a ROM and opens a
synth. That is seconds, not milliseconds.

| ID | Requirement | Evidence |
|---|---|---|
| **RST-1** | Pin 26 **MUST NOT** be wired to the SoM's reset. A host that pulses it during a DOS game's initialisation would take the module off the air for the length of a full boot | `[inferred]` `hw/CARRIER.md` § 1.3 |
| **RST-2** | Pin 26 is brought in level-shifted as a **GPIO input**, and firmware decides what it means. The sane reading is "the host says reinitialise": reset the synth's MIDI state, not the SoC | `[open]` — the firmware behaviour is proposed, not implemented |
| **RST-3** | The seconds-to-first-sound behaviour is **observable product behaviour** and MUST be stated in the product description. A module that behaves differently from a DB50XG here will otherwise be reported as broken | `[inferred]` |

---

## 3. Power-on behaviour

### 3.1 The sequence

```
power
  └─ BROM               SD → SPI → FEL
      └─ U-Boot SPL     DDR3 init, clocks
          └─ U-Boot     loads the payload
              └─ bootm  ── MMU off, D-cache off ──> firmware
                  │
                  ├─ 1. timer, GIC, UART console.  Banner printed.
                  ├─ 2. mount SD                   (may fail — see § 5)
                  ├─ 3. read /mt32.cfg             (may be absent — defaults)
                  ├─ 4. load ROMs                  (may fail — see § 5)
                  ├─ 5. open the synth
                  ├─ 6. start I²S and its DMA
                  ├─ 7. start the MIDI UART
                  └─ 8. render loop, forever
```

| ID | Requirement | Evidence |
|---|---|---|
| **PWR-1** | Handover from the bootloader **MUST be `bootm`, not `go`** | **`[measured]` 2026-09-18**, no longer inferred. A probe payload booted under a real sunxi U-Boot reports SCTLR `0x00c5187d` after `go` — MMU, D-cache and I-cache all **on** — against `0x00c50078` after `bootm`, all off. `cleanup_before_linux()` also turns the I-cache off, which the first reading missed |
| **PWR-1a** | The image **MUST be wrapped as `-O linux`.** `bootm` dispatches on `ih_os`, and `-O u-boot` takes `do_bootm_standalone`, a plain call — as bad as `go` | `[read]` `boot/bootm_os.c:529-533` |
| **PWR-1b** | **The firmware may be entered in non-secure Hyp mode, not SVC**, and must handle both. Mainline U-Boot defaults `ARMV7_NONSEC=y` and `ARMV7_VIRT=y` for sunxi (`ARMV7_BOOT_SEC_DEFAULT` is `default y if ARCH_TEGRA`, so off here), and its secure monitor selects Hyp on a virtualisation-capable core — which the A7 is. **Measured: `bootm` gives CPSR `0x600001da`, mode `0x1a`.** A `msr cpsr_c` *cannot* leave Hyp; the write is ignored. Startup code must detect Hyp and `eret` out of it | **`[measured]`** — and both startup files got this wrong until 2026-09-18. See § 9 trap 15 |
| **PWR-2** | The synth is opened **before** the DAC starts (step 5 before step 6), so the first block of audio out is real rather than a ramp from silence | `[read]` `port/DESIGN.md` § 4.1 |
| **PWR-3** | The console banner MUST appear before any step that can fail | `[read]` |
| **PWR-4** | On a successful open, the console MUST log the ROM descriptions `mt32emu` returns — e.g. `MT-32 Control v1.07 + MT-32 PCM ROM`. That one line answers most support questions | `[read]` `ROMInfo::description` |
| **PWR-5** | Boot-to-first-sound time | `[open]` — no target agreed, nothing measured. SHA-1 over a 1 MB PCM ROM plus the SD read dominates |

### 3.2 What the firmware is handed

| ID | | Evidence |
|---|---|---|
| **HW-1** | Working DDR3. The firmware does **not** do DRAM init; U-Boot does | `[read]` |
| **HW-2** | MMU off, D-cache off, as a consequence of PWR-1 | `[read]` |
| **HW-4** | The I²S block is **I²S1**, not I²S0. I²S0 feeds the on-chip codec, is absent from all of mainline, and appears only in a vendor HAL carrying no licence grant. I²S1 has a device-tree node, a driver binding, a DMA port number and a pin table | `[read]` `port/T113.md` |
| **HW-3** | Whether the bootloader or the firmware configures the **audio PLL** | **Answered 2026-09-18: the firmware does.** So the non-cacheable ring mapping is ours to configure, and `port/t113/src/mmu.c` configures it — a 1 MB Normal Non-cacheable window for the ring and the DMA descriptors. The PLL recipe itself is still inferred, see OUT-4 |

---

## 4. MIDI behaviour

The parser is this project's own (`port/src/mtp_midi_parser.c`), not `mt32emu`'s
— see § 9 trap 2 for why. Every requirement below is asserted in
`port/host/test.sh` and passes.

| ID | Requirement | Evidence |
|---|---|---|
| **MIDI-1** | **Running status** MUST be honoured. A held arpeggio is two bytes per note and most of a game's note stream is running status | `[host]` |
| **MIDI-2** | System Common (0xF1–0xF6) **clears** running status; channel messages set it; sysex clears it | `[host]` |
| **MIDI-3** | **System Real Time bytes (0xF8–0xFF) MUST be accepted anywhere**, including in the middle of another message and inside a sysex, and MUST be emitted immediately without disturbing the pending message or the open sysex. A PC's MPU-401 emits Active Sensing every ~300 ms and this is not optional | `[host]` |
| **MIDI-4** | **Sysex MUST reassemble across arbitrary fragmentation.** An MT-32 timbre bank dump is ~16 kB, which is **5.25 s of wire time** at 31 250 baud and roughly two thousand audio blocks. Audio MUST keep flowing throughout | `[host]` — tested with 64 × 254-byte sysexes interleaved with notes and Active Sensing, zero errors |
| **MIDI-5** | A **sysex that never ends** is aborted by the next status byte, counted, and the stream resynchronises on that byte. This is what hardware does | `[host]` |
| **MIDI-6** | A **sysex longer than 32 768 bytes** is refused, counted, and **MUST NOT be forwarded**. Half a patch dump fails `mt32emu`'s checksum and produces `SysEx error!`, which is a worse failure than silence. 32 768 is the right cap because it is `mt32emu`'s own, chosen because the hardware units have 32 K of RAM | `[host]`, `[read]` `globals.h:119-122` |
| **MIDI-7** | **Data bytes with no status** — we powered up mid-stream — are counted and discarded until a status byte appears | `[host]` |
| **MIDI-8** | The port **does not filter by channel**. Channel and part assignment are `mt32emu`'s, matching the MT-32's own behaviour | `[read]` |
| **MIDI-9** | Events are handed to the synth **before** each render block, not after, so a message arriving a microsecond before a block lands in that block | `[read]` `port/DESIGN.md` § 2.4 |
| **MIDI-10** | `MIDIDelayMode` MUST be `IMMEDIATE`. The library's default adds ~0.77 ms for a 3-byte message to emulate time on a MIDI cable — but our bytes **actually spent that time on a real MIDI cable**. Counting it twice is a bug, not authenticity | `[read]` `Synth.cpp:44,323,1109-1118` |
| **MIDI-11** | A refused **short message** is counted and dropped. At 1024 queue entries this should not occur in practice | `[read]` |
| **MIDI-12** | A refused **sysex** is stashed and re-offered at the top of the next block, and MIDI draining pauses until it is accepted. Back-pressure propagates to the UART FIFO — which holds 82 ms of wire — rather than a patch dump being silently lost | `[host]` |
| **MIDI-13** | `onMIDIQueueOverflow()` **MUST return false.** Returning true makes the library spin inside itself on the render thread while the DMA drains underneath | `[read]` `Synth.cpp:1138-1141` |
| **MIDI-14** | **Active Sensing timeout.** A real MIDI device that has seen 0xFE and then stops seeing it is expected to silence its notes. Whether this device does that is **not specified and not implemented** | `[open]` |

---

## 5. Failure behaviour

This is the part of the specification most likely to be skipped and most
certain to be experienced. **The normal case is that the ROMs are missing**,
because they are Roland's and the repository will never ship them.

The governing rule:

> In every failing case the device still boots, still accepts MIDI, still runs
> the I²S at the configured rate, and still outputs silence — never a stuck DC
> level, never a hang. A module that is quiet and talkative over serial is
> debuggable; a module that is bricked is not.

| ID | Situation | Required behaviour | Evidence |
|---|---|---|---|
| **FAIL-1** | No card, or no FAT | Log it. Play nothing. Flash an LED in a distinctive pattern. **Do not hang** — the console must still come up so the card can be diagnosed | `[read]` |
| **FAIL-2** | Card present, `/mt32.cfg` missing | Built-in defaults, logged at INFO. **Not an error** — a card with only ROMs on it must work | `[read]` |
| **FAIL-3** | ROM file missing | Name **the exact path** that was not found, and list what *is* in `/roms`. "No such file" without the path is a bug report waiting to happen | `[read]` |
| **FAIL-4** | ROM present, not recognised | Say so, and say the likely cause: a half image. `mt32emu` knows about `FirstHalf`/`SecondHalf` pairs; a 32 KB control ROM is one half and needs its partner. Support the two-file merge, and **say which half you were given** | `[read]` `ROMInfo.h:37-48,108` |
| **FAIL-5** | `machine = cm32l` but only MT-32 ROMs present | Fall back to MT-32 **with a warning**, do not fail | `[read]` |
| **FAIL-6** | Underrun | Counted, logged, and the loop recovers — it MUST NOT drift permanently out of sync after one bad block | `[host]` `[qemu]` |
| **FAIL-7** | MIDI byte lost to FIFO overrun | Counted. A silent drop is the one failure mode that makes a MIDI module feel haunted | `[read]` |
| **FAIL-8** | Engine back-pressure on a **short message** | Currently counted and **dropped**, where a sysex is stashed and retried. A dropped All Notes Off is a note that hangs until power-cycle. Capacity is the engine queue depth per block period: **23× margin over DIN MIDI at queue 64, 369× at mt32emu's default 1024**, so it cannot occur on this product's wire — first loss measured at 700 000 baud | `[measured]` **`[open]`** — the asymmetry is now deliberate and documented, but not fixed; see `port/DESIGN.md` § 3.5 |

---

## 6. Timing

Measured from the last byte of a 3-byte Note On on the wire to an audible
sample, at 48 kHz, 128-frame blocks, ring depth 3, target occupancy 2.

| Stage | Typical | Worst | Negotiable? |
|---|---|---|---|
| Wire time, last byte, 10 bits @ 31 250 | 0.32 ms | 0.32 ms | **no** |
| RX interrupt to FIFO | <0.01 ms | 0.02 ms | no |
| Wait for the top of the next block | 1.33 ms | 2.67 ms | = block size |
| Block waits behind queued audio | 2.67 ms | 5.33 ms | = ring depth |
| I²S serialisation of one frame | 0.02 ms | 0.02 ms | no |
| PCM5102A interpolation filter group delay | ~0.4 ms | ~0.4 ms | **no** `[inferred]` — verify against the datasheet |
| **Total from the last byte** | **≈4.7 ms** | **≈8.8 ms** | |
| **Total from the first bit** | **≈5.4 ms** | **≈9.4 ms** | |

| ID | Requirement | Evidence |
|---|---|---|
| **LAT-1** | Of that budget, only **~0.75 ms is non-negotiable** — wire time, DAC group delay, and the I²S frame. Everything else is block size and ring depth | `[read]` |
| **LAT-2** | Block size and ring depth are **the only two numbers in this design that should move in response to a measurement**. Both are runtime parameters and the harness takes both as arguments | `[read]` |
| **LAT-3** | Ring depth is simultaneously the latency knob and the robustness knob: every block of ring is 2.67 ms of latency and 2.67 ms of tolerance for a slow render | `[measured]` — tolerance is exactly `(depth − 1)` block periods, confirmed in all 25 cells of an injected-stall sweep |
| **LAT-6** | **Ring depth does NOT rescue a real-time factor above 1.** The cliff is at RTF 1.00 ± 0.06 and neither tunable parameter moves it — at RTF 1.07 a depth of 8 fails with the same 143 dropouts as a depth of 2. A ring absorbs a *transient*; an RTF above 1 is not a transient. Above ~0.95 the only moves are 32 kHz `COARSE` or fewer partials | `[measured]` `emu/tools/sweep.sh`, `port/DESIGN.md` § 2.2 |
| **LAT-7** | Depth has a second lower bound unrelated to the renderer: **`depth ≥ ceil(consumer_service_interval / block_period) + 1`**. Our depth of 3 is correct only because the T113's DMAC interrupts once per descriptor. Do not carry 3 to a different consumer without redoing that arithmetic | `[measured]` |
| **LAT-8** | The sink's deadline sequence **MUST be absolute**, never a reloaded countdown. Absolute recovers in exactly one period with no drift; a countdown makes every dropout permanently slow the audio clock **while still reporting zero underruns** | `[measured]` `port/include/mtp_audio.h` |
| **LAT-4** | The instantaneous margin is larger than the average: one block that overruns can eat `queued × 2.667 ms` — 5.3 ms at occupancy 2 — before the DMA runs dry | `[read]` |
| **LAT-5** | For context, a real MT-32's own MIDI-to-note latency is **commonly cited** at 10–20 ms. Nobody on this project has measured one. Treat it as folklore | `[open]` |

---

## 7. Resources

| ID | | Value | Evidence |
|---|---|---|---|
| **RES-1** | MT-32, int16, reverb preallocated | **1 525 KiB** | `[measured]` on x86-64 |
| **RES-2** | CM-32L, int16, reverb preallocated | **2 553 KiB** | `[measured]` on x86-64 |
| **RES-3** | This port's own — audio ring, MIDI FIFO, sysex and retry buffers, stack | ~100 KiB | `[measured]` |
| **RES-4** | Arena to allocate | **4 MB**, covering every configuration with room to spare | `[inferred]` |
| **RES-5** | High-water mark MUST be logged at boot so a regression is visible | `[read]` |
| **RES-6** | **It does not fit in on-chip SRAM.** The smallest defensible configuration is 869 KiB and the expanded PCM ROM alone is 512 KiB. **The T113 build needs the DDR3.** This contradicts a hope recorded earlier in the project, and the earlier hope is wrong | `[measured]` + `[read]` |
| **RES-7** | Heap growth while rendering MUST be **zero bytes**. Reverb must be preallocated, because otherwise `mt32emu` allocates on the render thread when a game changes reverb mode | `[qemu]` — measured at zero bare-metal |
| **RES-8** | RAM sizing for a future General MIDI mode is the SoundFont, not the synth: MT-32 alone is happy in about 16 MB; 512 MB covers every font in common use | `[read]` |

---

## 8. Observability

The device has no display and no buttons (NG-4), so the counters *are* the user
interface. They are also the conformance contract: `docs/PLAN.md` § 0.5 rule 2
says every implementation of the seam must pass the same assertions on them.

| ID | Counter | Why it exists |
|---|---|---|
| **OBS-1** | short messages parsed | |
| **OBS-2** | sysex messages reassembled | |
| **OBS-3** | underruns | Zero is the requirement |
| **OBS-4** | orphan data bytes | Powered up mid-stream, or a framing problem |
| **OBS-5** | sysex aborted / sysex truncated | Distinguishes a host that stopped mid-dump from one sending something too big |
| **OBS-6** | MIDI FIFO overruns | |
| **OBS-7** | `min_queued` — the lowest ring occupancy ever seen after a commit | **The margin, made visible.** Never below 1 means the loop never came within a block of an underrun. Touching 0 means you are one bad block from a click |
| **OBS-8** | heap high water | RES-5 |
| **OBS-12** | **Real-time bytes are invisible.** Active Sensing and MIDI Clock are forwarded to the engine but not counted in `short_msgs`, so they occupy queue slots no counter above the seam can see — and an MPU-401 sends Active Sensing every 300 ms forever | `[measured]` **`[open]`** |

| ID | Requirement | Evidence |
|---|---|---|
| **OBS-9** | `min_queued` SHOULD be logged every few seconds on the target | `[read]` |
| **OBS-10** | All implementations of the seam MUST agree on every counter for the same input | `[host]` `[qemu]` — verified across five builds by `desktop/conform.sh`: host x86-64, armv7 under `qemu-user`, the same with `-ffp-contract=off`, bare-metal Cortex-A7, and the desktop build. Every counter agreed on every run |
| **OBS-11** | They MUST also render **byte-identical PCM** for the same input. This is a stronger test than the counters and it is cheap | `[measured]` — byte-identical across all five on the fake engine; the one divergence found was floating-point contraction, now fixed by flag (§ 9 trap 14). **Still unverified: bare-metal ARM against x86 on non-silent *synthesiser* output**, because the bare-metal image's MIDI vectors are compiled in and do not yet include a stream that makes the fake-ROM engine sound |

---

## 9. Traps

Things that look like improvements and are not. Each of these was found the hard
way by someone in this project, and each has a citation.

1. **Do not add a sample-rate converter.** `mt32emu` renders internally at 32 kHz
   and says the output is meant to be resampled externally, which invites either
   a 32 kHz I²S clock or a resampler. Both are wrong: `AnalogOutputMode_ACCURATE`
   returns 48 kHz directly, and the upsampling is not overhead — it is the
   emulation of the MT-32's own reconstruction filter. `port/DESIGN.md` § 2.1.

2. **Do not use `mt32emu`'s `MidiStreamParser`.** It is a good parser, but it
   grows its buffer with `new Bit8u[]` on the first long sysex — a heap operation
   triggered by wire input — it pulls in `snprintf`, and it has one timestamp for
   the whole parser rather than per byte. `port/DESIGN.md` § 3.1.

3. **`MIDIDelayMode_IMMEDIATE`.** See MIDI-10. The default double-counts cable
   delay that our bytes really did spend on a cable.

4. **`onMIDIQueueOverflow()` returns false.** See MIDI-13.

5. **Do not move MIDI to the second core.** `mt32emu`'s MIDI queue synchronises
   with `volatile` alone. That is fine on x86 and **not fine on a weakly-ordered
   dual-core Cortex-A7**. The design's answer is to keep MIDI and rendering on
   one core. `port/PORTING.md`.

6. **Preallocate reverb.** See RES-7.

7. **Map the audio ring non-cacheable.** The CPU writes the blocks and the DMA
   reads them. A clean-by-MVA would be marginally faster, but 1 536 bytes is not
   worth a class of bug that only appears under load. `port/DESIGN.md` § 2.3.

8. **`bootm`, not `go`.** See PWR-1.

9. **A 128 KB control ROM is truncated to 64 KB** by the library. This is
   expected, not a bug to fix. `Synth.cpp:622`.

10. **The lower half of the CM-32L PCM ROM *is* the MT-32 PCM ROM.** The two are
    aliased, and `makeROMImage` always prefers the full image. Do not be
    surprised by it, and do not "fix" it. `ROMInfo.h:93-96`.

11. **Peak memory during `open()` is double the PCM ROM**, because the file
    buffer and the expanded `pcmROMData` both exist. Allocate in that order.
    `port/PORTING.md` § 7.

12. **A large host cache flatters the real-time factor.** The PCM ROM is
    512 KiB–1 MiB and is addressed pseudo-randomly by whichever partials are
    sounding. It will not fit in the A7 cluster's L2. Any RTF measured on a
    laptop is optimistic and does not count. `port/PORTING.md` § 7.

13. **QEMU tells you nothing about speed.** Its TCG models neither the A7
    pipeline nor its caches. Structural correctness, yes; timing, never.

14. **Watch floating-point contraction.** GCC's default `-ffp-contract=fast`
    fuses a multiply-accumulate on armv7 that x86-64 does not fuse, inside
    `Analog.cpp`'s polyphase FIR — which every output sample passes through in
    `AnalogOutputMode_ACCURATE`. The audio difference is 1 LSB and inaudible;
    the cost is that the board and the bench stop being bit-comparable, which
    is the cheapest strong test available here. `-ffp-contract=off` on every ARM
    build. `port/PORTING.md` § 4.2.

15. **You may not be in the mode you think you are.** A bare-metal payload
    started by mainline U-Boot on sunxi arrives in **non-secure Hyp**, and the
    usual `mrs`/`orr #0x13`/`msr cpsr_c` idiom silently does nothing there —
    the ARM ARM makes a mode write that would leave Hyp ignored. The symptom is
    the nastiest kind: no fault, no message, exceptions vectoring through HVBAR
    instead of VBAR, and every "per-mode" stack aliased to the Hyp stack.
    U-Boot's own `armv7/start.S:93-99` guards the same instruction with
    `teq r1, #0x1a`. Detect Hyp and `eret` out; do not rely on a bootloader
    config, because then the config is load-bearing and nobody will remember.

16. **Do not commit ROMs, and do not commit the vendored upstreams.**
    `bench/vendor/`, `boot/vendor/` and `port/vendor/` are gitignored. Munt is
    cloned at a recorded commit, not submoduled, because the LGPL boundary is
    cleaner if upstream is fetched rather than embedded.

---

## 10. Acceptance

What it means for this device to be finished, in the order the answers can be
obtained.

| | Gate | Where it is answered | State |
|---|---|---|---|
| **1** | `mt32emu` renders faster than real time on one Cortex-A7 at 1.2 GHz — RTF ≤ 0.6 on the busiest passage | `bench/`, on real silicon with real ROMs | **`[open]` — blocks everything.** Now bounded, not blank: **16 859 armv7 instructions per frame** at 32 partials, so the gate is exactly the question "does an A7 sustain **0.749 IPC**?" (0.450 for real time at all; floor 0.225). Every known bias makes the truth worse. **A first IPC measurement needs no ROMs** — run `bench/rtf-synth` on any A7 |
| **2** | All conformance counters (§ 8) agree across every implementation of the seam | `port/host/test.sh` | host and bare-metal QEMU pass; **the T113 implementation does not exist yet** |
| **3** | The failure table (§ 5) behaves as specified, on the target | | `[silicon]` |
| **4** | Latency (§ 6) measured with a scope: MIDI start bit to audio out | | `[silicon]` |
| **5** | Zero underruns, `min_queued` ≥ 1, over a long real score | | `[silicon]` |
| **6** | It sounds right | A speaker, a DOS machine, and a person | **Nothing has ever been through a speaker** |

`docs/PLAN.md` § 0 sets the decision rule for gate 1: under ~0.6, proceed to a
board; 0.6 to 1.0, proceed single-synth only and revisit the sample rate; over
1.0, stop — either the part is wrong or the project is a Raspberry Pi again.

---

## 11. Licensing, as a requirement

| ID | | |
|---|---|---|
| **LIC-1** | `mt32emu` is **LGPL 2.1**. Anything statically linked against it inherits obligations. Keep the boundary clean and publish what the licence requires | |
| **LIC-2** | MT-32 and CM-32L ROMs are **Roland's**. Dump your own from hardware you own. They never enter this repository, and no build may embed them | |
| **LIC-3** | Upstream mt32-pi is **discontinued** — its author stepped back citing sustained harassment and code theft. Nuked-SC55 was archived for similar reasons. If this project ever ships, that history is the reason to be careful about attribution and about how contributors are treated | |
