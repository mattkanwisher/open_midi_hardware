# What the bare-metal image proves, and what it does not

Workstream E, 2026-09-18. Everything below was run in this container. Where a
number appears, the command that produced it is given, and where something was
*not* run, it says so.

---

## 1. The headline

**It boots.** `qemu-system-arm -M virt -cpu cortex-a7` starts the image at reset
with no operating system, and it brings up a console, a timer, an interrupt
controller, an audio sink and a MIDI source, runs `port/src`'s render loop
against a real periodic deadline for a second of audio, prints its counters and
exits with a status a script can assert on.

**It passes the same conformance suite as `port/host`.** All fifteen assertions
from `port/host/test.sh`, against the same MIDI vectors, plus nine more that
only a bare-metal run can make:

```
$ cd emu && make mt32emu && make test
ok   boots into SVC with MMU off, caches off, interrupts masked
ok   generic timer is present and running
ok   demo short msgs (8)             ok   bank sysex count (64)
ok   demo sysex (1)                  ok   bank short msgs (12)
ok   demo underruns (0)              ok   bank parsed cleanly
ok   demo wav written                ok   bank underruns (ring 3) (0)
ok   bad sysex emitted (0)           ok   3 orphan data bytes counted
ok   bad short msgs (2)              ok   oversize sysex refused
ok   bad underruns (0)               ok   unterminated sysex aborted
ok   realtime underruns (0)          ok   no spurious interrupts
ok   audio ring is in the non-cacheable window
ok   sink actually read every block it played
ok   deadline-driven sink consumed blocks
ok   heap high water is reported (PORTING.md 7)
ok   virtio-sound stream starts
ok   virtio periods complete
ok   virtio and timer sinks carried identical PCM (192000 bytes)
ok   QEMU wrote the device's audio to a host wav
ok   missing ROMs are named and the image still exits cleanly
ok   mt32emu opens a Synth bare metal
ok   mt32emu bank sysex (64)         ok   mt32emu bank short (12)
ok   mt32emu underruns (0)           ok   mt32emu render allocates nothing (0 B)
all tests passed
```

Thirty-three assertions, six consecutive clean runs. The suite runs under
`-icount shift=2`, which derives guest time from instruction count rather than
from the host's wall clock; without it a busy container reads as a missed audio
deadline. See § 8.7 for the one place where that is still not enough, and what
the suite does about it.

**That was the first half of this directory's work. §§ 10–15 are the second:
pushing the same image from "it boots and renders" to "we know where it
breaks".** The short version, each with the section that has the commands and
the tables:

- **`port/DESIGN.md` § 2.2's 128 / 3 survives, and the sweep does not argue
  with it** (§ 10). But the cliff is at real-time factor 1.00 and **ring depth
  does not move it by one block**: at RTF 1.07 a ring of 8 fails with the same
  143 dropouts as a ring of 2. Depth buys exactly (depth − 1) block periods of
  tolerance for *one bad block* — measured to the block, § 10.4 — and nothing
  at all of a sustained deficit. § 2.2's "quadruple buffering is the knob to
  turn if A's RTF comes back above ~0.7" is the wrong knob (§ 8.9).
- **Recovery from a dropout is one sink period, always, with no drift** (§ 10.4),
  because the tick rearms from an absolute compare value. It resyncs.
- **The parser is now checked against a second implementation of its own
  written contract**, not against numbers from a previous run, over seven
  streams including real-time bytes wedged inside a patch dump, running status
  straddling every read boundary, and a stream that just stops (§ 11). The
  16 kB bank dump arrives at the engine hashing byte-identically to the model.
- **A short message the engine refuses is dropped, not retried** (§ 11.5). The
  margin over DIN MIDI is a measured 23× with a 64-deep queue and 369× with
  mt32emu's 1024, so this is safe — but it is an undocumented limit whose
  failure mode is a permanently stuck note (§ 8.11).
- **The ring's second bound now has a number**: depth ≥ ceil(service interval /
  block period) + 1, with the consumer's service interval measured at
  10.2–14.3 ms for QEMU's audio backend (§ 12). Depth 3 is defensible on the
  T113 *only* because its DMAC's granularity is one block.
- **The dual-core hazard cannot be reproduced here, and § 13 proves why not**:
  the reordering mt32emu's queue is exposed to is store→store and load→load,
  which an x86-64 host does not perform and TCG does not add. A control litmus
  in the same image fires 1.6–7.8 % of the time, so the harness is not inert —
  it simply cannot see this class. The stronger result needs no second core at
  all: **`libmt32emu.a` cross-compiled for Cortex-A7 contains zero barrier and
  zero exclusive instructions** (§ 13.1).
- **The real engine now runs the whole suite, not one case of it**, with zero
  heap growth on all six streams, and the footprint numbers are re-measured
  (§ 14).

`make test` is now **eighty-six assertions**, and it stays green: three
consecutive clean runs at 86/86, plus clean runs of the `MMU=0` build and of
the build without `mt32emu` linked. One run takes about 1 min 50 s
(1:46 to 1:49 measured across four runs).

**The real `mt32emu` links, opens a `Synth`, and renders — bare metal.** Not
"links and fails cleanly on missing ROMs", which is what the brief asked for as
a success condition; the actual result is stronger. See § 6.

**Two independent cross-checks that the port layer is platform-blind.** The
1-second demo render produces **byte-identical PCM** on x86-64 (`port/host`) and
on bare-metal Cortex-A7 — 192 000 bytes, `cmp`-identical — and the same again
whether the blocks are consumed by the timer sink or by a virtio-sound device.

---

## 2. What this cannot prove, stated plainly

`boot/BRINGUP.md` § 6.4 already says it and it is worth repeating with the
emphasis the results above might otherwise erode:

- **No timing evidence comes out of this at all.** QEMU's TCG models neither
  the A7 pipeline nor its caches. The "worst render 2413 us" line the image
  prints is a property of this container's host CPU and of QEMU's translation
  cache. It is not a real-time factor and it must never be quoted as one.
  `docs/PLAN.md` § 0's gate still needs a board and real ROMs.
- **Nothing about the T113's peripherals.** There is no DRAM controller here,
  no I2S, no DMAC, no SMHC, no CCU, no pinmux. `-M virt` is a generic ARMv7-A
  machine that happens to put its RAM at 0x40000000, which is also the T113's
  DRAM base — that coincidence is what lets one linker script serve both, and
  it is the only thing the two machines have in common below the CPU.
- **Nothing about the audio being right.** The PCM the real engine produces
  here comes from a ROM image of zeroes. It proves the emulator runs; it says
  nothing about what an MT-32 sounds like.
- **The PL011 console does not transfer.** `src/console.c` is the only file in
  `emu/` that is throwaway. The T113 needs a 16550 driver at 0x02500000 + n·0x400
  (`boot/BRINGUP.md` § 6.2), which is about forty lines behind the same three
  functions.
- **The GIC base and interrupt numbering do not transfer**, though the
  programming model does: `-M virt` puts its GIC-400 at 0x08000000/0x08010000,
  the T113 at 0x03020000 (`boot/BRINGUP.md` § 4.3).
- **Nothing about weak memory ordering.** TCG translates guest loads and stores
  into host loads and stores and inherits the host's model. On the x86-64 host
  this ran on that model is TSO, which forbids the store→store and load→load
  reordering that ARMv7-A permits — so the two reorderings that actually
  threaten mt32emu's `volatile`-only MIDI queue are invisible here *by
  construction*. § 13 measures both sides of that: eight million message-passing
  publications with zero violations, and eight million store-buffering rounds
  with four hundred and eight thousand. Do not read the first number
  without the second.
- **The RTF numbers in § 10 are of a machine that does not exist.** `-icount
  shift=N` makes the guest a deterministic fictional machine whose speed halves
  with each step of N. That is what makes the *shape* of the margin
  measurable — where the cliff is relative to the block clock, how much a ring
  absorbs — and it is exactly why the *position* of the cliff on a T113 is
  still unknown and still needs `bench/`, real ROMs and a board.

---

## 3. The entry state, measured — and it answers a question BRINGUP left open

`boot/BRINGUP.md` § 4.4 asks for "a 10-line `mrs`/`mrc` dump in the first
payload" to settle secure-versus-non-secure on real silicon. That dump is
`src/start.S` step 2 plus `print_entry_state()` in `src/main.c`, it runs before
anything is reconfigured, and it is the *same code* that will run on the board.
Under QEMU today:

```
entry cpsr          0x400001d3  mode SVC  I=1 F=1 A=1 T=0
entry sctlr         0x00c50078  MMU=0 Dcache=0 Icache=0 align=0
entry actlr/vbar    0x00000000 / 0x00000000
midr                0x410fc075  part 0xc07 r0p5
id_pfr1             0x00010001  security=0 virt=0 genTimer=1
cntfrq at entry     62500000 Hz
loader r0/r1/r2     0x00000000 / 0x00000000 / 0x00000000
running at          0x40200020
```

Three things follow.

1. **QEMU's `-kernel` loader hands over in exactly the state `bootm` does** —
   SVC, IRQ and FIQ masked, MMU off, D-cache off, I-cache off. That is
   `cleanup_before_linux()`'s row in `boot/BRINGUP.md` § 4.1. So the reset path
   this image exercises is the one the board will use, and the recommendation
   there (wrap with `mkimage`, start with `bootm`, never `go`) is what
   `make uimage` produces.
2. **`ID_PFR1.Security = 0` under QEMU**: `-M virt` with `secure=off` has no
   Security Extensions at all, so the secure/non-secure question is moot here
   and the answer QEMU gives is not the answer the T113 will give. On the board
   expect `security=1`, and this dump is what will say whether U-Boot dropped
   us to non-secure. **Do not skip this step on hardware.**
3. **`CNTFRQ` is 62.5 MHz here and will be 24 MHz on the T113.** Nothing in
   `src/timer.c` has either number compiled in; both are derived from CNTFRQ at
   init, with a loud fallback to 24 MHz if a loader left it at zero.

One deviation from `port/include/mtp_time.h` worth recording: that header says
CNTVCT. `src/timer.c` uses **CNTPCT and the PL1 physical timer** (PPI 30)
instead. In a bare-metal PL1 payload with no hypervisor, CNTVOFF is zero and
the two counters are the same value; the physical timer is the one whose
comparator and PPI a PL1 payload can actually own. The header's argument for
the generic timer over a SoC peripheral is unaffected and was correct.

---

## 4. The MMU decision, and a measurement that argues both ways

**Decision: the image sets up a flat 1:1 mapping and turns the caches on, and
`make MMU=0` builds the other way.** Both configurations pass the whole test
suite.

The mapping is 1 MB sections (`src/mmu.c`): DRAM as Normal write-back
write-allocate shareable, everything below 0x40000000 as Device-XN, everything
above unmapped so a wild pointer faults into the abort handler rather than
wandering, and one 1 MB window — the `.dma` section from `link.ld` — retyped as
**Normal Non-cacheable**, which is where the audio ring and the virtqueues live.
That window exists because `port/DESIGN.md` § 2.3 requires it for the I2S DMA
ring on the board. Nothing here can prove the attribute is *sufficient*, because
TCG models no caches; what it proves is that the descriptor is built, the
linker puts the ring in it, and uncached `int16` stores at 48 kHz cost nothing
measurable.

The measurement, real engine, `--midi bank --seconds 2`:

| | worst render | underruns |
|---|---|---|
| `make mt32emu` (MMU + caches on) | 3262 µs | 0 |
| `make MMU=0 mt32emu` | 4807 µs | 0 |

**Read that with care.** The 47 % difference is QEMU's soft-MMU bookkeeping
changing, not caches: with the MMU off, TCG takes a different address-translation
path. It is *not* a preview of what caches-off costs on an A7, where it would be
far worse — an order of magnitude is the usual figure, because every load goes
to DDR3. The honest summary is that **this experiment cannot measure the thing
it looks like it measures**, and that is itself the reason the image says
`all timings are meaningless` on its own banner when built `MMU=0`.

What the MMU-on build *does* earn: it proves the payload can go from the `bootm`
handover state to a live MMU with caches on, from reset, without faulting — which
is a real bring-up step and one that is much easier to debug here than on a board.

---

## 5. Toolchain: what a bare-metal link actually costs

`arm-none-eabi-gcc` is **not** in this container, so there is no newlib and no
newlib libstdc++. The image is built with `arm-linux-gnueabihf-gcc 13.3` used as
a bare-metal compiler (`-nostdlib -nostartfiles`), with `libm.a` and `libgcc.a`
pulled in by hand and everything else written here. That is a deliberate choice
— it keeps the ABI identical to `bench/`'s cross build, which
`boot/BRINGUP.md` § 6.1 insists on — but it cost five specific things that are
worth writing down, because four of them will bite the T113 build too.

1. **glibc's `libm.a` declares `errno` as `__thread`.** Define a plain `int
   errno` and the link fails with `TLS reference in libm.a(math_err.o)
   mismatches non-TLS definition ... error adding symbols: bad value`, which is
   not a helpful message. The fix is a real TLS segment: `.tdata`/`.tbss` in
   `link.ld` with an explicit size reservation (`.tbss` does not advance the
   location counter), and `__aeabi_read_tp` in assembly returning
   `__tls_start - 8` — ARM variant 1 puts an 8-byte TCB in front of the block.
   Twenty lines in total, and `__aeabi_read_tp` must be assembly because AEABI
   requires it to preserve r1–r3.
2. **Ubuntu's GCC enables `_FORTIFY_SOURCE=2` at `-O2`.** Every `printf`,
   `sprintf`, `vsnprintf` and `vfprintf` call in `mt32emu` is rewritten to
   `__printf_chk`, `__sprintf_chk`, `__vsnprintf_chk`, `__vfprintf_chk` — glibc-only
   symbols, four undefined references with no hint as to the cause. Build with
   `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0`.
3. **`-ffreestanding` must not be passed to the C++ translation units.**
   libstdc++'s `<cstdlib>` is guarded by `_GLIBCXX_HOSTED`, and `-ffreestanding`
   makes it withhold `std::div` and `std::div_t`, which `mt32emu`'s
   `Display.cpp:227` uses. The error (`'div_t' is not a member of 'std'`) points
   nowhere near the flag. `emu/Makefile` keeps `-ffreestanding` on the C side
   only.
4. **`-fno-use-cxa-atexit` is a trap**, because it makes static destructors
   register with `atexit` instead of `__cxa_atexit`, and `atexit` is one more
   symbol to invent for no benefit. Drop it and implement `__cxa_atexit` as the
   one-line stub `port/PORTING.md` § 4.1 already specifies.
5. **`-fno-threadsafe-statics` really does remove `__cxa_guard_*`.** Confirmed:
   neither symbol appears in the link. `src/cxxrt.cpp` deliberately does not
   define them, so a build that forgets the flag fails loudly instead of
   silently acquiring a guard variable in `Tables::getInstance()`.

**Total retarget surface**, in `src/retarget.c`, `src/printf.c` and
`src/cxxrt.cpp`: `_sbrk`, `_write`, five `-ENOSYS` fd stubs, a bump allocator
(`malloc`/`calloc`/`realloc`/`free`), fifteen `string.h` functions, `rand`,
`abs`, `labs`, `div`, `errno`/`__errno_location`, `raise`, `abort`, `exit`,
`__assert_fail`, `__stack_chk_*`, `stdout`/`stderr`/`stdin` as data symbols, a
printf family, and six C++ runtime symbols. About 600 lines including comments.
`port/PORTING.md` § 8's closing claim — "twenty external symbols, better than a
bare-metal port has any right to expect" — survives contact with reality.

---

## 6. `mt32emu` bare metal: it links, it opens, it renders

`make mt32emu` builds Munt 2.8.3 out of source (nothing under `bench/` is
written to) with `port/PORTING.md` § 4.2's flags, and links it into the image.

### Static footprint

| | text | data | bss | raw `.bin` |
|---|---|---|---|---|
| Image without `mt32emu` | 97 824 | 436 | 13 798 284 | **98 260 B** |
| Image with `mt32emu` | 190 672 | 3 840 | 13 801 420 | **194 512 B** |
| `mt32emu`'s contribution | **+92 848** | **+3 404** | +3 136 | +96 252 B |

(`bss` is dominated by reservations, not code: the 12 MB heap, the 81 KB of
stacks, the 16 KB page table and the 1 MB uncached window are all `NOLOAD`
sections the loader never has to carry. The number that matters for an SD-booted
payload is the raw `.bin`: **190 KiB with the synthesiser in it.**)

### Runtime footprint, measured on ARM

```
$ make mt32emu && make run ARGS="--engine mt32emu-fakerom --midi bank --seconds 2"
heap after setup    1608728 B
heap high water     1608728 B of 12582912 B
heap grown by run   0 B
```

**1 608 728 bytes = 1 571 KiB** for MT-32, `RendererType_BIT16S`,
`AnalogOutputMode_ACCURATE`, 32 partials, reverb preallocated, 64 KB sysex ring.
`port/PORTING.md` § 7 predicted **1 525 KiB** for that configuration from an
x86-64 measurement, and expected ARM to come in slightly *under* it. It came in
3 % over, and the difference is this image's bump allocator: it rounds every
request to 8 bytes and never reuses anything, so the 589 824-byte PCM staging
buffer is not recovered. The prediction was good; the 4 MB arena § 7 recommends
is right with room to spare.

**`heap grown by run` is 0 bytes**, across a run that delivered 64 sysex patch
dumps into the synth. That is `port/PORTING.md` § 3's central measured claim —
zero heap operations on the render path, given `preallocateReverbMemory(true)`
and `configureMIDIEventQueueSysexStorage()` — independently confirmed on ARM,
bare metal, with a different allocator underneath. It is now a `make test`
assertion, so it cannot silently regress.

### How it is opened without ROMs

`port/host/engine_mt32emu.cpp` loads ROMs through `mtp_storage` and lets
`makeROMImage()` identify them by size and SHA-1. With no ROMs that path can
only ever reach "no such file" — which `emu/` does test, and which behaves as
`port/DESIGN.md` § 4.3 specifies:

```
[E] control ROM 'roms/MT32_CONTROL.ROM': no such file
[E] engine open: I/O error
--- run ---
engine              mt32emu (FAILED TO OPEN)
--- end ---
```

The console stays up, nothing hangs, the image exits cleanly. But that leaves
the C++ runtime, the arena, the static constructors and the LA32 render path
untested, so `emu/` also carries `src/engine_mt32emu_fake_roms.cpp`: Munt's own
`src/test/FakeROMs.cpp` fixture, reduced and rewritten against the public API,
which fabricates ROM images the library accepts by handing `ArrayFile` the
*expected* SHA-1 (`File.h:60`) instead of hashing the fabrication. It needs the
library compiled with `-DMT32EMU_WITH_TESTING` for `Test::getControlROMMap`
(`Synth.cpp:2912-2922`), which `emu/Makefile` does.

With it, the real emulator opens and renders:

```
[I] fake ROMs: 'ctrl_mt32_1_07' (65536 B) + 'pcm_mt32' (524288 B)
[I] ROMs: MT-32 Control v1.07 + MT-32 PCM ROM
[I] engine: mt32emu 2.8.3, 32 partials, 48000 Hz out
blocks committed    376  (1.002 s of audio)
sysex messages      64
underruns           0
```

The audio is meaningless — the PCM ROM is zeroes. Everything else is real: the
C++ runtime, `operator new` against the bump arena, `.init_array`,
`Tables::getInstance()`, 1.5 MiB of allocations, `Synth::playSysex` on 64
16 kB-total patch dumps, and `Synth::render(Bit16s*, 128)` 376 times without
missing a deadline.

---

## 7. The virtio-sound sink, and one transferable result

The default sink is a timer interrupt pretending to be a DMA engine. The second
is a real device model — `src/virtio.c` (virtio-mmio transport and split
virtqueues, 297 lines) and `src/virtio_snd.c` (a playback stream). It
works:

```
$ make virtio
[I] virtio-snd: transport slot 31 at 0xa003e00, irq 79
[I] virtio-snd: 0 jacks, 2 streams, 0 chmaps
[I] virtio-snd: stream 0 is output, 1..2 channels
[I] virtio-snd: stream 0 started, period 512 B, buffer 3072 B
```

and QEMU's `-audiodev wav` writes the device's output to a host file. (That run
is `make virtio`, ring depth 6: 370 periods completed, `max 6 completed per irq`
— the batching § 7's table is about — and 2 device-idle events.)

**Practical note that cost an hour:** QEMU's virtio-mmio transport defaults to
the **legacy** interface and reports Version 1, whose queue setup is QueuePFN
plus a guest page-size register rather than the split
descriptor/driver/device addresses a modern driver writes. Pass
`-global virtio-mmio.force-legacy=false`. The driver detects the legacy case
and prints that flag rather than failing obscurely.

**The transferable result.** The number of device-idle events — the sink running
out of data, the virtio analogue of an I2S ring going dry — against ring depth,
demo stream, one second, 128-frame blocks:

| ring depth | audio in flight | device-idle events |
|---|---|---|
| 3 (`port/DESIGN.md` § 2.2's choice) | 8.0 ms | 124 |
| 4 | 10.7 ms | 83 |
| 6 | 16.0 ms | 2 |
| 8 | 21.3 ms | 1 |

`min ring occupancy` stayed at 1 in every one of those runs: **the renderer was
never behind.** The starvation is entirely on the consumer side, and its cause
is visible in the image's own `max N completed per irq` counter — QEMU's audio
subsystem services its backends on a ~10 ms timer and takes several 2.67 ms
periods in one go, so a ring holding 8 ms of audio cannot cover one service
interval.

(§ 12 turns this into a stated rule with a measured number, and checks
`port/DESIGN.md`'s depth 3 against it. The rest of this section is the original
observation and is left as it was written.)

That is a constraint `port/DESIGN.md` § 2.2 does not state. Its ring depth is
derived from the *renderer's* slack — how long a bad `render()` can overrun
before the DMA runs dry — and that derivation is right as far as it goes. The
second, independent constraint is the **consumer's service granularity**: the
ring must hold at least one service interval of the thing draining it, or it
starves no matter how far ahead the renderer is. On the T113 this constraint is
satisfied trivially, because the DMAC raises a completion interrupt per
descriptor and its granularity is one block — which is exactly why depth 3 is
defensible there and is *not* defensible against QEMU's audio backend. It is
worth a sentence in DESIGN.md anyway, because the next consumer might not be a
DMAC (an RTOS driver with a work queue, say, or a USB isochronous endpoint).

---

## 8. Things in `port/` and `boot/` that want changing

Nothing outside `emu/` was modified — with one caveat worth stating: running
`port/host`'s own suite for the PCM comparison in § 1 regenerated
`port/host/build/`, which is build output and is already in `.gitignore`. No
source file outside `emu/` was touched.

These are the changes this work says are needed.

### 8.1 `port/include/mtp_audio.h`: the start-of-stream underrun

The header says "Opens the sink and starts the DMA. Until the first block is
committed the ring holds silence, so opening early is safe." An implementation
that takes that literally — start the consumer in `open()` — counts an underrun
for every consumer period between `open()` and the first `commit()`, because the
ring genuinely is empty. That is an artefact of the start-up order, not a
rendering failure, and it makes the counter that the whole design hangs on
non-zero on a perfectly healthy boot. (`emu/` hit exactly this: 1 underrun on a
clean run.)

The fix here was to start the consumer clock on the **first commit**, which is
also what a real driver should do: prime the ring, then start the DMA. **Proposed
change:** say so in `mtp_audio.h` — "the implementation must not begin consuming,
or must not count underruns, before the first `commit()`" — so that the T113
driver does not have to rediscover it. One sentence, no API change.

### 8.2 `port/host/engine_fake.c` is in the wrong directory

It is not host-specific in any way: it has no POSIX dependency, it allocates
once at open, and `emu/` compiles it unmodified for bare metal. It is the
portable reference engine that lets *any* platform implementation be exercised
before a synthesiser exists, which is precisely the role `docs/PLAN.md` § 0.5
puts above the seam. **Proposed change:** move it to `port/src/engine_fake.c`.
`emu/Makefile` compiles it from `port/host/` today and will need one path edited.

### 8.3 `port/PORTING.md` § 4.1's retarget table is missing entries

Measured from the undefined-symbol dump of an actual bare-metal link against
Munt 2.8.3. Add:

| Symbol | Where from | Note |
|---|---|---|
| `abs` | `TVP.cpp:88` | the table lists `div` but not this |
| `stdout` | `Synth.cpp:391-398`, as a **data** symbol | needed even though the default `ReportHandler` is overridden, exactly as § 4.7 predicts |
| `vfprintf` | same | § 4.7 mentions `vprintf`; the call is `vfprintf(stdout, …)` |
| `sprintf` | `Part.cpp` constructor | § 4.7 mentions `MidiStreamParser.cpp` only |
| `errno` as **TLS** | glibc `libm.a` | with `__aeabi_read_tp`; see § 5.1 above. Not needed with newlib |
| `__printf_chk` family | `_FORTIFY_SOURCE` | see § 5.2 above — or disable it, which is the right answer |

And a note on § 4.2's flags: **do not add `-ffreestanding`** to the C++ flags
(§ 5.3 above), and drop `-fno-use-cxa-atexit` if it is ever considered.

### 8.4 `port/PORTING.md` § 4.2 asks for a patch to `bench/`

It says to remove `src/FileStream.cpp` from `libmt32emu_CPP_SOURCES`
(`CMakeLists.txt:154`). Upstream offers no option for that, so following the
instruction means editing the vendored tree — which conflicts with every
workstream's rule that `bench/vendor` is read-only. `emu/` solves it by owning
the file list instead (`tools/prepare-mt32emu.sh` stages the headers, the
Makefile names the nineteen translation units). **Proposed change:** say that in
§ 4.5, because "patch upstream" and "never modify the vendored clone" cannot
both be the policy. Worth noting that including `FileStream.h` is harmless — it
is only compiling `FileStream.cpp` that drags iostream in.

### 8.5 `port/src/mtp_render.c`: a stalled sink ends the run

`mtp_render_run()` breaks out of its loop when `mtp_audio_wait()` times out,
logging "audio sink stalled". On a host harness that is right. On the device it
means a transient sink problem silently ends audio for ever, with one line on a
console nobody is watching — against the spirit of `port/DESIGN.md` § 4.3's "never
hang, always be debuggable". It did not fire in any `emu/` run; flagging it as a
design question, not a bug. `worst_block_us` is also collected and never printed
by any harness.

### 8.6 `port/host`'s underrun assertions are structurally vacuous

`port/host/host_audio_wav.c:107`: outside `--realtime`, `advance()` sets
`g_queued = 0` unconditionally, so the ring can never be found empty and
`mtp_audio_underruns()` can never be non-zero. Three of `port/host/test.sh`'s
four "underruns (0)" assertions — cases 1, 2 and 3 — are therefore checking
nothing. Only case 4, with `--realtime`, tests anything, and even there the
"clock" is `clock_gettime` polled by the same thread that renders, so a late
render cannot be observed by an independent observer.

That is not a criticism of the harness: it was built to exercise structure
before silicon existed, and it does. But it means **`emu/` is the first place
the underrun counter is a measurement**, because here the consumer is an
interrupt from a free-running timer that fires whether or not the renderer is
ready. `emu/test.sh` says so in its own comments so that nobody reads the two
suites' matching "underruns (0)" lines as equally strong.

**Proposed change:** either make the host's non-realtime sink advance the
virtual cursor at the block rate against `mtp_time_us()` (a few lines, and it
would make the assertions mean something), or say in `test.sh` that those three
lines are structural checks rather than timing ones.

### 8.7 A real deadline test needs a way to tell jitter from failure

Running `emu/test.sh` repeatedly on a loaded container fails perhaps one run in
five, on whichever case has the least audio in flight. The cause is always the
same: the host descheduled QEMU for longer than the ring covers, the guest's
generic timer interrupt arrived late, and the sink correctly reported that it
had nothing to play. The render loop did nothing wrong.

Two things were needed to make the suite deterministic, and both are worth
carrying into whatever tests the T113 build one day has.

1. **`-icount shift=2`**, so that guest time is derived from instruction count.
   This removes most of the jitter and makes the numbers repeatable. It also
   makes the reported render times obviously fictional, which is a feature: at
   shift 0, 1 and 2 the real engine's worst render came out as 838, 1676 and
   3352 µs — exactly doubling — which is as clear a demonstration as one could
   want that these are instruction counts and not microseconds.
2. **A test that distinguishes the two failures.** `expect_no_underruns()` in
   `emu/test.sh` fails on an underrun only when the sink interrupt was *on
   time*; if the interrupt itself was later than `(ring − 1) × block_period`,
   it says so and passes, because the deadline was moved rather than missed.
   The image prints the worst tick-to-service latency for exactly this purpose.

The bank case is also split in two as a result: the parser contract is asserted
at `port/host`'s own `--block 64 --ring 2` (2.67 ms of audio in flight), and the
underrun contract at the 128-frame, ring-3 numbers `port/DESIGN.md` § 2.2
actually specifies (5.3 ms). Asserting a 2.67 ms deadline inside a container is
asserting how busy the container is.

### 8.8 `boot/BRINGUP.md` § 4.4's open question is now half-answered

The § 3 dump above is the "10-line test" that section asks for, written and
working. It answers the question for QEMU (no Security Extensions at all) and
is ready to answer it on silicon unchanged. § 4.4 can be updated to point at
`emu/src/start.S` and `emu/src/main.c:print_entry_state()` rather than
describing a test to be written.

### 8.9 `port/DESIGN.md` § 2.2's fallback names the wrong knob

> "Quadruple buffering (depth 4) is the knob to turn if A's RTF comes back
> above ~0.7."

Measured (§ 10.3): at RTF 1.07 depth 4 fails with 143 dropouts, which is the
same 143 that depth 2 produces and the same 143 that depth **8** produces. Ring
depth is a shock absorber for one bad block — it buys exactly (depth − 1) block
periods of overrun, measured to the block in § 10.4 — and it is not a
throughput reserve. § 2.4 of the same document already says this correctly; it
is § 2.2's sentence that invites the wrong reading.

**Proposed change:** replace that sentence with something like

> Ring depth buys transient tolerance, not throughput: a ring of depth *d*
> absorbs exactly (*d* − 1) block periods of overrun in a single block and
> nothing at all of a sustained deficit. Size it from the measured *peak*
> render time, not from the mean. If A's RTF approaches 1.0, no value of block
> size or ring depth helps and the fallback is § 2.1's `COARSE` at 32 kHz, or
> fewer partials.

§ 10.5 has the table this replaces it with.

### 8.10 `port/DESIGN.md` § 2.2 needs the consumer's service granularity

§ 7 found the constraint; § 12 measures it. **Proposed change:** add to § 2.2,
after the ring-depth row:

> Ring depth has a second, independent lower bound that has nothing to do with
> the renderer:
>
>     depth ≥ ceil(consumer_service_interval / block_period) + 1
>
> where the service interval is the longest gap between two chances for the
> consumer to take a block. Take the larger of this and the renderer-slack
> bound above. For the T113's DMAC the interval is one block, because it
> raises a completion interrupt per descriptor, so this bound gives 2 and the
> renderer bound dominates — **which is the only reason depth 3 is defensible
> here.** A consumer with coarser granularity breaks it: measured against
> QEMU's virtio-sound backend, whose service interval is 10.2–14.3 ms, depth 3
> produces 125 dry periods per second of audio while the renderer's minimum
> ring occupancy never drops below 1 (`emu/FINDINGS.md` § 12).

This matters beyond QEMU. If the I²S path is ever driven by half/full-buffer
interrupts on a larger buffer instead of per-descriptor completions, or if a
control plane is ever put between the render loop and the DMA, the granularity
changes and depth 3 stops being enough.

### 8.11 `port/src/mtp_render.c` drops short messages under back-pressure

`drain_midi()` reads the MIDI source without bound and hands every message to
the engine. A sysex the engine refuses is stashed and retried; **a short
message the engine refuses is counted in `stats.engine_backpressure` and
dropped.** A dropped All Notes Off is a note that hangs until the box is
power-cycled.

The margin is large — measured 23× over DIN MIDI's maximum message rate with
`engine_fake.c`'s 64-deep queue, 369× with mt32emu's default 1024 (§ 11.5) —
so this is not a bug on a DIN MIDI box. It is an undocumented limit with a
silent, permanent failure at the end of it, and the unpaced rows of § 11.5's
table show that a source which can deliver in bulk loses 640 of 1664 messages
even with mt32emu's own queue depth.

**Proposed change:** either make the asymmetry deliberate and say so in a
comment (a dropped note-off is less bad than a reordered patch dump), or make
`drain_midi()` stop reading when the engine refuses a short message, exactly
as it already does when the engine refuses a sysex. Either way,
`port/DESIGN.md` § 3 should state the capacity: *engine queue depth per block
period*, and what it is with mt32emu's defaults.

Smaller, same area: `mtp_render.c` passes `MTP_MSG_REALTIME` to
`engine->short_msg()` but does not count it in `stats.short_msgs`, so every
Active Sensing and MIDI Clock byte takes an engine queue slot that no counter
above the seam sees. Harmless at 3.3 and 48 events/s respectively; it should
still not be invisible.

### 8.12 `port/PORTING.md` § 5 is confirmed at the instruction level

§ 5 infers the hazard from mt32emu's source. It can be stated more strongly
and more cheaply, from the cross-compiled object:
`MidiEventQueue::pushShortMessage` emits three payload stores followed by the
index store with nothing between them, and **`libmt32emu.a` built for
`-mcpu=cortex-a7` contains zero `dmb`, `dsb`, `isb`, `ldrex` and `strex`
instructions in total** (§ 13.1, with the commands).

**Proposed change:** quote the disassembly in § 5 instead of the source. It
removes the "but maybe the compiler…" question entirely, and it is two
commands anyone can re-run.

§ 13 also records that this cannot be reproduced under QEMU, why not, and what
would settle it — the cheapest being to compile `emu/src/smp.c`'s two litmus
tests as a Linux userspace program on any real ARM SMP board, which needs no
T113 and no ROMs.

### 8.13 `port/include/mtp_audio.h` should say what "underrun" means

§ 8.1 asks for one sentence about not counting underruns before the first
commit. A second sentence is now worth having, from § 10.4: an implementation
should make the sink's deadline sequence **absolute**, not a countdown reloaded
in the handler. `emu/src/timer.c` rearms from CNTP_CVAL, and that is why
recovery from a dropout is exactly one period with no drift. A driver that
reloads a countdown turns every late service into a permanently slower audio
clock — the counter still reads 0 underruns and the pitch is wrong, which is
the worst possible combination.

---

## 9. What was not run

- **`make uimage`.** `mkimage` is not installed in this container (it is in
  `u-boot-tools`) and no packages were installed. The target is written and the
  addresses match `boot/BRINGUP.md` § 4.2, but it has never been executed.
- **Anything with real ROMs**, for the obvious reason.
- **`-audiodev alsa`/`oss`.** `make virtio` uses the `wav` backend; the others
  are listed by this QEMU but there is no sound card in the container.
- ~~**A second core.**~~ **Now run.** `-M virt -smp 2` boots one through PSCI
  CPU_ON over HVC, `src/start.S` gained a `_secondary_start` entry, and § 13
  has the litmus results. The original parking loop still exists and still has
  never executed, because PSCI starts the secondary at our entry point rather
  than at reset.
- **`-icount shift=N,sleep=off`**, which would have made guest time fully
  independent of the host, **hangs**: with the render loop in `WFI` no
  instructions retire, so the virtual clock stops and the timer deadline never
  arrives. Killed after two minutes with no output. `sleep=on` (the default)
  works and is what `make test` uses. **Re-confirmed 2026-09-18** while trying
  to remove the residual jitter from § 10.1's surface: killed again after 200 s
  with no output. This is the single biggest limitation on the sweep — it is
  why non-monotonic dropout counts appear in a table whose `min_queued` column
  says the renderer was never behind.
- **Anything with a real ARM SMP machine.** § 13's negative result is bounded
  by the host's memory model, and the cheapest way to settle it is to build
  `src/smp.c`'s two litmus tests as a Linux userspace program with two pinned
  threads on any Cortex-A7 or A53 board. Not done: there is no such board here,
  and no `herd7`/`litmus7` in this container.
- **Latency, at all.** § 10.6's latency columns are `port/DESIGN.md` § 2.5's
  model evaluated at each sweep point. Nothing in this image measures latency,
  because there is no DAC and the "audio" is a checksum.
- **Any listening.** Still true, and now doubly so: `--engine probe` renders a
  deterministic ramp, `--sink none` renders to nowhere, and `--rate` in cliff
  mode deliberately produces audio that would play at the wrong speed.

---

## 10. The two numbers that should move on measurement

`port/DESIGN.md` § 2.2 names block size and ring depth as "the only two numbers
in this design that should move in response to a measurement", and says the
harness takes both as arguments so the experiment is one command. This is that
command:

```sh
$ cd emu && make mt32emu && tools/sweep.sh            # the (block, ring) surface
$ tools/sweep.sh --rtf                                # by fictional CPU speed
$ tools/sweep.sh --cliff --shift 4                    # fine steps around RTF 1
```

**Before any number below.** QEMU's TCG models neither the A7 pipeline nor its
caches. Every microsecond here is a retired-instruction count divided by a
constant that `-icount shift=N` chooses. What the sweep measures is the
*shape* of the margin — how far the renderer can fall behind the block clock,
at each (block, depth), before the ring runs dry — not how fast a T113 is. The
shape is a property of the ring discipline and transfers; the scale is
fictional and does not.

That is also what makes `-icount` the right instrument here rather than a
nuisance: it turns "how fast is this machine" into a dial. `shift` doubles the
fictional machine's cycle time per step, and § 8.7 already showed the render
time doubling exactly with it, so **the shift is a real-time-factor knob** —
which is precisely the question `docs/PLAN.md` § 0's gate will answer on
silicon and cannot answer here.

### 10.1 The surface, at one machine speed

`tools/sweep.sh --seconds 6 --shift 2`, real engine (`mt32emu-fakerom`), the
16 kB bank dump paced at 31250 baud, 6 s of audio per point, 25 points,
2 min 31 s wall:

```
 block  ring period_us flight_ms  under  bursts worstrun  min_q peak/period   late_us cause
    32     2       666      1.33      0       0       0      1       0.23       916  -
    32     3       666      2.00     40      12       9      1       0.22      7403  sink late (host)
    32     4       666      2.66     31       5      20      1       0.22     15485  sink late (host)
    32     6       666      4.00     46       1      46      1       0.22     33990  sink late (host)
    32     8       666      5.33      8       2       4      1       0.23      7432  sink late (host)
    64     2      1333      2.67     21      13       3      1       0.19      5903  sink late (host)
    64     3      1333      4.00      8       3       6      1       0.19     11622  sink late (host)
    64     4      1333      5.33      5       3       3      1       0.19      8620  sink late (host)
    64     6      1333      8.00      0       0       0      1       0.19      4013  -
    64     8      1333     10.66      0       0       0      1       0.19      6272  -
   128     2      2666      5.33      5       5       1      1       0.16      5286  sink late (host)
   128     3      2666      8.00      0       0       0      1       0.16      6630  -
   128     4      2666     10.66      0       0       0      1       0.16      1943  -
   128     6      2666     16.00     10       1      10      1       0.16     40711  sink late (host)
   128     8      2666     21.33      0       0       0      1       0.16      6146  -
   256     2      5333     10.67      2       1       2      1       0.15     16759  sink late (host)
   256     3      5333     16.00      0       0       0      1       0.15      6961  -
   256     4      5333     21.33      7       1       7      1       0.15     57353  sink late (host)
   256     6      5333     32.00     19       2      16      1       0.15    111456  sink late (host)
   256     8      5333     42.66     10       1      10      1       0.15     93684  sink late (host)
   512     2     10666     21.33      2       1       2      1       0.14     38457  sink late (host)
   512     3     10666     32.00      0       0       0      1       0.14      6714  -
   512     4     10666     42.66      0       0       0      1       0.14      5391  -
   512     6     10666     64.00      0       0       0      1       0.14      5401  -
   512     8     10666     85.33      0       0       0      1       0.14       340  -
```

Read the `min_q` column first: **it is 1 at every one of the twenty-five
points.** The render loop never came within a block of an underrun anywhere on
the surface. Every non-zero `under` is classified `sink late (host)` by the
same rule `test.sh` uses — the sink's own interrupt arrived later than the
`(ring − 1) × period` the ring covers, so the deadline was *moved*, not missed.
The non-monotonicity (128/6 fails while 128/4 and 128/8 pass) is the
signature: a structural limit does not come and go with ring depth.

So the honest summary of the surface at this machine speed is: **at RTF 0.148
the structure has margin everywhere from 32 frames up, and what the experiment
is actually measuring is this container's scheduling jitter.** That is a
useful negative result — the design is not marginal at any sane block size —
but the interesting question is elsewhere.

`-icount shift=N,sleep=off` would remove the last of that jitter by making
guest time completely independent of the host. It still **hangs**, exactly as
§ 9 recorded: re-confirmed 2026-09-18, killed after 200 s with no output. With
the render loop in `WFI` no instructions retire, so the virtual clock stops.

### 10.2 The interesting question: how slow before it breaks

`tools/sweep.sh --rtf --seconds 4`. Each row is one fictional machine speed.
`rtf` is measured on the same image with `--sink none`, a sink added for this
experiment that retires every block the instant it is committed, so wall/audio
is the pipeline's own cost rather than the metronome's. A `.` is zero
underruns; a number is underruns in a 4-second paced run.

```
 shift      rtf    peak   128/2   128/3   128/4    64/3   256/3   128/8
     0    0.037    0.31       3       .       .       1       .       .
     1    0.074    0.63       1       .       .       3       .       .
     2    0.148    1.26       .       .       7      20       .       .
     3    0.296    2.51       .       .       .       1       .       .
     4    0.592    5.03       1       .       .       .       .       .
     5    1.187   10.06     302     303     303     783     129     303
     6    2.376   20.12    1679    1670    1670    3514     816    1670
```

The `rtf` column is exactly 0.037 x 2^shift across all seven rows, which is the
arithmetic proof that `-icount shift` is a clean speed dial and that the
"microseconds" it produces are instruction counts and not time.

Everything at and below RTF 0.59 is host jitter — note that the counts do not
fall as the fictional machine gets *faster*, which is the giveaway. At RTF 1.19
every single point fails, and **they fail by almost exactly the same amount**:
303 dropouts at ring 2, 303 at ring 3, 303 at ring 4, 303 at ring 8. Twenty-one
milliseconds of audio in flight buys precisely nothing over five.

(`peak` in that table is from the unpaced free-run and is dominated by the
whole 16 kB bank dump arriving in one drain; see § 10.5. The paced runs' peak
is 0.16 × 2^(shift−2).)

### 10.3 The cliff, in fine steps

`-icount`'s shift is an integer, so § 10.2 can only step by factors of two and
the interesting region falls between two steps. `tools/sweep.sh --cliff` gets a
continuous knob a different way: it fixes the machine and **moves the
deadline**. `--rate R` tells the audio sink the stream is R Hz, so the block
period becomes frames/R while the engine still renders the same 128 frames of
the same work — so R = 48000·k is exactly an RTF multiplier of k. It is not a
real 48 k→R mode and the audio would play at the wrong speed; nothing listens
to it.

`tools/sweep.sh --cliff --shift 4 --seconds 4`:

```
    rate     rtf   128/2   128/3   128/4   128/6   128/8    64/3   256/3
   48000    0.59       .       .       .       .       .       2       4
   57600    0.71      10       .       .       .       .       .       .
   62400    0.77       1       .       .       .       .       6       .
   67200    0.83       .      18       .       .       .       7       .
   72000    0.89       .       .       .       .       .       2       1
   76800    0.95       1       1       1       .       .      12       .
   86400    1.07     144     143     143     143     143     566      37
   96000    1.18     512     511     511     511     511    1370     213
```

**The cliff is at RTF 1.00, to within the 0.06 resolution of this sweep, and
ring depth does not move it by one block.** At RTF 1.07 the ring-8
configuration — 21.3 ms of audio in flight, eight times the block period —
fails with 143 dropouts, the same 143 as ring 2 with 5.3 ms. Below the cliff
the scattered single digits are host jitter; above it the counts are a fixed
fraction of the blocks in the run.

### 10.4 What ring depth *does* buy, measured exactly

Sustained throughput is not it. Transient absorption is, and the amount is
exact. `--stall-at N --stall-us U` holds the producer for U µs immediately
before block N, which from the sink's point of view is indistinguishable from
a `render()` that overran by the same amount. The image counts underruns that
happen *during* the stall separately from underruns outside it, so this is an
arithmetic identity rather than a statistic:

```sh
$ qemu-system-arm -M virt -cpu cortex-a7 -icount shift=2 ... \
    --engine probe --midi bank --realtime --block 128 --ring R \
    --stall-at 400 --stall-us U
```

| ring | ring held at stall | 1 period | 2 | 3 | 5 | 10 |
|---|---|---|---|---|---|---|
| 2 | 1 | 0 | 1 | 2 | 4 | 9 |
| 3 | 2 | 0 | 0 | 1 | 3 | 8 |
| 4 | 3 | 0 | 0 | 0 | 2 | 7 |
| 6 | 5 | 0 | 0 | 0 | 0 | 5 |
| 8 | 7 | 0 | 0 | 0 | 0 | 3 |

Dropouts caused by a stall of *n* block periods. Every cell is
`max(0, n − (ring − 1))`, and the image says `stall accounted EXACT` at all
twenty-five points. Two things follow, and both are now `make test`
assertions:

1. **A ring of depth d absorbs exactly d − 1 block periods of overrun.**
   `port/DESIGN.md` § 2.4 asserts this ("a single block that overrun can eat up
   to `queued × 2.667 ms`"); it is now measured, at four depths and five
   overrun lengths, with the container's own jitter excluded by construction.
2. **Recovery is one sink period, always.** `underrun bursts` is 1 and
   `recovery` is 1 in every overrun case above. The loop does not drift: it is
   back at target occupancy on the very next period, because `src/timer.c`
   rearms the tick from an absolute compare value (CNTP_CVAL) rather than
   reloading a countdown, so a late service does not shift the deadline
   sequence. A design that used CNTP_TVAL would have turned every dropout into
   a permanently slower audio clock, and nothing downstream would have noticed.

### 10.5 What this says the fallback should be

`port/DESIGN.md` § 2.2 says: *"Quadruple buffering (depth 4) is the knob to
turn if A's RTF comes back above ~0.7."* **The data says that is the wrong
knob.** Depth 4 does not help at RTF 1.07 — it fails identically to depth 2 —
and at RTF 0.95 depth 2 is already fine. Ring depth is a shock absorber for
*one bad block*, not a throughput reserve, and § 2.4 of the same document says
so correctly; it is § 2.2's sentence that invites the wrong reading.

What the surface says instead, in order:

| If A's measured RTF is | do this | why |
|---|---|---|
| below ~0.7 | **keep 128 / 3** | passes everywhere in § 10.3, and 128/3's 8 ms of flight is the smallest that also satisfies § 11's consumer bound for a per-block consumer |
| 0.7 to 0.9 | keep 128 / 3, **raise depth only if the peak-to-mean ratio is large** | depth buys (d−1) block periods of absorption for one bad block; size it from the measured peak, not from the mean |
| above ~0.95 | **do not touch block size or ring depth** | neither moves the cliff. Go to `port/DESIGN.md` § 2.1's real fallback: `AnalogOutputMode_COARSE` at 32 kHz, which removes the 16-tap stereo FIR and one third of the output samples; or reduce `max_partials` below 32 |
| above ~1.3 | the part is wrong for the job | no software knob in this layer closes a 30 % deficit |

One more thing the surface says: **block sizes below 128 cost margin and buy
latency that the fixed costs swamp.** At 32 frames the block period is 666 µs
and every point in the row shows host jitter as dropouts, while the latency
model saves only 1.0 ms typical against 128 frames — against a floor of
0.75 ms of wire, DAC and I²S that no software decision touches
(`port/DESIGN.md` § 2.5). 128 frames is the right answer and the sweep does not
argue with it.

### 10.6 The latency each point costs

Nothing in QEMU can measure audio latency: there is no DAC, and the "audio"
is a checksum. These are `port/DESIGN.md` § 2.5's model evaluated at each
point, and they are **derived, not measured** — included because the sweep is
useless without the other half of the trade.

| block | ring | flight | typical | worst |
|---|---|---|---|---|
| 64 | 3 | 4.0 ms | 2.74 ms | 4.74 ms |
| 128 | 2 | 5.3 ms | 2.07 ms | 6.07 ms |
| **128** | **3** | **8.0 ms** | **4.74 ms** | **8.74 ms** |
| 128 | 4 | 10.7 ms | 7.40 ms | 11.40 ms |
| 128 | 6 | 16.0 ms | 12.74 ms | 16.74 ms |
| 256 | 3 | 16.0 ms | 8.74 ms | 16.74 ms |

The 128/3 row reproduces `port/DESIGN.md` § 2.5's headline 4.7 ms / 8.8 ms,
which is the arithmetic check that the sweep's model is that model.

---

## 11. The failure modes

`port/host/test.sh` covers the happy path and two malformed cases. These are
the rest, and each one is a thing a DOS machine, a MIDI interface or a cable
can actually do. All of them are now `make test` assertions.

### 11.1 The parser against a second implementation of its own contract

`tools/gen_vectors.py` now contains `model()` — an independent implementation
of the contract written in `port/include/mtp_midi_parser.h`, sharing no code
with `port/src/mtp_midi_parser.c` — and compiles what it predicts into the
image alongside each stream. The image prints measured against expected and
says `MATCH` or `MISMATCH`, so the suite's assertions are no longer numbers
pasted from a previous run of the thing under test.

The model reproduces the three counts `port/host/test.sh` already asserts
(demo 8/1, bank 12/64/8, bad 2/0/3/1/1) without being told them, which is the
evidence that it is a model of the contract and not of the implementation.

```
$ python3 tools/gen_vectors.py src/
stream     bytes  wire_s  short sysex    rt orphan trunc abort    sysexB hash
demo          49    0.02      8     1     1      0     0     0        30 0xd05d6098
bank       16418    5.25     12    64     8      0     0     0     16384 0x0c4abe05
bad        40116   12.84      2     0     0      3     1     1         0 0x811c9dc5
rtsysex      271    0.09      2     1     9      0     0     0       256 0x2ba48b8f
runstat      802    0.26    400     0     1      0     0     0         0 0x811c9dc5
panic       4992    1.60   1664     0     0      0     0     0         0 0x811c9dc5
trunc        131    0.04      1     0     0      0     0     0         0 0x811c9dc5
```

All seven `MATCH` with `--engine probe` and all six applicable ones `MATCH`
with the real `mt32emu` engine.

### 11.2 A sysex interleaved with real-time bytes

`rtsysex`: a 254-byte Roland patch dump with nine System Real Time bytes
(`F8 FE FA F8 FC FE F8 FE F8`) wedged into it — after the `F0`, after every
32nd byte, and immediately before the `F7`. Legal per the MIDI spec, and
routine from any interface that emits Active Sensing during a bank load.

*Should happen:* the synth receives the dump byte for byte as if nothing had
been inserted, and the nine real-time bytes are delivered separately.

*Does happen:* exactly that. This is the case that motivated
`src/engine_probe.c`: a *count* cannot express "the same 254 bytes", so the
probe engine hashes every sysex payload byte it is handed (FNV-1a, including
the F0 and the F7) and the image compares:

```
parser: short 2 sysex 1 realtime 9
engine saw          1 sysex, 256 bytes, longest 256, 2 short, 9 realtime, 0 panic CCs
sysex payload hash  0x2ba48b8f (expected 0x2ba48b8f)
sysex vs contract   MATCH
```

*Acceptable?* Yes, and it is the strongest single assertion in the suite: the
16 kB `bank` stream's 64 dumps hash to `0x0c4abe05` on both the model and the
hardware, so 16 384 bytes of patch data survive 6 000 audio blocks of
reassembly unchanged.

### 11.3 Running status across a read boundary

`mtp_render.c:11` reads MIDI in batches of `MIDI_BATCH = 64` bytes. The
`runstat` vector is one `0x90` followed by 400 two-byte running-status
messages, so a message straddles byte 64 and — since 64 is even and the
messages are two bytes — every batch boundary after it as well. One real-time
byte is also wedged between a key and its velocity.

*Should happen:* 400 short messages, 1 real-time, nothing dropped. A parser
that resets pending state per call would lose one message per 64 bytes, i.e.
about 12 of 400.

*Does happen:* `short messages 400`, `realtime 1`, orphan 0.

*Acceptable?* Yes. Worth noting that this was never really in doubt — the
parser holds its state in a caller-owned struct — but it was also never tested,
and "the bytes arrive in whatever chunks the FIFO gives you" is the single
most common way a MIDI parser is wrong.

### 11.4 Oversize and truncated sysex

Both were already covered by `port/host/test.sh` case 3 and both behave: a
40 002-byte sysex against the 32 768-byte cap is counted `sysex truncated 1`
and **not** emitted (the parser must not hand half a message to something that
will checksum it), and a sysex interrupted by a status byte is counted
`sysex aborted 1`.

The new case is the third way: **the stream simply stops.** `trunc` ends
inside a patch dump; no `F7`, no aborting status, no more bytes ever.

*Should happen:* nothing. 0 sysex emitted, 0 truncated, 0 aborted — because
none of those events happened; the parser just holds an incomplete message
until reset.

*Does happen:* exactly that.

*Acceptable?* Yes, and it is the right answer for a box someone plugs a DOS
machine into: the machine is switched off mid-dump, and when it comes back the
next `F0` starts cleanly. The failure this would catch is a parser that
flushes at EOF.

**One wrinkle, and it cost a test failure before it was understood.** `bad` is
40 116 bytes: 12.84 s of wire at 31250 baud. Asserting its expectations after a
6-second paced run fails, because the oversize sysex has not finished arriving
— `sysex truncated` is legitimately still 0. `test.sh` now derives each
stream's run length from its wire time. This is not a curiosity: **on the
hardware, a 32 kB sysex occupies the MIDI wire for ten seconds**, and anything
that assumes a message completes promptly is wrong by an order of magnitude.

### 11.5 MIDI arriving faster than 31250 baud allows

`--baud N` sets the pacing rate. It is not a knob the product has — DIN MIDI is
31250 and that is that — but a merger, a USB-MIDI bridge or a host that buffers
and bursts can all deliver faster than the wire, and the question is what
breaks first.

The answer is a structural one that had not been written down. `drain_midi()`
in `port/src/mtp_render.c` reads the MIDI source **without bound**: it loops
until a read returns less than `MIDI_BATCH`, and hands every parsed message
straight to the engine. A sysex the engine refuses is stashed and retried
(§ 11.6). **A short message the engine refuses is counted and dropped.**

```c
case MTP_MSG_SHORT:
    ctx->stats.short_msgs++;
    if (ctx->engine->short_msg(ctx->inst, m->msg, ts) != MTP_OK)
        ctx->stats.engine_backpressure++;    /* ...and that is all */
    break;
```

So the pipeline's short-message capacity is exactly **the engine's event-queue
depth per block period**, and nothing above the seam recovers an overflow.
`--engine-queue N` models that queue (mt32emu's own default is 1024,
`globals.h:117`; `port/host/engine_fake.c`'s is 64), draining it once per
`render()` as mt32emu does.

`--midi panic` is 1664 messages: 128 notes on across 16 channels, then 32
rounds of All Sound Off / Reset All Controllers / All Notes Off on every
channel. Every one of the 1536 controller messages is one that must not be
lost — a dropped All Notes Off is a note that hangs until the box is
power-cycled.

| pacing | engine queue | accepted of 1664 | lost |
|---|---|---|---|
| 31250 baud (DIN MIDI) | 1024 | 1664 | 0 |
| 31250 baud | 64 | 1664 | 0 |
| 125 000 baud (4×) | 64 | 1664 | 0 |
| 500 000 baud (16×) | 64 | 1664 | 0 |
| 600 000 baud | 64 | 1664 | 0 |
| 700 000 baud | 64 | 1633 | **31** |
| 800 000 baud | 64 | 1433 | **231** |
| 1 000 000 baud (32×) | 64 | 1201 | **463** |
| 1 000 000 baud | 1024 | 1664 | 0 |
| unpaced (whole stream at once) | 1024 | 1024 | **640** |
| unpaced | 64 | 64 | **1600** |

**The rule, with a number.** A 3-byte MIDI message is 30 bits. At baud rate B
the arrival rate is B/30 messages per second, and a block period of 2.667 ms
admits B·0.002667/30 of them. That exceeds a 64-deep queue above
**720 000 baud**; first loss is measured at 700 000 (62.2 messages per block on
average), slightly early because arrivals are not aligned to block boundaries
so the peak per block exceeds the mean.

| queue depth | sustained message rate before loss | margin over DIN MIDI's 1042 msg/s |
|---|---|---|
| 64 (`engine_fake.c`) | ~24 000 msg/s | **23×** |
| 1024 (mt32emu default) | ~384 000 msg/s | **369×** |

*Acceptable?* For a DIN MIDI box, comfortably — 369× is not a margin anyone
needs to think about, and the design is safe. But the margin is *finite and
undocumented*, and the failure at the end of it is silent and permanent (a
stuck note). Two things follow for `port/`:

- The unpaced rows are the real warning. They are not a wire; they are what
  happens when the source can deliver an arbitrary amount in one go — a
  file-backed source, a USB endpoint, an RTOS driver with a work queue, or a
  UART FIFO that was allowed to fill while something else ran long. In that
  case even mt32emu's 1024-deep queue loses 640 of 1664 messages.
- The asymmetry is worth a sentence somewhere: **sysex is retried, short
  messages are not.** If that is deliberate, say why (a dropped note-off is
  less bad than a reordered patch dump); if it is not, `drain_midi()` should
  stop reading when the engine refuses a short message, the same way it
  already does when the engine refuses a sysex.

There is a second, smaller finding in the same area. `mtp_render.c` passes
`MTP_MSG_REALTIME` to `engine->short_msg()` and does **not** count it in
`stats.short_msgs`, so every Active Sensing or MIDI Clock byte takes an
event-queue slot that no counter above the seam accounts for. The probe engine
counts them separately and the image prints both. At the usual rates this is
3.3 events/s for Active Sensing and 48 events/s for MIDI Clock at 120 bpm —
irrelevant against a 1024-deep queue, but it should not be invisible.

### 11.6 Sysex back-pressure and the one-slot retry

`mtp_render.c` stashes exactly one refused sysex and retries it on the next
block, refusing to read further MIDI until it lands. With `--engine-queue 1`
every one of the 64 patch dumps in the bank stream has to go through that path:

| engine queue | sysex delivered | bytes | back-pressure events | payload hash |
|---|---|---|---|---|
| unlimited | 64 | 16 384 | 0 | MATCH |
| 4 | 64 | 16 384 | 0 | MATCH |
| 2 | 64 | 16 384 | 7 | MATCH |
| 1 | 64 | 16 384 | 10 | MATCH |

*Acceptable?* Yes. The one-slot retry is sufficient for a real bank load even
against a one-event queue, and the payload still hashes correctly, so nothing
was reordered or duplicated by the retry.

### 11.7 A render that overruns its deadline

Two independent observations, and they agree.

The injected one is § 10.4's table. The *natural* one happens on its own: with
the real engine and the bank stream delivered unpaced,

```
worst render        3352 us  (block period 2666 us)
underruns           0
min ring occupancy  1 of 3
```

A block took 1.26 × its own playing time and nothing dropped out, because the
ring absorbed it and the next blocks caught up — `port/DESIGN.md` § 2.4's
shock absorber, observed rather than argued. `make test` asserts it.

*Acceptable?* Yes, with one caveat that belongs to the harness rather than the
design: that 3352 µs is an artefact of unpaced delivery. The whole 16 kB dump
arrives in a single `drain_midi()` call and one block pays for all of it. Paced
at 31250 baud the same run's worst render is 0.16 × the period. **A real wire
delivers at most 8.3 bytes per 2.667 ms block**, so the peak the hardware will
see is nothing like this — and any harness that feeds MIDI from a file without
pacing is measuring its own artefact. `bench/`'s RTF harness should pace, or
should say that it does not.

---

## 12. The ring's second constraint, now with a number

§ 7 found that ring depth has a constraint the design had not accounted for —
the consumer's service granularity — and did not put a number on it.
`src/virtio_snd.c` now timestamps the device's used-buffer interrupts and the
image prints the worst gap between them, so the rule can be stated.

```sh
$ qemu-system-arm ... -device virtio-sound-device,audiodev=snd0 \
    --engine probe --midi demo --seconds 2 --sink virtio --block B --ring R
```

| block | ring | period | flight | dry periods | max completed per IRQ | worst service gap | min occupancy |
|---|---|---|---|---|---|---|---|
| 128 | 2 | 2.67 ms | 5.3 ms | 187 | 2 | 10 218 µs | 1 |
| 128 | 3 | 2.67 ms | 8.0 ms | 125 | 3 | 10 155 µs | 1 |
| 128 | 4 | 2.67 ms | 10.7 ms | 81 | 4 | 10 430 µs | 1 |
| 128 | 5 | 2.67 ms | 13.3 ms | 4 | 5 | 11 751 µs | 1 |
| 128 | 6 | 2.67 ms | 16.0 ms | 2 | 6 | 11 471 µs | 1 |
| 128 | 8 | 2.67 ms | 21.3 ms | 1 | 8 | 14 323 µs | 1 |
| 256 | 2 | 5.33 ms | 10.7 ms | 89 | 2 | 10 240 µs | 1 |
| 256 | 3 | 5.33 ms | 16.0 ms | 6 | 3 | 13 793 µs | 1 |
| 256 | 4 | 5.33 ms | 21.3 ms | 1 | 4 | 10 486 µs | 1 |
| 256 | 5 | 5.33 ms | 26.7 ms | 0 | 4 | 12 441 µs | 1 |
| 256 | 6 | 5.33 ms | 32.0 ms | 0 | 4 | 13 496 µs | 1 |
| 256 | 8 | 5.33 ms | 42.7 ms | 0 | 4 | 12 362 µs | 1 |

`min occupancy` is 1 in all twelve rows: **the renderer was never behind, at
any depth, at either block size.** The starvation is entirely the consumer's,
and its cause is now measured rather than inferred — QEMU's audio backend
services its `wav` backend on a timer whose worst observed interval is
**10.2 to 14.3 ms**, independent of block size and of ring depth, and it takes
as many periods in one go as the ring will give it (`max completed per IRQ`
tracks the ring depth exactly until it saturates).

**The rule, stated:**

> A ring must hold at least one of the consumer's service intervals, plus one
> block for the writer:
>
>     depth ≥ ceil(consumer_service_interval / block_period) + 1
>
> This bound is independent of the renderer and is not relaxed by the renderer
> being fast. The design must take the larger of it and the renderer-slack
> bound (`port/DESIGN.md` § 2.4).

Checking `port/DESIGN.md`'s ring sizing against it:

| consumer | service interval | bound at 128 frames | DESIGN's depth 3 |
|---|---|---|---|
| T113 DMAC, one completion IRQ per descriptor | 1 block = 2.67 ms | **2** | **correct**, and the renderer-slack bound dominates |
| T113 DMAC with half/full interrupts on a 3-block buffer | 1.5 blocks = 4 ms | 3 | correct, with nothing to spare |
| QEMU virtio-sound + `wav` backend (measured) | 10.2–14.3 ms | **6 to 7** | **wrong**: 125 dry periods per second of audio |
| an RTOS driver servicing from a work queue at 10 ms | 10 ms | 5 | would break |
| USB isochronous, 1 ms frames, 8-frame service | 8 ms | 4 | would break |

So: depth 3 is defensible on the T113 **only because the DMAC's granularity is
one block**, and that is a property of the chosen peripheral, not of the audio
design. The recommended text for `port/DESIGN.md` § 2.2 is in § 8.10.

The residual 1–2 dry periods at depths that satisfy the bound are start-of-
stream: the device is started before the ring is primed. That is the same
effect § 8.1 describes for the timer sink, and the same fix applies.

---

## 13. The dual-core claim: what this environment can and cannot settle

`port/PORTING.md` § 5 found that mt32emu's MIDI event queue synchronises its
producer and consumer with `volatile` alone, which is not sufficient on a
weakly-ordered dual-core Cortex-A7, and `port/DESIGN.md`'s answer is to keep
MIDI and rendering on one core. The brief was: demonstrate the hazard, or
demonstrate that it cannot be demonstrated here and say why.

**The answer is the second one, and it is sharper than "we could not reproduce
it": the class of reordering that threatens mt32emu's queue is precisely the
class this environment cannot produce, and a control experiment in the same
image proves the environment is not simply inert.**

### 13.1 The strongest evidence needs no second core at all

`make mt32emu` cross-compiles Munt 2.8.3 for `-mcpu=cortex-a7` with exactly the
flags the T113 build will use. Disassembling the result:

```
$ arm-linux-gnueabihf-objdump -d --demangle build/mt32emu/Synth.o \
    | sed -n '/MidiEventQueue::pushShortMessage/,/^$/p'
...
  5c:   str     r3, [sl, r6]      ; newEvent.sysexData = NULL
  60:   str     r8, [r9, #4]      ; newEvent.shortMessageData = shortMessageData
  64:   str     r7, [r9, #8]      ; newEvent.timestamp = timestamp
  68:   str     r5, [r4, #16]     ; endPosition = newEndPosition
  6c:   pop     {r4, r5, r6, r7, r8, r9, sl, pc}
```

Three payload stores and then the index store, with nothing between them.

```
$ arm-linux-gnueabihf-objdump -d build/mt32emu/libmt32emu.a \
    | grep -cE "\b(dmb|dsb|isb|ldrex|strex)\b"
0
```

**The entire library, compiled for a Cortex-A7, contains zero barrier and zero
exclusive instructions.** `volatile` did what `volatile` does — it stopped the
compiler reordering the stores — and emitted nothing that constrains the
hardware. On an ARMv7-A core the store to `endPosition` may reach the other
core before the stores to the event body, and the consumer's
`startPosition == endPosition` check has no acquire to pair with. That is not
an inference about mt32emu's source; it is what the machine code says.
`port/PORTING.md` § 5 is confirmed at the instruction level, which is a
stronger and cheaper result than any litmus test.

### 13.2 A second core does boot

`-M virt -smp 2` starts the secondary through PSCI, and its device tree says
`method = "hvc"` (verified with `-machine dumpdtb`). `src/smp.c` calls it and
`src/start.S` gained a `_secondary_start` entry that brings the core up the
same way `_start` does, minus clearing core 0's BSS:

```
--- smp ---
conduit             hvc
psci version        0x00010001  (major 1 minor 1)
cpu_on(mpidr=1)     0
second core         alive after 808 us
```

The `smc` conduit is **UNDEFINED at PL1** in this configuration and the image's
own undefined-instruction handler catches it and exits cleanly, which is a
small free test of the abort path:

```
conduit             smc
*** undefined instruction at pc 0x402032dc
```

The MMU is enabled on the secondary before any test runs, and that is
load-bearing rather than tidiness: **with the MMU off, ARMv7-A treats every
access as Strongly Ordered**, in which no reordering is architecturally
possible. A litmus test run with the MMU off is rigged to find nothing.

That is not an argument from the architecture reference manual; it is
measured. The identical source built both ways, same host, same QEMU
invocation:

| build | SB rounds | SB both-zero |
|---|---|---|
| `make mt32emu` (MMU on, Normal cacheable shareable) | 200 000 | **7 914** |
| `make MMU=0 mt32emu` (MMU off, Strongly Ordered) | 200 000 | **0** |

So the control is measuring a real property of the memory type and not host
noise — which is exactly what a control has to demonstrate before its silence
elsewhere can be believed. `test.sh` recognises the MMU=0 case and skips the
control rather than failing it.

### 13.3 The experiment, and its control

Two litmus tests, both across the two cores, both in Normal cacheable shareable
memory:

- **MP (message passing)** — mt32emu's queue restated with the same field order
  and the same absence of barriers. Strict ping-pong: the producer waits until
  the queue is *empty* before publishing, so the consumer is spinning on
  `endPosition` at the instant it is written. (The first version used the full
  64-entry ring; it observed 2 000 000 events and could not have caught
  anything, because the consumer always ran far behind the payload stores.)
- **SB (store buffering)** — `x=1; r1=y ‖ y=1; r2=x`. Both reads returning zero
  is impossible under sequential consistency. This is the **control**: if it
  never fires, the environment is sequentially consistent and the MP result
  means nothing.

`-icount` is deliberately absent, because it forces single-threaded TCG.

```
$ qemu-system-arm -M virt -cpu cortex-a7 -smp 2 -accel tcg,thread=multi ... \
    --smp hvc --smp-mp 2000000 --smp-sb 2000000
```

| accelerator | MP iterations | MP violations | SB rounds | SB both-zero |
|---|---|---|---|---|
| `thread=multi`, run 1 | 2 000 000 | **0** | 2 000 000 | **128 691** (6.4 %) |
| `thread=multi`, run 2 | 2 000 000 | **0** | 2 000 000 | **155 063** (7.8 %) |
| `thread=multi`, run 3 | 2 000 000 | **0** | 2 000 000 | **91 153** (4.6 %) |
| `thread=multi`, run 4 (`make smp`) | 2 000 000 | **0** | 2 000 000 | **32 861** (1.6 %) |
| default (= `multi`) | 200 000 | 0 | 200 000 | 7 914 |
| `thread=single` | **25 in 5 s** | 0 | **0 of 200 000** | 0 |

### 13.4 Why, and what it means

**Eight million message-passing publications, zero violations. Eight million
store-buffering rounds, four hundred and eight thousand violations** (1.6 % to
7.8 % depending on how the host happened to schedule the two vCPU threads).
Both numbers come from the same image, the same two cores and the same run.

The reason is that TCG is not a memory-ordering model. It translates each guest
load and store into a host load and store and relies on the *host's* ordering;
it inserts fences only where the guest asks for them. The host here is x86-64,
which is TSO:

| reordering | allowed on ARMv7-A | allowed on x86-64 (TSO) | observable here |
|---|---|---|---|
| store → load (SB) | yes | **yes** | **yes, 1.6–7.8 %** |
| store → store (MP producer) | yes | no | **no** |
| load → load (MP consumer) | yes | no | **no** |

**mt32emu's queue is exposed to store→store and load→load. Those are exactly
the two rows this environment cannot produce.** The 0 in the MP column is a
property of x86-64, not of the code under test, and it must never be quoted as
evidence that the queue is safe.

What the control buys is that this is a *bounded* negative result rather than a
shrug: the harness demonstrably can see non-sequentially-consistent execution,
it ran eight million tight publications, and it still saw nothing — because the
one thing it cannot show is the one thing that matters. `test.sh` asserts the
control fires and fails loudly if it does not, so nobody can later read a
silent "0 violations" as reassurance.

**Cost of not knowing this: about a day.** That is the finding.

**What would settle it**, in rough order of cost:

1. **Read the disassembly** — done, § 13.1, and it is conclusive on its own for
   the *code*, if not for the frequency.
2. **Any real ARM SMP machine**, including a Raspberry Pi 2/3 running Linux:
   compile `src/smp.c`'s two litmus tests as a userspace program with two
   pinned threads. MP fires on real Cortex-A7/A53 within seconds. This costs
   nothing and needs no T113.
3. A model that actually implements ARM's memory model (`herd7`/`litmus7` from
   the `diy` tool suite), which would answer it without any hardware.
4. The T113 itself, which is the least useful of the four because by then the
   design has already decided not to need the answer.

**None of this changes the design.** `port/DESIGN.md` keeps MIDI and rendering
on one core, so the queue is never crossed by two cores and the hazard is
avoided rather than fixed. What § 13 adds is that the avoidance is *necessary*
— the library will not save you — and that the reason nobody has tripped over
it on x86 desktops is that x86 desktops cannot trip over it.

---

## 14. The real synthesiser through the whole suite

§ 6 got `mt32emu` linked, opened and rendering on one stream. It now runs the
whole conformance suite: every vector, paced at 31250 baud, for at least the
stream's own wire time.

```sh
$ cd emu && make mt32emu && make test
```

| stream | parser vs contract | heap grown while rendering |
|---|---|---|
| demo | MATCH | 0 B |
| bank (16 kB of patch dumps) | MATCH | 0 B |
| bad (40 kB oversize + abort + orphans) | MATCH | 0 B |
| rtsysex | MATCH | 0 B |
| runstat (400 running-status messages) | MATCH | 0 B |
| trunc | MATCH | 0 B |

**Zero heap growth on the render path across all six**, including the 40 kB
oversize-sysex stream, which is the case most likely to provoke an allocation
(mt32emu's own `MidiStreamParserImpl` would have reallocated its buffer here —
`port/DESIGN.md` § 3.1's reason for not using it, now checked against the
behaviour of the library it was avoiding).

### 14.1 Footprint, re-measured with the real engine

```
$ make clean && make && make mt32emu && make size
```

| | text | data | bss | raw `.bin` |
|---|---|---|---|---|
| Image without `mt32emu` | 113 552 | 788 | 13 815 892 | **114 340 B** |
| Image with `mt32emu` | 206 392 | 4 192 | 13 819 020 | **210 584 B** |
| `mt32emu`'s contribution | **+92 840** | **+3 404** | +3 128 | **+96 244 B** |
| `libmt32emu.a` itself | 89 696 | 3 609 | 3 036 | — |

`mt32emu`'s contribution is **the same as § 6's** to within eight bytes of
section alignment (+92 848 / +3 404 there, +92 840 / +3 404 here), which is the
check that nothing in this session changed the synthesiser's cost.

The image itself grew from § 6's 194 512 B to 210 584 B. **All of that is test
apparatus, and 62 779 B of the total is MIDI test vectors in `.rodata`:**

```
$ arm-linux-gnueabihf-nm --size-sort -S build/mt32emu-bare.elf | grep midi_
midi_demo            49      midi_panic         4992
midi_trunc          131      midi_bank         16418
midi_rtsysex        271      midi_bad          40116
midi_runstat        802      TOTAL             62779
```

So **the product-shaped part of this image is 147 805 B — about 144 KiB — with
the synthesiser in it**, and a T113 payload would carry no vectors at all. The
40 kB `bad` vector alone is a fifth of the `.bin`; it is there because
`port/host/test.sh` uses those exact bytes and the two suites must answer
questions about the same data.

### 14.2 Heap, re-measured

```
$ make run ARGS="--engine E --midi bank --realtime --seconds 8"
```

| engine | heap after setup | high water | grown by run | uncached |
|---|---|---|---|---|
| `fake` | 9 464 B | 9 464 B | **0 B** | 1 536 B |
| `probe` | 0 B | 0 B | **0 B** | 1 536 B |
| `mt32emu-fakerom` | 1 608 728 B | 1 608 728 B | **0 B** | 1 536 B |

**1 608 728 B = 1 571 KiB**, unchanged from § 6 and still 3 % over
`port/PORTING.md` § 7's 1 525 KiB x86-64 prediction for the same configuration,
for the same reason (this image's bump allocator never reuses the 589 824-byte
PCM staging buffer). The 4 MB arena § 7 recommends remains right with room to
spare. `probe` allocates nothing at all, which is why it is the engine the
failure-mode cases use: a footprint of zero makes "heap grown by run 0" mean
something when the thing under test is the loop rather than the synth.

---

## 15. What is asserted now, and what one run costs

```
$ cd emu && make mt32emu && make test
...
all tests passed

real    1m46s

$ for i in 1 2 3; do ./test.sh 2>&1 | grep -c '^ok'; done
86
86
86
```

**Eighty-six assertions where § 1 had thirty-three**, three consecutive clean
runs. The fifty-three new ones, by group:

| group | assertions | what fails loudly |
|---|---|---|
| parser vs independent contract model (§ 11.1) | 7 | MISMATCH on any of the seven streams |
| sysex payload byte-exactness (§ 11.2) | 4 | the hash the model computed |
| running status across read boundaries (§ 11.3) | 2 | fewer than 400 messages |
| truncated-at-EOF (§ 11.4) | 2 | anything emitted or miscounted |
| underrun absorption and recovery (§ 10.4) | 12 | absorption ≠ (depth−1) periods, or recovery > 2 periods |
| panic storm and the drop limit (§ 11.5) | 3 | any loss at 31250 baud — **or no loss at 800 kbaud**, which would mean the back-pressure path changed and the margin is unknown again |
| sysex retry under back-pressure (§ 11.6) | 3 | fewer than 64 dumps, or a bad hash, or the retry path never taken |
| consumer service granularity (§ 12) | 3 | the interval cannot be measured, or the shallow ring does not starve more than the deep one |
| dual core (§ 13) | 4 | the second core not booting, or **the SB control not firing** |
| real engine across every vector (§ 14) | 13 | any MISMATCH, any heap growth |
| | **53** | |

Two of those deserve calling out as deliberately inverted, because they are the
ones that keep the suite honest rather than green:

- **§ 11.5's 800 kbaud case fails if nothing is dropped.** The test exists to
  prove the limit is where the arithmetic says, so that the 31250-baud margin
  is a measured 23× rather than an assumption.
- **§ 13's control fails if no store-buffering violation is seen.** A litmus
  test that observes nothing *and cannot observe anything* is worth nothing,
  and the suite must not report it as a pass.

---

## 16. The first thing to try on real hardware

In this order, and the first step is worth its own session.

1. **Build `make uimage`, load it with `xfel` or over U-Boot, and start it with
   `bootm` — and read the entry-state dump.** Change two constants first:
   `PL011_BASE` in `src/console.c` becomes the T113's 16550 at 0x02500000 (and
   the driver becomes a 16550 — about forty lines behind the same three
   functions), and `GICD_BASE`/`GICC_BASE` in `src/gic.c` become 0x03020000 and
   0x03021000. Nothing else should need to change to get a console.

   That single boot answers, in one screen: whether U-Boot handed over secure or
   non-secure (`id_pfr1`, `cpsr`), whether `bootm` really left the MMU and caches
   off as `boot/BRINGUP.md` § 4.1 says, whether `CNTFRQ` is the 24 MHz everyone
   assumes, and whether the image runs at the address it was linked for. Those
   are four assumptions the whole port rests on, and they cost one boot to check.

2. **`--engine fake --midi bank --sink timer`, with the tick driven by the
   generic timer and nothing else connected.** If that reports zero underruns on
   silicon, the render loop, the parser, the ring discipline and the interrupt
   path are all working on the real part, before a single audio peripheral has
   been touched. If it does not, the counters say which of them broke — and
   because the identical assertions pass here and under `qemu-user` and on the
   host, the difference is a driver.

3. **Then, and only then, the I2S DMA ring**, replacing `emu_audio.c`'s timer
   tick with the DMAC's per-descriptor completion interrupt. The ring
   discipline, the non-cacheable mapping, the acquire/commit protocol and the
   underrun accounting above it are already written and already tested; what is
   new is the descriptor list and the clock tree. Keep `emu/`'s counters and
   compare the numbers directly.

4. **`--engine mt32emu` with real ROM dumps**, which is `docs/PLAN.md` § 0's gate.
   Everything needed to run it is in place: the link works, the arena is sized,
   the render path allocates nothing, and the ROM failure path is friendly. The
   only missing inputs are Roland's ROMs and a board.
