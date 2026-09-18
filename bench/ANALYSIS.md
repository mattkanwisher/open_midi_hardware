# What mt32emu actually does per sample

Read of Munt `6e7c01fba7e1d50c8fa705834889fd0eac136075` (mt32emu 2.8.3).
All line numbers are into `bench/vendor/munt/mt32emu/src/`.

**Sections 1 to 7 contain no measurement of a running program.** They are a
source read plus static disassembly of the armv7-a build, written when nothing
in `bench/` had ever been executed. Every numeric claim in them is a size from
`size(1)`/`sizeof`, a static instruction census from `objdump`, or arithmetic.

**Sections 8 and 9 were added afterwards and they do measure the code running**:
exact armv7-a *dynamic* instruction counts under `qemu-arm`, an exact
symbol-level profile of the render path, an exact host x86-64 instruction count
from `callgrind`, and host wall clock. All of it on **fabricated ROMs**, because
there are none of Roland's here and never will be. One command regenerates every
one of those numbers:

```
./bench/estimate.sh
```

**There is still no real-time factor in this file, and § 9 exists to stop
anyone reading § 8 as though there were.**

Two claims made in §§ 3 and 4 from the source read have now been put to the
test. § 4's survived intact and is now a measurement rather than an inference;
§ 3's was right in direction and overstated in magnitude. Both are marked in
place.

## 1. The shape of the render call

`Synth::render()` → `RendererImpl<Sample>::doRender()` → `doRenderStreams()` →
`produceStreams()`. The whole of the per-block work is here:

```
Synth.cpp:2648  void RendererImpl<Sample>::produceStreams(...)
Synth.cpp:2656      mute the four accumulation buffers
Synth.cpp:2661      for (i = 0; i < synth.getPartialCount(); i++)
Synth.cpp:2663          partialManager.produceOutput(i, reverbDryL, reverbDryR, len)
Synth.cpp:2665          ... or into nonReverbL/R
Synth.cpp:2669      produceLA32Output()      (a bit re-shuffle over the block)
Synth.cpp:2673      reverbModel.process()
Synth.cpp:2686      convertSamplesToOutput()  (analog LPF / gain)
```

So the structure is: **outer loop over 32 partial slots, inner loop over the
block's samples.** Each partial walks the whole block accumulating into a
shared stereo pair.

`PartialManager::produceOutput` (`PartialManager.cpp:70`) is a one-line
forward to `Partial::produceOutput`, whose body is:

```
Partial.cpp:378  template <class Sample, class LA32PairImpl>
Partial.cpp:379  bool Partial::doProduceOutput(...) {
Partial.cpp:383      for (sampleNum = 0; sampleNum < length; sampleNum++) {
Partial.cpp:384          if (!generateNextSample(la32PairImpl)) break;
Partial.cpp:385          produceAndMixSample(leftBuf, rightBuf, la32PairImpl);
Partial.cpp:386      }
```

and the per-sample step (`Partial.cpp:336-352`) is:

```
Partial.cpp:336      tva->isPlaying() && la32Pair->isActive()     -- two predicate calls
Partial.cpp:340      la32Pair->generateNextSample(MASTER,
                         getAmpValue(),        <- TVA envelope, one LA32Ramp step
                         tvp->nextPitch(),     <- TVP, one timer step
                         getCutoffValue())     <- TVF, one LA32Ramp step
Partial.cpp:341-343  ... and the same again for the ring-modulating slave,
                     if this partial has one
```

That is the unit of work. **Per active partial, per sample**, with a second
full pass when the partial is half of a ring-modulated pair.

## 2. What dominates: integer table lookups and branches, not arithmetic

The default renderer is the integer one, set in the `Synth` constructor:

```
Synth.cpp:331    selectRendererType(RendererType_BIT16S);
```

`Enumerations.h` describes it as "the accurate wave generator model based on
logarithmic fixed-point computations and LUTs. Maximum emulation accuracy and
speed." That is exactly what the code is.

The LA32 works entirely in a **log domain**. Everything — the waveform, the
TVA amplitude, the TVF cutoff contribution — is added as a log value, and the
result is exponentiated once at the end:

```
LA32WaveGenerator.cpp:33-35   // "These two tables are accessed extremely often.
                              //  Keeping the direct pointers here significantly
                              //  improves performance in most cases."
                              static const Bit16u *exp9;
                              static const Bit16u *logsin9;

LA32WaveGenerator.cpp:41-47   interpolateExp()  -- two exp9[] loads + a lerp
LA32WaveGenerator.cpp:49-55   unlog()           -- interpolateExp() then a shift
LA32WaveGenerator.cpp:57-61   addLogSamples()   -- an add and a sign XOR
```

Both tables are `Bit16u[512]` (`Tables.h:50-51`) — 1 KiB each, 2 KiB total,
and they account for most of `Tables.cpp.o`'s 2,620 bytes of bss.

The per-sample generator is a phase-driven state machine over those tables:

```
LA32WaveGenerator.cpp:144-169  generateNextSquareWaveLogSample()
                               switch (phase) over 6 segments, one logsin9[] load,
                               two shifts, two adds, a clamp
LA32WaveGenerator.cpp:171-209  generateNextResonanceWaveLogSample()
                               the expensive one: up to four logsin9[] loads,
                               a multiply, several conditional adds, two clamps
LA32WaveGenerator.cpp:211-...  generateNextSawtoothCosineLogSample()  (sawtooth only)
LA32WaveGenerator.cpp:296-322  generateNextSample() ties them together
```

and the envelopes are a handful of compares and one add each:

```
LA32Ramp.cpp:110-144   nextValue()  -- one add or subtract, two compares,
                                       an interrupt countdown. 80 bytes of Thumb-2.
Partial.cpp:256-272    getAmpValue()     -- ampRamp.nextValue() + interrupt check
Partial.cpp:274-282    getCutoffValue()  -- cutoffModifierRamp.nextValue() + check
TVP.cpp:321-336        nextPitch()  -- usually just a decrement and a return;
                                       does real work only every ~N samples
```

**Answer to "float or integer": integer, overwhelmingly.** The default path
touches no floating point at all in the per-sample loop. Confirmed by
disassembly — `LA32WaveGenerator.cpp.o`, `Partial.cpp.o`, `LA32Ramp.cpp.o`,
`TVA.cpp.o`, `TVF.cpp.o`, `TVP.cpp.o` contain no libm calls and no VFP
double-precision instructions at all.

The character of the work is: **L1-resident table lookups, 16- and 32-bit
shifts and adds, and a lot of unpredictable branches.** That is a profile the
Cortex-A7 is reasonable but not brilliant at — it is a dual-issue in-order
core with a fairly modest branch predictor, so the `switch (phase)` in the
wave generator and the conditional ladders in `generateNextResonanceWaveLogSample`
are where the cycles will actually go.

## 3. The float renderer is disqualified on this platform

Do not use `--renderer float` on the T113. The float wave generator's
per-sample function calls **double-precision libm**:

```
LA32FloatWaveGenerator.cpp:71   generateNextSample(...)
LA32FloatWaveGenerator.cpp:88   EXP2F(ampVal / -1024.0f / 4096.0f)
LA32FloatWaveGenerator.cpp:89   EXP2F(pitch / 4096.0f - 16.0f) * SAMPLE_RATE
LA32FloatWaveGenerator.cpp:115  fmod(newPCMPosition, float(pcmWaveLength))
LA32FloatWaveGenerator.cpp:172  sin(FLOAT_PI * (cutoffVal - MIDDLE_CUTOFF_VALUE) / 32.0f)
LA32FloatWaveGenerator.cpp:179  cos(FLOAT_PI * relWavePos / cosineLen)
LA32FloatWaveGenerator.cpp:258  cos(FLOAT_2PI * wavePos / waveLen)
```

and `EXP2F` is not `exp2f` — off Apple platforms it is `exp()` on a double:

```
mmath.h:41-48   static inline float EXP2F(float x) {
                  #ifdef __APPLE__
                    return exp2f(x);
                  #else
                    return exp(FLOAT_LN_2 * x);     <- double exp()
                  #endif
                }
```

Disassembling the armv7-a build of that one function
(`LA32FloatWaveGenerator::generateNextSample`) finds **9 calls to `exp`, 3 to
`sin`, 3 to `cos` and 1 to `fmod`**, and 40 double-precision VFP instructions.
Branching means not all of them execute on every sample, but even one
double-precision libm call per partial per sample is fatal at 32 kHz × 32
partials on a 1.2 GHz A7. The integer generator, by contrast, has zero calls
out except to its own two helpers.

This is worth measuring once on the board — `rtf --renderer float` exists for
exactly that — but the expected answer is "several times slower", and it is
not a close call.

### 3.1 Measured — direction right, magnitude wrong

It is not several times. `bench/estimate.sh` runs both renderers on the same
workload, and the float one costs **1.6x the armv7-a instructions**, not 5x or
10x:

| workload | int | float | ratio |
|---|---:|---:|---:|
| armv7-a instructions per output frame, 8 partials, square, structure 0 | 4 605.7 | 7 313.5 | **1.588x** |
| armv7-a instructions per output frame, 8 partials, structure 2 (half the partials PCM, so `fmod` and the second `cos` are reached) | 3 832.9 | 6 169.6 | **1.610x** |
| host x86-64 ns per output frame, 32 partials, square (best of five) | 1 193.67 | 2 734.78 | 2.291x |
| host x86-64 ns per output frame, 32 partials, structure 2 | 992.81 | 2 152.68 | 2.168x |

(The two host rows are ±20 % in absolute terms — § 8.8 — but they come from the
same invocation minutes apart, so the *ratio* is much better behaved than either
figure.)

Why the source read over-predicted: the libm calls are real — `objdump -dr` on
`LA32FloatWaveGenerator.cpp.o` finds relocations for 10 `exp`, 3 `sin`, 3 `cos`
and 1 `fmod` — but they are branch-guarded, most do not execute on a given
sample, and glibc's `exp` on armhf with hardware double precision is a few
hundred instructions, not thousands.

**The recommendation does not change, for three reasons that survive the
correction:**

1. Instruction count *understates* the float path's cycle cost. Double-precision
   libm is a long dependency chain of VFP operations with little independent
   work to dual-issue against — the worst case for an in-order A7 — while the
   integer path is L1 table lookups the core can overlap. The cycle ratio will
   exceed 1.6 by an amount only silicon can say. Note the host, which is far
   better at hiding that latency, still shows 2.2-2.3x in *time*.
2. The float renderer doubles the renderer temp buffers (98 KB against 49 KB)
   and the preallocated reverb delay lines (about 206 KB against 103 KB) — see
   `README.md`'s footprint table. On a part whose DRAM is the constraint, that
   is the wrong direction twice over.
3. mt32emu's own default is the integer renderer (`Synth.cpp:331`), and upstream
   calls it both the more accurate and the faster model.

So: **use `--renderer int`** — but the sentence above this one was an
overstatement, and that is the argument for running things rather than reading
them.

## 4. Would NEON help? Mostly no, with one specific exception

The armv7-a build was inspected for actual NEON (q-register) instructions per
object. GCC 13.3 at `-O2 -mcpu=cortex-a7 -mfpu=neon-vfpv4` auto-vectorised:

| Object | NEON q-reg insns |
|---|---:|
| `Synth.cpp.o` | 531 |
| `sha1.cpp.o` | 51 |
| `Part.cpp.o` | 42 |
| `MidiStreamParser.cpp.o` | 36 |
| `Analog.cpp.o` | 15 |
| **`Partial.cpp.o`** | **0** |
| **`LA32WaveGenerator.cpp.o`** | **0** |
| **`LA32Ramp.cpp.o`** | **0** |
| **`TVA.cpp.o` / `TVF.cpp.o` / `TVP.cpp.o`** | **0** |
| **`BReverbModel.cpp.o`** | **0** |
| **all of `srchelper/` (the resampler)** | **0** |

And of `Synth.cpp.o`'s 531, the breakdown by symbol is:

| Symbol | NEON insns | in the render path? |
|---|---:|---|
| `Synth::dumpSysexBank` | 198 | no |
| `Synth::loadPCMROM` | 141 | no — one-time ROM decode |
| `SysexBuilder::appendSysex` | 97+11 | no |
| `RendererImpl<short>::produceLA32Output` | 30 | yes, but it is a bit-shuffle over the block |
| `RendererImpl<short>::produceStreams` | 30 | yes — these are the `muteSampleBuffer` memsets |
| `RendererImpl<short>::convertSamplesToOutput` | 5 | yes, trivially |

**So: essentially none of the synthesis work is vectorised today, and hand-
writing NEON for it would not help either.** The reasons are structural, not
a compiler failure:

- The LA32 is a **sequential state machine**. `wavePosition`, `squareWavePosition`,
  `resonanceSinePosition` and the `phase` enum each depend on the previous
  sample (`LA32WaveGenerator::advancePosition`, 472 bytes of Thumb-2). You
  cannot compute sample *n+1* without sample *n*, so there is no vectorising
  *across samples within one partial*.
- Vectorising *across partials* is the theoretically right axis — 32 partials
  are independent — but they are in different `phase` states with different
  branch outcomes, so every lane needs predication, and worse:
- The core operation is a **gather**: `logsin9[(squareWavePosition >> 9) & 511]`
  and `exp9[fract >> 3]` with per-lane indices. armv7 NEON has **no gather
  instruction**. Four lanes means four scalar loads plus `vmov`s, which costs
  more than doing it scalar.
- `BReverbModel`'s comb and allpass filters are **feedback** filters — serial
  by definition.

The one genuine hand-NEON opportunity is **`srchelper/` — the sample rate
converter**. `SincResampler` and `FIRResampler` got **zero** NEON from GCC,
and a FIR is precisely what NEON is good at: contiguous loads, `vmla.f32`,
no gather, no feedback. If the board runs its DAC at 48 kHz (which a PCM5102A
almost certainly will, against mt32emu's native 32 kHz), that FIR is in the
signal path on every output sample and is worth attacking. Measure it first
with `rtf --sample-rate 48000` against `rtf --sample-rate 32000`; if the delta
is small, leave it alone.

Note: `SincResampler.cpp.o` also contains 78 double-precision ops, but they are
all in `KaizerWindow::windowedSinc`, `bessel`, `estimateOrder` and
`createSincResampler` — i.e. **filter construction at open() time**, not the
per-sample path. That is fine.

`Analog.cpp` (the LPF emulation, `--analog coarse` by default) got 15 NEON
insns and is a plausible second target, but it is 5 KB of code total and runs
once per output sample rather than per partial-sample, so it is unlikely to be
where the time is.

### 4.1 Measured: the ablation, and the one place NEON does pay

The reading above is now an ablation. Same source, same `-O2`, same
`-mcpu=cortex-a7`; built once with `-mfpu=neon-vfpv4` and once with
`-mfpu=vfpv3-d16`, which takes NEON away from the compiler entirely.
`bench/cmake/toolchain-armv7a-neon.cmake` exposes that as `-DT113_FPU=`.

| armv7-a dynamic instructions | with NEON | without NEON | difference |
|---|---:|---:|---|
| render, 32 partials, per output frame | 16 859.6 | 16 854.7 | **-0.03 %** |
| render, 1 partial, per output frame | 996.6 | 1 021.5 | +2.50 % |
| one-time, before any audio: process start + ROM fabrication + `Synth::open()` | 7 797 084 | 18 430 800 | **+10 633 716** |
| of which, inside `Synth::loadPCMROM` alone (`qemu-arm -dfilter` over that symbol's address range) | 2 228 285 | 12 845 093 | +10 616 808 |

Three conclusions, and the middle one was not in the original reading:

1. **The § 4 claim stands, now as a measurement.** At 32 partials NEON is worth
   0.03 % of the render path — nothing. The reasons in § 4 are why: a sequential
   state machine over a table gather, and armv7 NEON has no gather.
2. **NEON's only measurable render-path contribution is the per-frame fixed
   overhead** — the four `muteSampleBuffer` calls in `produceStreams`. About 25
   instructions per frame: visible at 1 partial (2.5 %), lost in the noise at 32.
3. **NEON pays for itself once, at ROM load, and never again.** 99.8 % of the
   10.6 M-instruction difference in start-up is inside `Synth::loadPCMROM`,
   which expands the 8-bit log PCM ROM to 16-bit. Per ROM byte that is **4.25
   instructions vectorised against 24.50 scalar, a 5.76x speed-up on 524 288
   bytes.** Worth having on a device that decodes the ROM at every boot; worth
   nothing at all once audio is playing.

## 5. Can the two cores be used?

**Not through the public API, no.**

The partial loop is `Synth.cpp:2661`, inside `RendererImpl::produceStreams`,
which is private. `PartialManager.h` and `Partial.h` are **not public headers** —
the installed set is `mt32emu.h`, `globals.h`, `Enumerations.h`, `Types.h`,
`File.h`, `FileStream.h`, `MidiStreamParser.h`, `ROMInfo.h`,
`SampleRateConverter.h`, `Synth.h` (`mt32emu/CMakeLists.txt:197-212`). There is
no hook to split the partial loop from outside.

`Synth.h` is explicit that there is exactly one rendering thread:

```
Synth.h:379   "Calls from multiple threads must be synchronised, although, no
               synchronisation is required with the rendering thread."
Synth.h:397   "A thread that invokes these methods must be explicitly
               synchronised with the thread performing sample rendering or be
               the same."
```

What is available without patching:

1. **Two independent `Synth` instances on two cores.** Legitimate and useful if
   the plan's "second synth (FluidSynth, small font)" happens — but it does not
   make one MT-32 faster.
2. **Pipelining across blocks**: core 0 renders block *n* while core 1 does the
   I²S/DMA, MIDI parsing, display. This is worth doing regardless and it is
   probably most of what the second core is for. It does not reduce the
   worst-case render RTF, which is the gate.
3. **Split reverb off**, partially. `Synth::renderStreams()` hands out
   `nonReverb*` and `reverbDry*` separately (`Synth.h:573-577`), so with
   `setReverbEnabled(false)` you get the dry signal and could run a reverb on
   the other core. But `BReverbModel` is not a public class, so you would be
   writing your own reverb, which changes the sound. Probably not worth it.

What patching would buy, if the gate comes back marginal (RTF 0.6–1.0):

The partial loop at `Synth.cpp:2661` is **almost** embarrassingly parallel.
Each `Partial::produceOutput` reads its own state and accumulates into a shared
stereo pair. Give each core its own accumulation buffer and sum at the end and
the partials are independent. The catch is the integer mixer:

```
Partial.cpp:354-368  produceAndMixSample(IntSample*, IntSample*, ...)
                     leftOut = ((sample * leftPanValue) >> 13) + *leftBuf;
                     *leftBuf++ = Synth::clipSampleEx(leftOut);
```

— it **saturates at every accumulation step**, so the result depends on the
order partials are summed in. Splitting the loop across two cores changes the
output bit-for-bit whenever any intermediate sum clips. That may or may not be
audible; it is a deliberate emulation-accuracy decision, not a bug. The float
mixer (`Partial.cpp:370-375`) is a plain `+=` and has no such problem, but the
float renderer is disqualified for the reason in § 3.

So: a two-core split of the partial loop is **mechanically straightforward and
semantically lossy**. It is the obvious lever to pull if the single-core number
lands between 0.6 and 1.0, and it is a fork of mt32emu, which the LGPL is fine
with as long as the source is published.

## 6. The cycle budget, as arithmetic

This is not a prediction. It is what the target implies.

- Native rate 32,000 Hz; 32 partials maximum (`globals.h:94`, `globals.h:97`).
- A7 at 1.2 GHz → 37,500 cycles per output sample.
- At the plan's RTF ≤ 0.6 target → **22,500 cycles per output sample** for
  everything: 32 partials, reverb, the analog LPF, the mix, MIDI dispatch.
- Reserve, say, 20 % of that for reverb + analog + mix + MIDI, and 32 fully
  active partials leaves roughly **560 cycles per partial per sample**.

For scale, the code that has to be walked per partial per sample is, on armv7
Thumb-2: `Partial::produceOutput` 624 B (loop body inlined),
`LA32IntPartialPair::generateNextSample` 340 B,
`LA32WaveGenerator::advancePosition` 472 B, `nextOutSample` 300 B,
`unlogAndMixWGOutput` 240 B, `LA32Ramp::nextValue` 80 B × 2,
`TVP::nextPitch` 176 B — call it 2.3 KB of code, of which only a branch-
dependent fraction executes each iteration. At roughly 3 bytes per Thumb-2
instruction that is a few hundred instructions of headroom, on a core that
will manage well under 1 IPC on branchy integer code with L1 table lookups.

**That is neither obviously safe nor obviously doomed.** It is close enough
that the answer genuinely has to be measured, which is the point of the
harness. The two things that most plausibly decide it are (a) how many
partials the busiest real passage actually keeps active — 32 is the ceiling,
not the norm — and (b) whether the 48 kHz resampler is in the path.

## 7. Practical consequences for the port

1. **Use the integer renderer.** It is the default (`Synth.cpp:331`); do not
   change it.
2. **Pin `exp9[512]` and `logsin9[512]`** — 2 KiB, hit on essentially every
   sample of every partial, and the source itself flags them as the hot ones
   (`LA32WaveGenerator.cpp:33`). If anything goes in the T113's on-chip SRAM,
   it is these.
3. **Cut `MT32EMU_MAX_SAMPLES_PER_RUN`** (`globals.h:107`) from 4096 to the
   actual DMA block size. `RendererImpl` holds six inline buffers of that
   length (`Synth.cpp:212-214`); at 256 frames that is 3 KiB instead of 48 KiB.
4. **`rand()` is called in the render path.** `TVP.cpp:330`:
   `counter = NOMINAL_PROCESS_TIMER_PERIOD_SAMPLES + (rand() & 3);`. A bare-metal
   or RTOS port must supply a `rand()`, and it should be a cheap one — glibc's
   is not free and this is called per partial per sample (though it early-outs
   most of the time). **Now measured** (§ 8.6): `__random` plus `__random_r` is
   245.9 armv7-a instructions per output frame at 32 partials, **1.5 % of the
   render path**. Small, but it is free to fix and it has a name.
5. **`Synth::preallocateReverbMemory(true)`** before `open()` if the render
   loop must never allocate; the harness already does this. It costs ≈103 KB
   instead of ≈25 KB.
6. **`MidiStreamParser` is not needed** if the UART ISR feeds `playMsg()`
   directly — 4.4 KB of text you can drop. So is most of `ROMInfo.cpp`'s
   7.4 KB SHA-1 table once the ROM set is fixed at build time.

## 8. Measuring the cost without ROMs and without silicon

Sections 1 to 7 were a source read, and §§ 3.1 and 4.1 were added to them from
here. This section is not a source read at all: every number in it came
out of a command run in this container, and **`bench/estimate.sh` regenerates
all of them in one go**. Section 9 says what they are not. Read it.

### 8.1 The two things that were missing, and what replaced them

| Missing | Replaced by | What it costs in honesty |
|---|---|---|
| MT-32 ROM dumps | **fabricated ROMs** — mt32emu's own test technique (`src/test/FakeROMs.cpp`): hand `ArrayFile` the SHA-1 the library *expects* instead of hashing the fabrication (`File.h:60`), and fill in only the control-ROM fields `Synth::open()` actually reads. `emu/src/engine_mt32emu_fake_roms.cpp` already does this in workstream E | the emulator runs correctly on meaningless data. It cannot reproduce any real score's partial allocation, because the ROM's timbre data is exactly what decides that |
| Cortex-A7 silicon | **an exact armv7-a dynamic instruction count** under `qemu-arm`, converted to cycles by an *assumed* IPC | the instruction count is real; the cycle count is a hypothesis. QEMU's TCG has no pipeline, no branch predictor and no cache |

### 8.2 Bounding, not sampling

A fabricated control ROM is all zeroes, so it carries no timbres: a note-on
against it allocates **zero** partials, because `Part::cacheTimbre` reads
`timbre->common.partialMute` (`Part.cpp:261`) and it is 0. That is the whole
honesty problem in one line — *a synthetic ROM cannot tell you what a real score
costs, because the ROM is what decides how much work a note is.*

So `bench/rtf_synth.cpp` does not sample, it **bounds**. The Timbre Temp Area is
RAM, not ROM (`MemoryRegion.h:103`: `MT32EMU_MEMADDR(0x040000)`, eight entries
of `sizeof(TimbreParam)` = 246 bytes), reachable over an ordinary Roland DT1
sysex. The harness writes its own timbre there for each of the eight melodic
parts, with `partialMute` set to exactly the number of partials that part should
use, then strikes one note per part. Order matters and is not obvious:

1. **System Area** (`0x100000`) first — one MIDI channel per part and a reserve
   of four partials each, so allocation is deterministic. A fabricated ROM
   supplies zeroes for all of this, which would put every part on channel 1.
2. **Patch Temp** (`0x030000`) next — a write here calls `Part::resetTimbre()`,
   which copies the all-zero *ROM* timbre back over Timbre Temp
   (`Synth.cpp:1767`, `Part.cpp:214-218`). Writing it after the timbre would erase it.
3. **Timbre Temp** (`0x040000`) last.

The partial count is then **verified**, not assumed: the harness reads
`Synth::getPartialStates()` after the strike and after the run, prints
`ACTIVE AFTER STRIKE`, and shouts if it is not what was asked for. Every run
below reported `active=` equal to the requested count.

That turns one unrepresentative number into a **cost line**: a per-partial slope
and a fixed per-frame overhead. A real score's cost is that line evaluated at
the score's partial count — and the partial count is a knowable property of
MT-32 hardware (32 maximum, `globals.h:97`), unlike the ROM contents.

### 8.3 Counting armv7-a instructions

`qemu-arm` 8.2.2 as packaged here is built **without TCG plugin support**:

```
$ qemu-arm -plugin help
qemu: unknown option 'plugin'
```

so `libinsn.so` was not an option and that route was abandoned. What works is
`-one-insn-per-tb` with `-d exec`: one trace line per translation block, one
guest instruction per translation block, hence exactly one line per executed
guest instruction.

That was checked, not assumed, against a loop countable by eye:

```
$ arm-linux-gnueabihf-gcc -O2 -static -marm -o loop loop.c   # volatile long s; for(i<n) s+=i;
$ arm-linux-gnueabihf-objdump -d loop --disassemble=main
   10370:  ldr  r3, [sp, #4]
   10374:  add  r3, r3, r1
   10378:  add  r1, r1, #1
   1037c:  cmp  r1, r0
   10380:  str  r3, [sp, #4]
   10384:  bne  10370                       <- six instructions per iteration
$ { qemu-arm -one-insn-per-tb -d exec -D /dev/fd/3 ./loop 100000 >/dev/null 2>&1; } 3>&1 | wc -l
696335
$ { qemu-arm -one-insn-per-tb -d exec -D /dev/fd/3 ./loop 200000 >/dev/null 2>&1; } 3>&1 | wc -l
1296364
```

`(1296364 - 696335) / 100000 = 6.00029` against six counted by hand. The
residual 29 instructions are `printf` formatting one more digit in the larger
run; the loop itself is exact.

The count covers the whole process, so **every figure below is a difference
between two runs of the identical binary that differ only in how much audio they
render** — 0 frames against 512. Process start, ROM fabrication, `Synth::open()`,
the PCM decode and the probe block are byte-identical between the two and cancel
exactly. The counts are deterministic: repeated runs agreed to the instruction.
The binaries are statically linked, so the number is a property of the binary
and not of whichever `ld.so` qemu finds. `--count-mode` disables the harness's
own per-block `clock_gettime` and partial-state polling, which would otherwise
be a large share of a 512-frame measurement.

The same trace also carries the symbol each PC falls in, so it profiles as well
as counts, exactly and with no sampling error — § 8.6.

### 8.4 The cost line

Cross-built with `arm-linux-gnueabihf-g++ 13.3.0`, `-mcpu=cortex-a7
-mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb -O2`, statically linked. Integer
renderer, reverb on, analog `coarse`, 32 kHz out, 256-frame blocks, square-wave
partials in structure 0 (two independent synth partials per pair: no ring
modulation, no PCM). Notes held for the whole run. Every row's active partial
count was read back from the synth and matched what was asked for.

| sounding partials | armv7-a instructions per output frame | marginal, per partial |
|---:|---:|---:|
| 1 | 996.6 | — |
| 2 | 1 514.1 | 517.5 |
| 4 | 2 539.2 | 512.6 |
| 8 | 4 586.0 | 511.7 |
| 16 | 8 670.4 | 510.5 |
| 24 | 12 765.4 | 511.9 |
| 32 | **16 859.6** | 511.8 |

The marginal cost is flat to better than ±0.7 % across the whole range, so one
line describes it:

```
armv7-a instructions per output frame  =  489.9  +  511.5 x (sounding partials)
```

worst residual 4.9 instructions per frame, 0.03 % of the 32-partial figure.

**This is the useful output of the whole exercise.** A real score's cost is this
line evaluated at that score's partial count, and the partial count is knowable
— 32 is the hardware ceiling (`globals.h:97`) — in a way that ROM contents are
not. In other units: **one sounding partial costs 16.4 million armv7-a
instructions per second of audio**, and the fixed per-frame overhead costs 15.7
million per second whether one partial sounds or thirty-two.

Note what the `0` row would be and why it is not in the table: with nothing
sounding and the reverb settled, `RendererImpl::doRender` takes the
`!isActivated()` early-out (`Synth.cpp`, `doRender`) and mutes the buffer, which
costs essentially nothing. Idle is free; the 490 is the cost of *being* active.

For scale against § 6's arithmetic, written before anything ran: § 6 reasoned
from code size to "a few hundred instructions" per partial per sample, inside a
budget of **about 560 cycles** per partial per sample at RTF 0.6. The
measurement says **511.5 instructions**. So § 6's budget is met **if and only if
the A7 sustains about 0.91 IPC on this code** — the same statement as § 8.5's
break-even, arrived at from the other end. § 6 guessed the right order; it could
not close the gap, and neither can this, because the gap is IPC.

### 8.5 What that implies for a 1.2 GHz Cortex-A7 — and the one number nobody here can measure

One Cortex-A7 at 1.2 GHz rendering at 32 kHz has **37 500 cycles per output
frame**. `docs/PLAN.md` § 0's RTF ≤ 0.6 leaves **22 500**. The measurement says
16 859.6 *instructions* per frame at 32 partials. The conversion factor is IPC,
and **IPC is exactly what this container cannot supply**.

So it is not asserted. It is tabulated, and the break-even is stated instead:

| assumed IPC | cycles per frame | projected RTF | against the 0.6 target |
|---:|---:|---:|---|
| 1.20 | 14 050 | 0.375 | pass |
| 1.00 | 16 860 | 0.450 | pass |
| 0.90 | 18 733 | 0.500 | pass |
| 0.80 | 21 075 | 0.562 | pass, barely |
| **0.75** | **22 480** | **0.600** | **break-even** |
| 0.70 | 24 085 | 0.642 | real time, over target |
| 0.60 | 28 099 | 0.749 | real time, over target |
| 0.50 | 33 719 | 0.899 | real time, over target |
| 0.45 | 37 466 | 0.999 | break-even for real time |
| 0.40 | 42 149 | 1.124 | **fails** |

Three anchors, none of which is an assumption:

- **A hard floor.** The Cortex-A7 is at best *partial* dual-issue, so IPC ≤ 2 is
  an architectural ceiling that no tuning can beat. **RTF ≥ 0.225 at 32
  partials, whatever else is true.** There is no version of this where mt32emu
  is nearly free on this part.
- **A break-even.** The A7 must sustain **≈ 0.75 IPC** on this code to meet the
  plan's target, and **≈ 0.45** to keep up with real time at all.
- **An error bar on the break-even.** § 8.7 measures four timbre classes, and
  the per-partial cost ranges from 414.6 to 550.3 instructions. Carried through,
  the break-even IPC for the gate is **0.61 to 0.80** depending on what the
  score's timbres are made of, and **0.79** for the plain case if the DAC runs
  at 48 kHz rather than 32. That is the spread this measurement *can* bound;
  § 9.2 lists the ones it cannot.

Inverting the cost line gives the other decision-useful form — how much polyphony
fits inside RTF 0.6 at each assumed IPC:

| assumed IPC | sounding partials that fit in RTF 0.6 |
|---:|---:|
| 1.00 | 43 (i.e. all 32, with room) |
| 0.90 | 38.6 |
| 0.80 | 34.2 |
| 0.70 | 29.8 |
| 0.60 | 25.4 |
| 0.50 | 21.0 |
| 0.40 | 16.6 |

This is the row of the analysis a product decision can actually use. Note that
`rtf --partials N` and `rtf-synth --partials N` both exist because `docs/PLAN.md`
§ 0 already contemplates dropping below 32; the table says what that buys.

**None of this is a measurement of the T113.** § 9.

### 8.6 Where the instructions actually go

The same `-d exec` trace names the symbol each PC falls in, so it profiles as
well as it counts — exactly, with no sampling error at all. Subtracting a
zero-audio run from a 512-frame run leaves the render path alone. 32 partials,
square, structure 0, reverb on, analog coarse; 8 632 187 instructions for 512
frames, 16 859.7 per frame, which is the § 8.4 figure recovered independently:

| insn/frame | share | symbol |
|---:|---:|---|
| 4 005.4 | 23.8 % | `LA32WaveGenerator::advancePosition()` |
| 2 607.1 | 15.5 % | `Partial::produceOutput(short*, short*, unsigned)` |
| 2 446.2 | 14.5 % | `LA32IntPartialPair::unlogAndMixWGOutput()` |
| 1 714.4 | 10.2 % | `LA32WaveGenerator::generateNextResonanceWaveLogSample()` |
| 1 039.2 | 6.2 % | `LA32Ramp::nextValue()` |
| 886.1 | 5.3 % | `LA32WaveGenerator::generateNextSquareWaveLogSample()` |
| 736.0 | 4.4 % | `LA32IntPartialPair::generateNextSample()` |
| 416.0 | 2.5 % | `LA32IntPartialPair::nextOutSample()` |
| 407.9 | 2.4 % | `TVP::nextPitch()` |
| 330.6 | 2.0 % | `calcBasicAmp()` |
| 320.0 | 1.9 % | `LA32Ramp::checkInterrupt()` |
| 261.1 | 1.5 % | `BReverbModelImpl<short>::produceOutput<int>()` |
| 256.2 | 1.5 % | `TVA::handleInterrupt()` |
| 232.9 | 1.4 % | `TVA::recalcSustain()` |
| 155.2 | 0.9 % | `__random` |
| 141.7 | 0.8 % | `LA32Ramp::startRamp()` |
| 134.0 | 0.8 % | `CoarseLowPassFilter<int>::process(int)` |
| 111.4 | 0.7 % | `TVP::updatePitch()` |
| 96.0 | 0.6 % | `LA32IntPartialPair::isActive()` |
| 90.7 | 0.5 % | `__random_r` |

Four things worth saying about that list.

- **§ 2 predicted this from the source and got it right.** The top of the
  profile is the LA32's sequential position advance, its log-domain table
  lookups, and the per-partial mix. There is no floating point anywhere near
  the top.
- **`advancePosition()` alone is a quarter of everything**, and it is precisely
  the function § 4 identified as unvectorisable: `wavePosition`,
  `squareWavePosition` and `resonanceSinePosition` each depend on the previous
  sample. If anyone ever hand-optimises this code, that is the function.
- **`rand()` is in the profile, as § 7 item 4 warned it would be.** `__random` plus
  `__random_r` is 245.9 instructions per frame, **1.5 % of the total**, from one
  call in `TVP::nextPitch` (`TVP.cpp:330`). A bare-metal port must supply a
  `rand()` anyway; supplying a cheap one is worth about 1.4 % here. That is a
  small number with a name, which is better than a small number without one.
- **Reverb is 1.5 % and the analog LPF 0.8 %** of the render path at 32
  partials. Both are real and neither is where the problem is.

### 8.7 What moves the number, and what does not

All at 32 sounding partials, armv7-a instructions per output frame, against the
16 859.6 baseline. Every row's active partial count was verified.

| variant | insn/frame | vs baseline | what it means |
|---|---:|---:|---|
| baseline: square, structure 0, reverb on, analog coarse, 32 kHz, block 256 | 16 859.6 | — | |
| `--waveform saw` | 18 098.2 | **+7.35 %** | the sawtooth generator adds a cosine term per sample. The largest timbre-shape effect measured |
| `--structure 1` (ring-modulated pairs) | 16 673.3 | -1.10 % | ring modulation is cheaper than two independent partials, not dearer. `Partial::produceOutput` returns early for a ring-modulating slave (`Partial.cpp:324`), so there are 16 output loops rather than 32, each doing two `generateNextSample` calls — the same generator work, one fewer mix |
| `--structure 2` (half the partials PCM) | 13 756.5 | **-18.4 %** | a PCM partial is a table read; a synth partial is the log-domain state machine. **But see § 9.2: this is the row the synthetic PCM ROM flatters most** |
| `--reverb off` | 16 600.3 | -1.54 % | reverb costs 259 instructions per frame. Turning it off is not a lever |
| `--analog digital` | 16 729.6 | -0.77 % | the coarse LPF costs 130 instructions per frame. Also not a lever |
| `--block 64` | 16 892.7 | +0.20 % | per-block overhead is negligible in *instructions*. Block size is a latency and jitter decision, not a throughput one |
| `--block 512` | 16 874.1 | +0.09 % | likewise |
| `--retrigger 2` (a re-strike of all 8 parts every 2 ms) | 16 866.2 | +0.04 % | **note churn is free.** One full re-strike — 8 All Sound Off plus 8 note-ons, 32 partials torn down and reallocated — costs **481 instructions**, about 3 % of a single frame's work. At 2 ms this is 4 000 note-ons per second, far beyond any score |
| `--sample-rate 48000` | 11 879.1 per 48 kHz frame | **+5.69 % per second of audio** | 570.2 M instructions per second of audio against 539.5 M at 32 kHz. The internal resampler is **not** free, and it is not a catastrophe either |

Two of those deserve to change a plan.

**The 48 kHz question, which `README.md` flagged and nobody had measured.** A
PCM5102A will most likely run at 48 kHz, putting mt32emu's internal resampler in
the signal path. It costs **5.69 %** more instructions per second of audio,
which raises the break-even IPC from **0.749 to 0.792**. Worth knowing; not
worth redesigning the DAC clocking around. The § 4 suggestion of hand-writing
NEON for `srchelper/`'s FIR remains available and is now sized: it is at most
5.7 % of the problem.

**Per-partial cost is not one number, it is a range.** Subtracting the 489.9
fixed term and dividing by 32 gives the cost of one sounding partial per sample
for each timbre class measured:

| partial class | armv7-a instructions per partial per sample |
|---|---:|
| PCM (structure 2, half the partials) | 414.6 |
| ring-modulated (structure 1) | 505.7 |
| square wave (structure 0) | **511.6** |
| sawtooth (structure 0) | 550.3 |

**415 to 550, a ±14 % spread around the plain square-wave case.** That is the
timbre-mix error bar on § 8.5, and it is the one this measurement *can* bound.
Carried through, RTF at 32 partials is **(0.367 to 0.483) / IPC**, so the
break-even IPC for the gate is **0.61 to 0.80** depending on what the score's
timbres are made of, with 0.75 for the plain case.

### 8.8 Host x86-64: a cross-check, and an IPC anchor that did not work

Useless for the gate — different ISA, different microarchitecture — but it
bounds the algorithmic cost and catches gross errors in the ARM path.

**How noisy this is, measured:** host figures are the **best of five identical
runs**, and within one such set the worst/best ratio still reached 1.55x. Worse,
between two separate invocations of `estimate.sh` the best-of-five figure for
the same 32-partial workload came out **1 193.67** and **1 407.31** ns per
frame — an 18 % swing in the statistic that was supposed to be robust. An
ad-hoc run taken while the qemu sweep had all four cores busy came out **3.4x**
slow. **Treat every host number in this section as ±20 %, and never as
evidence.** The armv7-a instruction counts, by contrast, repeated to the
instruction across all three runs.

| workload | host ns per output frame | host RTF |
|---|---:|---:|
| 1 partial | 77.92 | 0.0025 |
| 8 partials | 326.10 | 0.0104 |
| 16 partials | 638.28 | 0.0204 |
| 24 partials | 900.03 | 0.0288 |
| 32 partials | 1 193.67 | 0.0382 |
| 32 partials, float renderer | 2 734.78 | 0.0875 |
| 32 partials, 48 kHz out | 851.87 | 0.0409 |

The host sweep is linear in partial count exactly as the ARM one is, which is
the cross-check: two independent toolchains, two ISAs, one shape.

`callgrind` then counts host instructions exactly, the same way qemu counts
guest ones, with the same 0-frames-against-512-frames differencing:

```
x86-64 instructions per output frame, 32 partials : 18 061.1
armv7-a instructions per output frame, same       : 16 859.6   (ratio 0.933)
```

Within 7 % for the same work on two unrelated ISAs is a good sign that neither
build is doing something silly.

**The IPC anchor did not work, and that is worth recording.** The intent was:
divide instructions by cycles on the host, get the IPC that this *kind* of code
achieves on a wide out-of-order core, and use it as an upper reference for the
A7's 0.75 break-even. The arithmetic gives **7.21 instructions per cycle at the
2.10 GHz that `/proc/cpuinfo` reports** — which is impossible; no x86-64 core
retires seven instructions per cycle on a dependent integer state machine. The
conclusion is not "the code is fast", it is **"the clock in `/proc/cpuinfo` is
not the clock this container runs at"**, which is ordinary under virtualisation
and which this environment gives no way to correct. `estimate.sh` now prints the
figure with that warning attached rather than quietly reporting a number that
looks like evidence.

What survives without needing a clock at all: the host retires this code at
**15.13 instructions per nanosecond** (18 061.1 instructions in 1 193.67 ns),
against **1.20** for a 1.2 GHz A7 at IPC 1.0 — so the host is about **12.6x
faster per unit time** on this workload, or 10.7x using the slower of the two
host runs. That ratio is plausible for a modern server core against an A7, and
it is the only host-derived quantity in this file worth anything — subject to
the same ±20 %.

### 8.9 The NEON ablation

In § 4.1, where the claim it tests lives. Summary: **-0.03 %** on the render
path at 32 partials, **+2.5 %** at 1 partial, and a **5.76x** speed-up on the
one-time PCM ROM decode, attributed by address-range filtering rather than by
inference.

### 8.10 What did not work, and one thing that was wrong before it was right

Recorded because the next person will otherwise try them again.

| Tried | Outcome |
|---|---|
| **QEMU TCG plugins** (`libinsn.so`, the obvious way to count instructions) | dead end. This build has no plugin support: `qemu-arm -plugin help` answers `qemu: unknown option 'plugin'`. Replaced by `-one-insn-per-tb -d exec`, § 8.3 |
| **`-d exec` without `-one-insn-per-tb`**, counting translation blocks and weighting them by their length from `-d in_asm` | rejected before implementing. It logs roughly ten times less, but block lengths change when a block is re-translated, and getting the weighting subtly wrong is undetectable. One line per instruction is slower and provably exact |
| **`perf`** | not installed, `perf_event_paranoid` is 2, and it would count x86 events under the emulator rather than guest instructions anyway |
| **valgrind against the ARM binary** | valgrind here is x86-64 only. Running `qemu-arm` under valgrind measures qemu, not the guest. valgrind is used on the *host* binary instead, § 8.8 |
| **Getting a real score's partial count** | not possible without ROMs. This is the gap § 8.2 turns into a parameter rather than a guess |
| **`Synth::setReverbEnabled(false)` right after `open()`** | **silently ignored.** Writing the System Area afterwards calls `refreshSystemReverbParameters()`, which reinstates the reverb model from `mt32ram` unless `reverbOverridden` is set (`Synth.cpp:1992`). The first run of this measurement therefore reported reverb-off and reverb-on as identical to 0.03 instructions per frame out of 16 860, which is exactly what a silently ignored switch looks like. The harness now disables reverb the way the hardware does — System Area reverb time and level both zero (`Synth.cpp:2003`) — and **asserts** `Synth::isReverbEnabled()` matches what was asked before it will measure anything |
| **`Synth::getPartialStates(Bit8u*)`** | the wrong overload, and it cost a long detour. It packs **four partials per byte, two bits each** (`Synth.h:604-606`); the `PartialState*` overload is one entry per partial. Reading the packed one as if it were the unpacked one made the harness believe a 32-partial workload had 8 active partials. The fix is the reason every run now prints `ACTIVE AFTER STRIKE` and refuses to be quoted silently |

The last two are the argument for the harness asserting its own configuration
rather than trusting it: both bugs produced *plausible* numbers.

## 9. READ THIS BEFORE QUOTING ANYTHING IN SECTION 8

### 9.1 Section 8 does not close the gate, and cannot

`docs/PLAN.md` § 0 asks one question: **does mt32emu render MT-32 audio faster
than real time on one Cortex-A7 at 1.2 GHz, on the busiest passage?** Section 8
does not answer it, and nothing further in this container will. Two independent
reasons, each sufficient on its own:

**(a) There is no Cortex-A7 here.** What § 8 measures is an *instruction count*
— a property of the binary and the workload, not of the machine. Turning it into
a time needs cycles per instruction, and cycles per instruction for branchy
integer code with table gathers, running out of DDR3 on an in-order,
partial-dual-issue core, is exactly what a functional emulator cannot tell you.
QEMU's TCG has no pipeline model, no branch predictor, no cache, no memory
latency. **Every RTF figure in § 8.5 is arithmetic over an assumed IPC, and the
assumption is doing most of the work.**

**(b) There are no ROMs here, so the workload is invented.** The partial count
is chosen by the harness, not by a score. A real score's cost is
`fixed + per-partial × (partials that passage sounds)`, and this repository
cannot learn the second factor: it is a property of the timbres in Roland's
control ROM and of what the composer wrote. § 8 gives the first two terms
honestly and leaves the third blank on purpose.

### 9.2 Everything the estimate is known to get wrong, and which way

| Effect | Direction | Size |
|---|---|---|
| **Cache misses against real DDR3 are not modelled at all.** QEMU counts an instruction identically whether its operand was in L1 or 200-plus cycles away in DRAM | **against us** | unbounded here; the single largest unknown |
| **The synthetic workload is unrealistically cache-friendly.** All 32 partials play one identical timbre out of one `PatchCache` per part. A real score has eight parts with eight different timbres and four differing partial parameter blocks each | **against us** | unknown, same mechanism as above |
| **The PCM ROM is zeroes and its wave map points every entry at one looping 2048-sample window.** Real PCM partials stride through a decoded 512 KiB (MT-32) or 1 MiB (CM-32L) array at pitch-dependent rates — that is the real DRAM traffic in this program, and this measurement touches 4 KiB of it | **against us**, badly, for any PCM-heavy timbre | unknown |
| Timbres here are plain: flat pitch envelope, no LFO, TVF envelope depth zero, TVA straight to sustain. Envelope *stage transitions* and their interrupt handling therefore fire rarely, though the ramps still run every sample | **against us** | small: `TVA::handleInterrupt` + `TVA::recalcSustain` + `LA32Ramp::startRamp` together are 3.7 % of the profile in § 8.6 even here, so the headroom for this to grow is a few per cent, not a factor |
| Only four timbre classes were measured (square, sawtooth, ring-modulated, half-PCM), not the real mix | bounded, both ways | 414.6 to 550.3 instructions per partial per sample, § 8.7. That is the ±14 % error bar on the slope, and it is carried into § 8.5's break-even as 0.61 to 0.80 |
| Notes are held rather than restruck. `--retrigger` brackets it and the answer is in § 8.7 | measured, neutral | tiny |
| The figures are a steady-state average over whole blocks. The gate is about the **worst** block | **against us** | not quantifiable without silicon: jitter is a cache and interrupt phenomenon, and § 8.7 shows block size barely moves the instruction count, which is precisely why instruction count cannot answer it |
| Instruction counts themselves | exact, deterministic, validated | ±0 |
| The host x86-64 wall clock in § 8.8 | noisy shared container | worst/best within a set of five identical runs reached 1.55x, and an ad-hoc run taken under load came out 3.4x slow. Only the minimum is quoted, and it is a cross-check, not evidence |

Note the pattern: **every identified bias runs the same way.** The real figure is
worse than the projection, never better. Treat § 8.5's RTF column as an
optimistic floor.

### 9.3 What would supersede this, in order of how much it settles

1. **`bench/rtf` on a real T113 — or any Cortex-A7 — with real ROM dumps and a
   real score.** This is the gate; nothing else is. `bench/README.md` has the
   command, including `taskset -c 0`, `chrt -f 80`, the performance governor and
   `--warmup 2.0`. One cheap board plus one dumped ROM set retires all of § 8.
2. **`bench/rtf-synth` on a real Cortex-A7. No ROMs needed.** This is the cheap
   half and it is worth doing on day one of a board, before any ROM is dumped:
   run the same partial sweep on silicon, divide the measured time by the
   instruction counts in § 8.4, and out falls **the measured IPC** — the one
   missing multiplier, obtained without a byte of anyone's copyright. Read the
   answer as follows:
   - above 0.80, the gate is very likely passed even for sawtooth-heavy material;
   - 0.61 to 0.80, it depends on the score's timbre mix, and only real ROMs and
     a real score settle it;
   - below 0.45, 32 simultaneous partials do not run in real time at all, and
     § 8.5's polyphony table says how far the partial limit would have to come
     down.

   **With one caveat that must travel with the number:** the IPC `rtf-synth`
   measures on silicon is itself an *upper bound* on the IPC a real score
   achieves, because the synthetic workload's working set is far smaller than a
   real one's (§ 9.2, rows one to three). It removes the largest unknown; it does
   not remove all of it.
3. A cycle-accurate A7 model, if one were reachable. It is not, here.
4. Real timbres without real ROMs — not possible, and not worth chasing. A board
   settles the question more cheaply than any amount of modelling.

### 9.4 One sentence

*From an exact armv7-a instruction count on a synthetic, verified 32-partial
workload, one Cortex-A7 core at 1.2 GHz must sustain about **0.75 instructions
per cycle** — **0.61 to 0.80** across the timbre classes measured, **0.79** if
the DAC runs at 48 kHz — to meet the plan's RTF ≤ 0.6, and about **0.45** to
keep up with real time at all; every effect this measurement cannot see, and
cache behaviour above all, makes that harder rather than easier.*
