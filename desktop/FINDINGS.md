# `desktop/` — findings

Workstream F. Status: 2026-09-18, second pass. Written in a Linux container
with **no sound device, no MIDI hardware, no ROMs and no Mac**, which is most
of what this document is about.

The second pass added three things and found one bug that matters to the
product: a five-way byte-for-byte conformance run across every implementation
of the platform seam that exists (section 8), which found that **the same
mt32emu source renders 1 LSB differently on armv7 than on x86-64 with the
project's current compiler flags**
and bisected it to one line and one flag; an `mt32.cfg` format, parsed here so
that the SD card's format is exercised before the card exists (section 9); and
the real mt32emu running end to end in this container on fabricated ROMs.

---

## 1. What was actually built and run

Everything below was executed in this session, on x86-64 Ubuntu 24.04, GCC 13.3.

| | |
|---|---|
| `cmake -S desktop -B desktop/build && cmake --build desktop/build -j` | **builds clean**, no warnings at `-Wall -Wextra -Wshadow`, linking `mt32emu` built out of source from `bench/vendor/munt/mt32emu` |
| the same with `-DMT32EMU_SOURCE_DIR=` | builds, fake engine only |
| the same with `-DCMAKE_DISABLE_FIND_PACKAGE_ALSA=ON` | builds, `--midi-seq` disabled and says which package to install, everything else works |
| `./desktop/test.sh` | **45 assertions, all pass** (27 before this pass) |
| `./desktop/conform.sh` | five builds, four MIDI streams, two engines, 38 runs, 8 comparisons: **7 byte-identical across every implementation**, 1 divergence of 1 LSB on 0.07% of samples, bisected to one line and one compiler flag (section 8.2) |
| `--engine mt32emu-fakerom` | the real mt32emu, opened on fabricated ROM images, renders here: 375 blocks, 0 underruns, peak 9300 on the `voice` vector |
| miniaudio **null** backend, 2 s and 8 s runs | runs, zero underruns at `--ring 8` |
| miniaudio **ALSA** backend, against ALSA's `null` PCM plugin | opens a real `snd_pcm`, starts, calls our callback with 128-frame periods. That PCM is unpaced, so it is a plumbing test, not a timing test |
| MIDI from **stdin** and from a **named FIFO** | 49-byte demo stream and a 16 kB timbre bank dump: parsed, zero errors |
| MIDI from a **serial tty**, driven byte-by-byte through a pty at 320 us spacing | `TCSETS2`/`BOTHER` set 31250 baud and read it back unchanged; 49 bytes arrived and parsed |
| **SMF** playback, including `--midi-loop` | format 0, tempo meta, running status expanded, sysex reconstructed; run ends by itself 2 s after the file does |
| `--midi-seq` (ALSA sequencer) | compiles and links against libasound; fails cleanly here because this container has no `/dev/snd/seq` |
| `mt32emu` with **no ROM files** | names the missing path, exits 1 |
| `mt32emu` with ROM files of the **right size and wrong contents** (64 kB + 512 kB of `/dev/urandom`) | "control ROM not recognised (wrong file, or a half image)", exits 1 |
| `--engine auto` with no ROMs | warns, explains what the fake engine is, falls back |
| WAV tap (`--tap-wav`) | 48 kHz stereo, peak 23409 from the demo stream — the audio really did reach the device path |
| Ctrl-C | clean shutdown, summary printed, exit 0 |

### What this verifies

1. **The platform seam is implementable a third time, unchanged.** `port/include`
   was written for a DMA ring and a UART. Nothing in it had to be modified to
   sit on top of a callback-driven sound card, a pthread, an ALSA sequencer or a
   Standard MIDI File. `port/src/*.c` and `port/host/engine_*.c*` are compiled
   here byte-for-byte as they are in `port/host`.
2. **The ring discipline survives a consumer that is genuinely concurrent.**
   `port/host`'s virtual play cursor advances when the render loop asks it to;
   this one advances on another thread whether we are ready or not, and the
   `acquire`/`commit`/`queued`/`wait` protocol in `port/src/mtp_render.c` still
   holds. That is a real single-producer/single-consumer test the WAV harness
   cannot do.
3. **The MIDI parser handles bytes that arrive the way bytes really arrive** —
   one at a time from a tty through `poll()`/`read()`, in 512-byte gulps from a
   FIFO, and as decoded sequencer events — rather than out of a `malloc`ed
   buffer. Running status, real-time bytes inside other messages, 16 kB sysex
   reassembly across ~2000 audio blocks, and all three malformed cases behave
   exactly as `port/host/test.sh` says they do.
4. **The `mt32emu` binding links and fails honestly.** Both failure modes
   (missing file, unrecognised image) produce the message `port/DESIGN.md` 4.3
   asks for, and the process exits non-zero. No audio is faked anywhere.

### What this does **not** verify

- **That anything is audible.** This machine has no sound device. Nothing here
  has ever been through a speaker. The ALSA run used ALSA's `null` PCM, which
  discards samples as fast as it is given them.
- **That the macOS build compiles**, let alone works. See section 4.
- **Anything at all about speed on a Cortex-A7.** See section 3.
- **That `mt32emu` produces correct MT-32 audio**, because there are no ROMs and
  the library refuses to open without recognised ones, by design.
- **Real MIDI hardware.** A pty is a tty, so the `termios` path is real, but no
  byte in this session has been through a UART, an optocoupler or a DIN socket.
  The 31250-baud setting was accepted and read back by a pty driver that is not
  clocking anything.
- **Latency.** The numbers in `port/DESIGN.md` 2.5 are unmeasured, and a
  desktop cannot measure the two fixed terms (wire time, DAC group delay) anyway.

---

## 2. The audio library: miniaudio

**Chosen: [miniaudio](https://miniaud.io) v0.11.25, vendored whole at
`vendor/miniaudio/miniaudio.h` (4.1 MB, one header) with its licence text at
`vendor/miniaudio/LICENSE`.**

**Licence: a choice of the Unlicense (public domain) or MIT-0** — MIT with the
attribution clause removed. Either is compatible with this repository and with
`mt32emu`'s LGPL 2.1, and neither adds an obligation to anyone who builds or
ships this. The full text is vendored unmodified; the notice at the end of
`miniaudio.h` is intact.

Why it beats the alternatives here:

- **It needs no development packages, on either OS.** On Linux it `dlopen`s
  `libasound.so.2` and `libpulse.so.0` at run time and declares their entry
  points itself, so `desktop/` builds on a machine with no `libasound2-dev` —
  which this container was, and which is exactly the "a person can actually use
  it" test. Verified: the ALSA backend enumerated and opened a device here with
  only `libasound.so.2` present. On macOS it uses CoreAudio, which ships with
  the OS.
- **One file, no build system of its own.** `port/` is deliberately a repository
  you can build with `cc` and `make`; adding a dependency that needs `pkg-config`
  and a package manager would be the wrong kind of first impression. Compare
  PortAudio (a library to build and install, and a `configure` to argue with),
  RtAudio (C++, and another build), SDL2 (enormous, and drags in a window
  system), JACK (a daemon the user has to be running), libsoundio (unmaintained
  since 2017).
- **It is one seam wide.** We use `ma_context`, `ma_device`, and a callback.
  Decoders, resampling, the node graph and the high-level engine are switched
  off in `vendor/miniaudio/miniaudio_impl.c` — 80% of its compile time and all
  of its file-format handling, gone.
- **It has a null backend**, which is what makes this build testable on a machine
  with no sound card at all, and a JACK backend for anyone who wants to measure
  latency properly later.

The cost is honest: 4.1 MB of vendored third-party C in a repository whose other
vendored thing is an emulator. It compiles in about ten seconds with the
decoders off, is built with `-w` so its style does not pollute our warnings, and
is not linked into anything that will run on the T113 — the target's audio path
is I2S and DMA, and miniaudio never goes near it.

**PulseAudio note.** miniaudio prefers PulseAudio over ALSA when both are
present. That adds Pulse's own buffering on top of ours and is the most likely
source of underruns on a normal Linux desktop; `--audio alsa` bypasses it. This
is untested here (no sound server in this container).

---

## 3. The real-time factor is not the gate, and will be read as if it were

This build prints a live real-time factor because it has a real clock and it
would be perverse not to. It is labelled on every line it appears on:

```
  12.3s  rtf 0.081 (desktop, not the A7 gate)  ring 2  under 0  ...
```

and the end-of-run summary spends six lines saying what it is not. That
verbosity is deliberate. `docs/PLAN.md` section 0 makes the whole project
conditional on an RTF of 0.5-0.6 **on one Cortex-A7 at 1.2 GHz**, and a number
printed next to the words "real-time factor" will be quoted as that number by
someone skimming. The two are not convertible:

- different ISA and different code generation;
- an x86 or Apple-silicon L2 that holds the 512 kB-1 MB PCM ROM, which the A7's
  will not — and `port/PORTING.md` 7 notes the ROM is addressed
  pseudo-randomly, so this flatters the desktop specifically;
- a desktop memory system rather than one DDR3 x16;
- and on Apple silicon, an entirely different microarchitecture again.

What the desktop number *is* good for: relative comparisons on one machine —
reverb on versus off, 32 partials versus 24, `--rate 32000` versus 48000,
`AnalogOutputMode` variants if someone adds the knob. Those ratios are worth
having and are cheap to measure here. The absolute number is worth nothing to
section 0, and `bench/rtf` on real silicon remains the only answer.

---

## 4. macOS is written but unverified

`src/desktop_midi_seq_core.c` (CoreMIDI) and the `__APPLE__` half of
`src/desktop_midi_tty.c` (`IOSSIOSPEED`) **have never been compiled or run.**
There was no Mac in this session and there is no way to cross-check them. The
CoreAudio path is miniaudio's, which is widely used, but our `CMakeLists.txt`
Apple branch — the framework list, `MA_NO_RUNTIME_LINKING` — is equally
unverified.

They are written carefully and guarded so they cannot break the Linux build
(proven: the Linux build is clean, and the `desktop_midi_seq_none.c` fallback
compiles too), but **do not report the macOS build as working until someone has
run it.** Expect to fix something on first contact. The likely candidates:

- `MIDIInputPortCreate` and `MIDIReadProc` are deprecated from macOS 11 in
  favour of `MIDIInputPortCreateWithProtocol`. They still work and still deliver
  MIDI 1.0 bytes, which is what an MT-32 wants, but a strict build may warn.
- `<IOKit/serial/ioss.h>` is header-only for our purposes (we use the
  `IOSSIOSPEED` ioctl constant), so no IOKit framework link should be needed. If
  the linker disagrees, add `-framework IOKit`.
- miniaudio's advice is `-framework AudioUnit` instead of `AudioToolbox` on
  older systems.
- CoreMIDI may need the process to have a run loop for *notifications*; we pass
  a NULL notify proc and only use the read proc, which is delivered on
  CoreMIDI's own thread, so this should be fine. Should.

### 4.1 A line-by-line reading, 2026-09-18 — still not a compile

This pass re-read every macOS path against Apple's documented interfaces and
against what the Linux side actually does. **Nothing below has been compiled.**
It is a list of what to expect, ordered by how likely it is to bite, so that
whoever gets to a Mac has a bring-up list rather than a surprise. Items marked
**will** are things this reading is confident about; items marked *may* are
things it is not.

1. **`./test.sh` case 8 will fail on macOS, and it is the test that is wrong,
   not the code.** The case drives the serial path through a pty. On Linux,
   `TCSETS2` + `BOTHER` sets 31250 on a pty and reads it back. On macOS the
   same job is done by the `IOSSIOSPEED` ioctl, and **a pty is not a serial
   driver**: `ioctl(fd, IOSSIOSPEED, &sp)` should fail with `ENOTTY`, our
   `set_raw_baud()` returns −1, and `dtty_open()` refuses the port. The fix is
   in the test, not in `desktop_midi_tty.c`: on `__APPLE__` the pty case should
   either be skipped or should run without a baud request. Until someone runs
   it, the tty path on macOS is untested in a way the Linux path is not.
2. **The macOS baud path does not read the rate back, and the Linux one does.**
   `set_raw_baud()` on Linux re-reads `termios2.c_ospeed` and warns
   `"not 31250"` if the driver rounded — which is the warning README.md
   promises for cheap CH340 clones. The `IOSSIOSPEED` branch only checks the
   ioctl's return value. `IOSSIOSPEED` is documented to fail if the driver
   cannot do the rate, so this is probably adequate, but the two platforms do
   not give the user the same quality of answer and the README implies they do.
3. **Deprecation warnings will appear, and are not errors.**
   `MIDIClientCreate`, `MIDIDestinationCreate`, `MIDIInputPortCreate` and the
   `MIDIReadProc` signature are all deprecated from macOS 11 in favour of the
   `…WithProtocol` family. We build without `-Werror`, so this is noise — but it
   is noise on every build, and `-Wno-deprecated-declarations` on that one file
   would be worth adding when someone can test the result.
4. **`dseq_list()` may fail where `--midi-seq` succeeds, or the reverse.** It
   creates its own `MIDIClientRef` purely to enumerate, which is not required —
   `MIDIGetNumberOfSources()` and `MIDIGetSource()` work without a client. On a
   machine where the MIDIServer cannot start (an SSH session with no window
   server is the usual case) `MIDIClientCreate` returns `kMIDIServerStartErr`
   (−10900) and `--list-midi` prints "CoreMIDI unavailable" while the rest of
   the program would have been fine. Dropping the client from `dseq_list()` is
   a three-line change and would make the two paths agree.
5. **Nothing notices a keyboard plugged in after start-up.** We pass a NULL
   `MIDINotifyProc`, so `kMIDIMsgObjectAdded` never arrives and a source that
   appears later is never connected. This is a real functional gap, and the
   ALSA side has the same one, so it is a seam-level decision rather than a
   macOS bug: either both sides poll for new sources or neither does.
6. **`#define _POSIX_C_SOURCE 200809L` in `src/desktop_time_posix.c` may hide
   what it needs.** On macOS that macro selects a strict POSIX view of the
   headers; `clock_gettime`, `CLOCK_MONOTONIC` and `nanosleep` are all POSIX
   and should survive it, but Darwin's headers gate a good deal on
   `__DARWIN_C_LEVEL` and this is the kind of line that produces an
   "implicit declaration of clock_gettime" on exactly one platform. If it does,
   `_DARWIN_C_SOURCE` is the answer.
7. **The framework list looks right.** miniaudio's own documentation
   (`miniaudio.h`, § 2.2) says `MA_NO_RUNTIME_LINKING` requires
   `-framework CoreFoundation -framework CoreAudio -framework AudioToolbox`,
   and that is exactly what `CMakeLists.txt`'s `APPLE` branch links, plus
   `CoreMIDI` for ours. `vendor/miniaudio/miniaudio_impl.c` really does define
   `MA_NO_RUNTIME_LINKING` under `__APPLE__`, so the comment in `CMakeLists.txt`
   is accurate — checked by reading both files, which is the one class of
   claim reading *can* settle. miniaudio's note that older systems may need
   `-framework AudioUnit` instead of `AudioToolbox` stands.
8. **The new files in this pass are POSIX-only C and should be portable.**
   `desktop_config.c` uses `snprintf`, `memcpy` and `strcmp`;
   `desktop_midi_raw.c` uses `fopen`/`fread`. Neither touches anything
   platform-specific. `conform.sh` was made portable in one place —
   `sha256sum` does not exist on macOS, so it falls back to `shasum -a 256` —
   but it has not been run there, and `conform.sh`'s bare-metal leg needs
   `arm-linux-gnueabihf-gcc`, which on a Mac means Homebrew or nothing.

**None of this is evidence that the macOS build works.** It is evidence that
someone read it. The honest summary is unchanged from the first pass: the macOS
half of this directory has never been compiled, and the first person to build it
should expect a bring-up, starting with item 1 above, which is a test that will
fail on a correct program.

---

## 5. What this build found out about the desktop, that the target does not have

**Device wake-up granularity is the binding constraint on a desktop, not CPU.**
With the target's own numbers — 128-frame blocks, a 3-block ring, 8 ms of audio
in flight — the null backend in this container produced **203 underruns in 2
seconds** while the render loop used 0.05% of one core. The reason is
measurable and now reported: the device went up to **10.3 ms between requests**
against a nominal 2.67 ms period. A ring holding 8 ms cannot survive a 10 ms
gap, however fast the synth is. `--ring 8` gives 21 ms and the underruns go to
zero.

This is not a defect in `port/DESIGN.md` 2.2's numbers. On the T113 the DMA
completion interrupt arrives every 2.67 ms because the I2S clock says so, and
nothing schedules it away. It *is* a thing every reader of this program's output
needs to know, so the summary now detects the case and says so in full sentences
rather than leaving the reader to conclude the design is broken. Two other
desktop-only consequences:

- The MIDI FIFO here is 64 Ki stamped bytes, not the target's 256
  (`port/DESIGN.md` 3.3). A UART physically cannot deliver faster than 3125
  bytes/s; a redirected file delivers 40 kB in one `read()`. With the target's
  size, a replayed capture overruns the FIFO and the parser reports a *parse*
  error, which blames the wrong component. The overrun counter is still printed
  and still means what it means.
- Underruns are not counted before the first `commit()`. The device starts
  inside `mtp_audio_open()`, microseconds before the render loop exists, and the
  silence it plays until then is the same silence the target's ring holds at
  start-up (`mtp_audio.h`: "opening early is safe"). Counting it put two or
  three phantom underruns on every run, which is exactly how a reader learns to
  ignore the number that matters most.

### 5.0 Several MIDI sources share one FIFO, which is a byte merge

`mtp_midi.h` is one stamped-byte FIFO because the target has one UART, and that
is the right shape for the target. The desktop can have four sources at once,
and they are merged at byte granularity: if two are transmitting simultaneously,
one can interleave into the middle of the other's message. Verified working with
an SMF and a stdin stream together (105 bytes, 17 short + 2 sysex, zero parse
errors) because neither was continuously busy. Two busy live sources would
corrupt each other, and the fix is not in this layer — it is `aconnect`, or a
DAW, or a real MIDI merger. Documented in README.md rather than papered over.

### 5.1 One thing to check before committing: the root `.gitignore`

The repository's root `.gitignore` has `**/vendor/`, which is right for
`bench/vendor` and `boot/vendor` (upstream clones, read and never committed) and
wrong for `desktop/vendor`, whose whole purpose is to be committed so that this
builds on a clean machine. `desktop/.gitignore` negates it:

```
!vendor/
!vendor/**
```

**Resolved, 2026-09-18.** Checked with a read-only git command, which the
first pass could not run:

```
$ git ls-files desktop/vendor
desktop/vendor/miniaudio/LICENSE
desktop/vendor/miniaudio/miniaudio.h
desktop/vendor/miniaudio/miniaudio_impl.c
```

All three are tracked, so the nested negation took and a clean checkout builds
with no package manager involved. The root `.gitignore` now reads
`bench/vendor/` and `boot/vendor/` rather than `**/vendor/` — someone narrowed
it — so the negation in `desktop/.gitignore` is belt and braces rather than
load-bearing. It is worth keeping either way: it is one line, and it documents
why this vendor directory is different from the other two.


---

## 6. Things in `port/` that want changing

Nothing in `port/` was modified. These are for whoever owns it.

### 6.1 `mtp_render_stats.min_queued` is polluted by start-up and reports 1 for ever

`port/src/mtp_render.c` records `min_queued` after every commit, including the
first few while the ring is still filling from empty. The first commit always
leaves occupancy 1, so **`min_queued` is 1 on every run that ever starts**, and
the "real safety margin" `port/DESIGN.md` 2.4 promises is not observable. Runs
in this session show `min ring occupancy 1 of 8` while the live readout shows a
steady occupancy of 4 to 5 and zero underruns — the metric and the reality
disagree by a factor of four.

Fix: ignore commits until occupancy has first reached `target_queued`, or keep a
separate `min_queued_steady`. It is a two-line change and it restores the one
number that is supposed to say "how close did we come".

### 6.2 A refused real-time byte is dropped silently

In `on_message()`, `MTP_MSG_REALTIME` calls `ctx->engine->short_msg(...)` and
ignores the result, with a comment explaining that real-time bytes should not
occupy a queue slot on failure. The intent is right, but nothing counts the
drop, so a synth that is losing MIDI Clock under load looks healthy. One
counter, `stat_realtime_dropped`, would close it.

### 6.3 `DESIGN.md` 4.3 asks for things `port/include` cannot express

The ROM-failure behaviour in 4.3 is good, and two parts of it are not
implementable behind the current seam:

- *"list what **is** in `/roms`"* — `mtp_storage.h` has no directory
  enumeration, deliberately (its header says so). Either 4.3 softens, or the
  seam gains one `readdir`-shaped call.
- *half-image ROM pairs* — `ROMImage::makeROMImage(File*, File*)` merges a
  `FirstHalf`/`SecondHalf` pair, and 4.3 says to support that, but
  `mtp_engine_config` carries exactly one `control_rom_path` and one
  `pcm_rom_path`. Supporting pairs needs two more fields or an array.

Neither is urgent. Both are the sort of thing that is much cheaper to decide now
than after a second implementation exists.

### 6.4 The engine seam has no volume and no panic

`mtp_engine.h` exposes open/close/timebase/rendered/short/sysex/render. A
desktop wants a master volume (`mt32emu` has `Synth::setOutputGain`), and
anything that can be stopped wants an all-notes-off on the way out. This
implementation works around the second by nothing at all: Ctrl-C stops the
device mid-note. On a hardware module with a power switch that is fine; here it
is a small wart, and on any future build with a display and an encoder it will
not be.

### 6.5 `mtp_midi_frame_errors()` cannot mean anything on a POSIX tty

The header describes it as UART framing/parity errors, which is right for the
target. A POSIX tty only reports them via `PARMRK`, which inserts marker bytes
into the stream and would corrupt MIDI data. This implementation returns 0 and
says so in a comment. Not a defect — worth a line in the header saying the
counter is target-only, so nobody later "fixes" a desktop implementation into
corrupting the byte stream.

### 6.6 `mtp_render_ctx` is 64 kB and must not go on a stack

Two `MTP_SYSEX_MAX` buffers inline. `port/host/main.c` makes it `static` with a
comment; so does this one. Given `port/PORTING.md` 4.4 measures `mt32emu`
putting 16-96 kB on a stack depending on configuration, a target task stack that
also had to hold this would be a bad day. Worth an explicit note in
`mtp_render.h` rather than in two `main.c`s.

### 6.7 `mtp_render_run()` is a test harness, not a main loop

It stops at `max_blocks`, and it *breaks out of the loop* if `mtp_audio_wait()`
times out once ("audio sink stalled"). On a desktop a device can legitimately
stall — suspend, a USB interface unplugged, a sample-rate change — and silently
ending the run is the wrong response. `port/DESIGN.md` 6.4 already says the
target uses `mtp_render_pump()` inside its own superloop, and this build does
the same, so nothing is broken. It might be worth saying in `mtp_render.h` that
`mtp_render_run` is for harnesses, since its name suggests otherwise.

### 6.8 `port/host/main.c` cannot be told about a third engine

It maps `--engine mt32emu` to the symbol `mtp_engine_mt32emu` and has no other
hook. `emu/` has a third engine — the real library on fabricated ROMs — and it
is the only one that can exercise the synthesiser where there are no ROMs,
which is everywhere. To drive it through an *unmodified* `port/host`,
`desktop/conform.sh` compiles `emu/src/engine_mt32emu_fake_roms.cpp` with
`-Dmtp_engine_mt32emu_fakerom=mtp_engine_mt32emu`, which renames the vtable
symbol at the preprocessor. It works, the run still prints the engine's real
name (`engine  mt32emu-fakerom`) because that is a string inside the vtable,
and it is a trick rather than a design.

What would replace it: a registry. `mtp_engine.h` already declares the vtables
it knows; a `const mtp_engine_vtable *mtp_engine_by_name(const char *)` in
`port/src`, and a link-time list, would let any harness offer any engine the
build happens to contain without three `main.c`s each growing an `#ifdef`. All
three implementations currently duplicate that `if`/`else` chain, and the
bare-metal one has already drifted: it knows `mt32emu-fakerom` and the other
two do not.

### 6.9 The float ABI needs one more flag, and it is not a taste question

See section 8.2. `port/PORTING.md` § 4.2 fixes the ARM flags — Cortex-A7,
NEON-VFPv4, hard float, no exceptions, no RTTI, no thread-safe statics — and
that list is short one entry: **`-ffp-contract=off`**. Without it the same
mt32emu source renders 1 LSB differently on armv7 than on x86-64, measurably,
on 0.07% of samples, because GCC fuses `Analog.cpp:392`'s multiply-accumulate
into `VFMA`. Nobody hears it; it costs the project the ability to compare the
board's output against the bench's by checksum, which is a test worth more than
the last bit is.

---

## 7. What would make this build worth more

In rough order of value per hour:

0. **Add one entry to `emu/tools/gen_vectors.py`'s `STREAMS` list**, so the
   bare-metal image can play the `voice` vector and the four-way conformance
   run becomes five-way on non-silent synthesiser output. It is the cheapest
   remaining item in this document and it closes the one gap that stops
   section 8.2's conclusion applying to the T113 directly.
   `desktop/conform/vectors.py`'s `voice()` is written to be copied over
   unchanged; it needs `roland_sysex()`, which that file already has.
1. **Run it on a Mac.** Half of section 4 evaporates, and the CoreMIDI path
   either works or is fixed in an afternoon.
2. **Run it on a Linux machine with a sound card and a MIDI keyboard**, with
   ROMs. That is the first time anyone will have heard this project, and it is
   also the first real test of whether 128 frames x 3 blocks is comfortable on a
   normal desktop or whether Pulse's buffering makes it hopeless.
3. **Measure round-trip latency for real**: a loopback cable, JACK, and a scope
   or an audio input. `port/DESIGN.md` 2.5 is arithmetic that has never met a
   measurement, and everything in it above the DAC is testable here.
4. **A/B the `lookahead` knob** (`port/DESIGN.md` 2.6). It exists precisely
   because the answer is a listening test, and this is the build you can listen
   on.
5. **Relative RTF measurements** — reverb on/off, partial limit, 32 kHz versus
   48 kHz. Not the gate, but real information about where the cycles go, and
   cheap.

---

## 8. Cross-implementation conformance: four builds, one stream, one answer

`docs/PLAN.md` § 0.5 rule 2 says every implementation of the seam passes the
same conformance tests. `docs/HISTORY.md` § 5 records one instance of it: a
one-second render that came out byte-identical on x86-64 and on bare-metal ARM.
`./desktop/conform.sh` is that, made standing and made wider.

```sh
./desktop/conform.sh          # about four minutes from cold, one from warm
```

It builds and runs the same MIDI through everything that exists:

| tag | what it is |
|---|---|
| `host-x86` | `port/host`, compiled here for x86-64 |
| `host-armv7` | `port/host`, cross-built `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`, run under `qemu-arm` |
| `armv7-fpoff` | the same, plus `-ffp-contract=off`. It exists because of what § 8.2 found |
| `emu-a7` | `emu/`'s bare-metal image, from reset, under `qemu-system-arm -M virt -cpu cortex-a7` |
| `desktop` | this directory, against miniaudio's null device |

on four MIDI streams (`demo`, `bank`, `bad` — the bytes `port/host/test.sh`
already asserts against — plus `voice`, which is § 8.3) and two engines
(`port/host/engine_fake.c`, and the real mt32emu on `emu/`'s fabricated ROMs).
It compares the rendered PCM sample for sample and every counter in the
contract, and it prints where any difference is, how large, and over how many
samples.

Three things make the comparison mean something rather than merely happen:

1. **The MIDI bytes are provably the same bytes.** `conform/vectors.py
   --verify` parses `emu/src/midi_vectors.c` and compares its `demo`, `bank`
   and `bad` arrays against the ones it generates. The run prints
   `VERIFY demo identical to the bare-metal image's copy (49 bytes)` and
   refuses to include the bare-metal leg if they ever differ.
2. **The MIDI arrives at the same sample position everywhere.** `--midi-fifo`
   cannot promise that — it is read by the poll thread, so an event can land one
   pump call and therefore 128 frames later. `--midi-raw` (new) is pulled on the
   render thread with exactly the arithmetic of `port/host/host_midi_file.c` and
   `emu/src/emu_midi.c` in their unpaced mode. `test.sh` asserts two runs of it
   produce identical samples.
3. **Nothing anyone else owns is written to.** `port/host`'s sources are
   compiled into `desktop/build/conform`, never into `port/host/build`, so a
   concurrent worker's tree is untouched; the bare-metal ELF is copied to
   `desktop/build/conform/emu-snapshot.elf` and its SHA-256 printed, because
   `emu/` is being worked on and the image can change underneath a run.

### 8.1 The result

Run of 2026-09-18, `SECONDS_PER_RUN=1`, 48 kHz stereo, 128-frame blocks:

| engine | vector | implementations | PCM |
|---|---|---|---|
| fake | demo | 5 | **byte-identical**, peak 23409, 13624 non-zero samples |
| fake | bank | 5 | **byte-identical**, peak 32767 |
| fake | bad | 5 | **byte-identical**, peak 6256 |
| fake | voice | 4 | **byte-identical**, peak 6436 |
| mt32emu-fakerom | demo | 5 | identical, **but every sample is zero** — § 8.3 |
| mt32emu-fakerom | bank | 5 | identical, all zero |
| mt32emu-fakerom | bad | 5 | identical, all zero |
| mt32emu-fakerom | voice | 4 | **`host-armv7` differs** — § 8.2 |

Every invariant counter agreed on every run: bytes in, short messages, sysex
messages, real-time messages, orphan data bytes, oversize sysexes refused,
aborted sysexes, engine back-pressure. For the `bad` stream that is
`40116, 2, 0, 0, 3, 1, 1, 0` on all five, which is the same row
`port/host/test.sh` case 3 asserts.

The counters that legitimately differ are printed and never asserted on. They
are worth reading anyway: on the `demo` + `fake` run, `worst render` was 22 µs
on x86-64, 1007 µs for the same code under `qemu-arm`, and 1103 µs under
`qemu-system-arm` — which is a statement about TCG, not about a Cortex-A7, and
is exactly why `docs/PLAN.md` § 0.5 says no timing evidence comes out of
emulation.

The `fake` engine is worth more here than it looks. It calls `pow()` and
`sinf()` per voice per sample, so a byte-identical result across glibc on
x86-64, glibc on armv7 and the bare-metal image's `libm.a` is a real statement
about libm, not just about the ring.

One unplanned robustness check fell out of running this while `emu/` was being
worked on: two runs an hour apart used bare-metal images with different
SHA-256s — `1104a3da…` and `0be51b35…`, that workstream having rebuilt in
between — and produced identical PCM and identical counters on all six of its
runs. The snapshot-and-print-the-hash step exists precisely so that this can be
said rather than assumed.

### 8.2 The divergence, and its cause

```
### engine mt32emu-fakerom  vector voice     (4 implementations)
  PCM: 96000 samples compared (1.000 s), peak 9300, 95990 non-zero
       armv7-fpoff   96256 samples  sha256 154d0bf6b5278d63335c5cacaa6a25c6
       desktop       96000 samples  sha256 154d0bf6b5278d63335c5cacaa6a25c6
       host-armv7    96256 samples  sha256 06c775266e698b857c8fe30efea2fe70
       host-x86      96256 samples  sha256 154d0bf6b5278d63335c5cacaa6a25c6
  PCM DIVERGENCE  armv7-fpoff vs host-armv7: 69 of 96256 samples differ
  (0.0717%), first at sample 5204 (6712 vs 6711), largest difference 1 LSB
```

**Cause, bisected to one translation unit and confirmed by removing it.**
GCC's default is `-ffp-contract=fast`, which on armv7 with `-mfpu=neon-vfpv4`
fuses a float multiply-accumulate into `VFMA` — one rounding where x86-64's
baseline SSE2 does two. The bisection:

```
$ arm-linux-gnueabihf-objdump -d *.o | grep -c vfma     # per object
Analog.o: 4      BReverbModel.o: 15
LA32FloatWaveGenerator.o: 6      Tables.o: 3

$ # rebuild ONE file with -ffp-contract=off, keep the rest, re-render, diff vs x86
Analog rebuilt with -ffp-contract=off:                 0 samples differ from x86
BReverbModel rebuilt with -ffp-contract=off:          69 samples differ from x86
Tables rebuilt with -ffp-contract=off:                69 samples differ from x86
LA32FloatWaveGenerator rebuilt with -ffp-contract=off: 69 samples differ from x86
```

One file: `mt32emu/src/Analog.cpp`. One line: **`Analog.cpp:392`**,
`sample += LPF_TAPS[tapIx] * ringBuffer[...]`, the multiply-accumulate inside
`AccurateLowPassFilter::process()` — the polyphase FIR that both models the
MT-32's analogue low-pass filter and resamples 32 kHz to 48 kHz. It is on the
output path of every sample whenever `AnalogOutputMode_ACCURATE` is selected,
which is what both `port/host/engine_mt32emu.cpp` and `emu/`'s fake-ROM engine
select. Rebuilding the whole library with `-ffp-contract=off` makes armv7
byte-identical to x86-64: `x86 vs armv7 -ffp-contract=off : 0 of 96256`.

**What it is worth.** The difference is at most 1 LSB of a 16-bit sample, on
0.07% of samples, and Munt's own comment on those coefficients says the filter
is "nearly bit-accurate for standard 16-bit sample resolution" — so this is
inside the filter's own design error and **nobody will hear it**. What it is
not is nothing:

- It is the difference between "the box sounds the same as the bench" and "the
  box sounds the same as the bench to within a rounding mode". For a product
  whose whole claim is fidelity to a Roland box, which of those two sentences
  is true should be a decision, not an accident of the default flags.
- **The bare-metal image has it too.** `emu/build/mt32emu/Analog.o` contains
  4 `VFMA` instructions, built with the same `-O2` and the same
  `-mfpu=neon-vfpv4`, so the T113 will render the same 1 LSB differences —
  the conformance run cannot show it only because the bare-metal image can play
  only the streams compiled into it, and all of those are silent under the
  fabricated ROMs (§ 8.3, § 8.4).
- It makes regression testing by checksum impossible across hosts unless the
  flag is fixed. That matters more than the audio does: "the ARM build renders
  the same bytes as the x86 build" is a cheap, strong test that is available
  only if the answer is yes.

**Recommendation for `port/PORTING.md` § 4.2 (not this workstream's file):**
add `-ffp-contract=off` to the ARM flag set alongside `-fno-exceptions`,
`-fno-rtti` and `-fno-threadsafe-statics`, and say why. The cost is one fused
instruction per FIR tap on a path that is already 60 taps of load-multiply-add;
the benefit is that the module and the bench are bit-comparable. If instead the
project decides the fused form is preferable — it is arguably the more accurate
of the two — then the *x86* builds should be the ones that change, and the
decision should be written down either way. `bench/` should apply whichever
choice is made, or its RTF numbers will be measured on a different binary from
the one that ships.

### 8.3 What the fake ROMs cannot tell you: silence compares equal

`emu/src/engine_mt32emu_fake_roms.cpp` opens a real `Synth` on fabricated ROM
images. Every line of mt32emu runs. But the PCM ROM is all zeroes and the
control ROM is almost all zeroes, and the machine state the synth powers up
into is therefore **master volume 0, every part's output level 0, no partials
reserved to any part, and every timbre a zero timbre**. So `demo`, `bank` and
`bad` render 48000 frames of digital silence, and comparing four
implementations' silence proves only that nobody added noise.

That is the finding, and `conform/vectors.py`'s `voice` vector is the fix. It
writes a playable machine over sysex before playing one note:

| sysex | address | what it fixes |
|---|---|---|
| System area, all 23 bytes | `0x100000` | master volume 100, **partial reserve 32 to part 1** (zero here is what silences everything even at full volume), channel assignment 1..9 |
| Patch Temporary, part 1 | `0x030000` | output level 100, key shift 0, pan centre |
| Timbre Temporary, part 1 | `0x040000` | one square-wave partial, TVA and TVF envelopes wide open — 246 bytes, the `TimbreParam` of `Structures.h` |

`0x90 60 100` then renders with **peak 9300 of 32767 and 95990 of 96000 samples
non-zero**, through the LA32 wave generator, the TVA and TVF envelopes, the
partial manager and the analogue output filter. That is what made § 8.2
visible; on silence, every implementation agreed.

`test.sh` asserts both halves of this, so the distinction stays on the record:
`fakerom rendered audible output (peak 9300)` and `fakerom demo is silent (0)`,
with the summary explaining that a silent run through a working audio path is
the engine and not the plumbing.

### 8.4 What the conformance run still does not cover

- **The bare-metal leg cannot play `voice`.** `emu/`'s MIDI source is a byte
  array in `.rodata` generated into `emu/src/midi_vectors.c`, and that file is
  not this workstream's to add to. So the one stream that makes mt32emu audible
  is compared on four implementations and not five, and **the bare-metal ARM
  build has never been compared against x86-64 on non-silent synthesiser
  output**. One entry in `emu/tools/gen_vectors.py`'s `STREAMS` list would close
  that; `conform/vectors.py`'s `voice()` is written to be copied over
  unchanged. Until then, § 8.2's conclusion about the T113 rests on the
  `VFMA` instruction count in `emu/build/mt32emu/Analog.o`, which is evidence
  about the binary and not about the audio.
- **No real ROMs.** Everything above is the emulator running correctly on
  meaningless data. Two implementations agreeing on the wrong sound is still
  worth knowing, but it is not the sound.
- **QEMU is not a Cortex-A7.** Neither `qemu-arm` nor `qemu-system-arm` models
  the pipeline or the caches. The comparison is of *values*, and values are
  exactly what TCG does get right; the timings in the same table are not
  evidence of anything.
- **One second, one configuration.** 48 kHz, 128-frame blocks, `lookahead 0`,
  reverb on, 32 partials. `SECONDS_PER_RUN` is an environment variable; the
  rest would need the matrix widened.
- **The desktop leg runs with `--ring 8`**, not the target's 3, because of
  section 5. Ring depth changes how many blocks a pump call produces and
  cannot change what is in them — which the run confirms, since `desktop` and
  `host-x86` agree sample for sample with different ring depths.

---

## 9. `mt32.cfg`: the format the SD card will carry

`docs/PLAN.md` § 1 says the microSD holds "ROMs, SoundFonts, config" and
nothing had said what the config looks like. A format nobody has parsed is a
format nobody has found the mistakes in, so this build reads one now — on a
laptop, where a mistake costs a rebuild rather than an SD card and a serial
console. `desktop/mt32.cfg.example` is the documented file;
`src/desktop_config.h` is the specification.

```
# comment; ';' works too. A comment must start the line, because paths
# contain '#'.
machine     = mt32          # mt32 | cm32l
rom_dir     = roms
engine      = auto          # auto | fake | mt32emu | mt32emu-fakerom
rate        = 48000
block       = 128
ring        = 3
partials    = 32
reverb      = on            # on/off/yes/no/true/false/1/0
midi_baud   = 31250
log         = info
desktop_audio = auto        # keys the module ignores carry this prefix
```

The rules, and the reason for each, are in `src/desktop_config.h`. The ones
that matter for the target:

- **`cfg_scan()` allocates nothing**, uses no float, no `long long`, no
  `sscanf`, no `strtol` and no `errno`, and makes one forward pass over a
  buffer the caller owns. It is written to be lifted into `port/src` unchanged
  when the seam owner wants it; `desktop_config_load()` reads the file with
  `mtp_storage_load()` into a 4 kB **static** buffer, which is the call the
  T113 will make through FatFs.
- **An unknown key is a warning, not a failure.** An old firmware has to be
  able to read a card written by a new one, and the other way round. The
  warning names the line.
- **Flat, no sections.** Four lines of parser, and the difference between
  debugging a config file over a 115200-baud console and not.
- **A value is literal to end of line**: no quotes, no escapes, so a path with
  a space in it needs nothing special.
- A UTF-8 BOM is skipped and CR is treated as blank, because the file will be
  edited on Windows.
- **The command line beats the file**, which is the order a headless box needs:
  the card is the standing configuration and the flags are what you are trying
  right now. `--config` is found in a pre-pass, because it has to be honoured
  before the settings it changes are read.

Measured here (`test.sh` section 10): a nine-line file with a comment, a blank
line, six settings, one unknown key and one line with no `=` produces
`config t.cfg: 9 lines, 6 settings, 1 unknown, 1 malformed`, applies all six,
warns twice by line number, and is overridden by `--block 512`.

**Deliberately not in the format yet:** `volume`. `mtp_engine.h` has no gain
call (section 6.4), so a `volume` key would be a key that silently does
nothing, which is worse than no key. It should go in at the same time as the
seam call.
