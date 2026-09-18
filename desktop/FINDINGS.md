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

---

## 10. The A/B rig: how do we compare with Munt?

The question asked was "hook this up to an emulator like an X68000 so we can
see how we sound against other implementations", with a follow-up that is the
better half of it: "see if any emulators can hook directly into a MIDI device
to make a cleaner test." Two answers follow, and the second is the one that
outlives the session.

**Nothing here has been heard.** There are no MT-32 ROMs in this repository and
there never will be, `mt32emu` verifies them by SHA-1, and this container has no
sound device: `/dev/snd` does not exist, `/proc/asound/seq/clients` does not
exist, and `aconnect`, `aseqdump`, `arecordmidi`, `dosbox` and `mame` are not
installed. So no statement below is about how anything sounds, and none of the
emulator settings in README.md § "Driving this from an emulator" was executed —
they are readings of those emulators' source, with line numbers.

### 10.1 Why a live hook-up is the wrong test, measured

The intuition behind "a cleaner test" is right, and it can be shown rather than
argued. The same binary, the same SMF, the same flags, three consecutive runs
through the null device:

```sh
for i in 1 2 3; do ./desktop/build/mt32-desktop --audio null \
    --engine mt32emu-fakerom --midi-smf timed.mid --rate 48000 --ring 8 \
    --status-ms 0 --seconds 4.400 --tap-wav rep/run$i.wav; done
python3 desktop/ab/abdiff.py rep --ref run1
```

```
  run1    211200    48000     18600   -4.92  -14.88     0.5174  3c3b237ba1c9f62a
  run2    211200    48000     18573   -4.93  -14.86     0.5174  66ef9a480535612a
  run3    211200    48000     18600   -4.92  -14.88     0.5174  3c3b237ba1c9f62a

-- run1  vs  run2   (422400 samples in common, 4.400 s)
   71476 of 422400 samples differ (16.9214%), first at sample 116736 ...
   onset: run2 starts 0 samples (0.00 ms) at the same sample
   difference RMS 3905.4 LSB ( -18.48 dBFS); reference RMS 5908.0 LSB
-- run1  vs  run3
   IDENTICAL: every one of 422400 samples
```

Two of three runs were bit-identical and the third was not. The first sample
that differs is 116736, which is frame 58368, which is 1.216 s — the second
note-on in that file is at 1.200 s. Nothing about the synthesis changed; the
event landed in a different 128-frame block because the MIDI source is paced by
`mtp_time_us64()` (`src/desktop_midi_smf.c:315-316`) and the render loop is
paced by the audio device. **A real-time rendering of a game's MIDI is not
reproducible to the sample, so two real-time renderings cannot be subtracted.**
Capture the stream once and render it offline; that is what `ab.sh` does.

The same effect, measured against a sample-exact renderer of the identical
events, is a **17.33 ms** head start for the offline leg:

```
  ours        211200    48000     18600   -4.92  -14.88     0.5174
  ref-seam    211200    48000     18570   -4.93  -14.86     0.5001
   onset: ref-seam starts 1664 samples (17.33 ms) earlier
```

17.33 ms is 832 frames, and the ring in that run was 8 blocks of 128 frames =
1024 frames = 21.3 ms. An event that arrives at wall-clock *t* is put into
audio that will not be *played* until up to a ring later, so the tap records it
late by most of a ring. That is the port behaving as designed, not a defect —
but it is a reason the tap and a file renderer can never be compared without
saying so.

### 10.2 What was built

Four files, all under `desktop/`:

| | |
|---|---|
| `ab.sh` | the driver: prepare, build, render every leg, compare, and say what the run did not show |
| `ab/midiprep.py` | one MIDI input (SMF or raw bytes) → `.evt` for our reference, a normalised `.mid` for Munt's, `.raw` for `--midi-raw`; all carrying the same events on a 10 µs grid |
| `ab/ab_ref.cpp` | the reference front end: events straight into the engine, sample-exact, with none of `mtp_midi_parser.c`, `mtp_render.c`, the ring or `desktop/src` |
| `ab/abdiff.py` | the comparison: imports `conform/compare.py`'s `pcm()` and `compare_pair()` rather than reimplementing them, and adds level, difference RMS, onset and a lag search |

`ab.sh` writes only inside `desktop/build/ab`, including the out-of-source
CMake build of Munt, so the rule `conform.sh` keeps — nothing outside
`desktop/` is written to — still holds.

**`bench/vendor/munt` does ship a reference renderer**, which was the thing
worth checking before writing one: `mt32emu_smf2wav`, "a console application
intended to facilitate conversion a pre-recorded Standard MIDI file (SMF) to a
WAVE file using the mt32emu library" (`bench/vendor/munt/README.md`). It builds
here:

```
$ cmake -S bench/vendor/munt -B desktop/build/ab/munt -DCMAKE_BUILD_TYPE=Release \
        -Dmunt_WITH_MT32EMU_QT=FALSE -Dmunt_WITH_MT32EMU_SMF2WAV=TRUE
-- Found GLIB2: /usr/lib/x86_64-linux-gnu/libglib-2.0.so (found suitable version "2.80.0",
   minimum required is "2.32")
$ cmake --build desktop/build/ab/munt -j8
[100%] Built target mt32emu-smf2wav
$ ./desktop/build/ab/munt/mt32emu_smf2wav/mt32emu-smf2wav -m /nonexistent -i mt32 -f \
      -o x.wav -a 2 -x 32 --record-max-start-silence -1 --record-max-end-silence -1 \
      --record-max-la32-end-silence -1 -e 96000 stream.mid
Error reading contents of ROM dir.
Munt MT32Emu MIDI to Wave Conversion Utility. Version 1.9.3
Using Munt MT32Emu Library Version 2.8.3, libsmf Version 1.3 (with modifications)
```

That is the whole flag set `ab.sh` uses, and it parses: the only complaint is
the ROM directory. **It has never been run to completion, here or anywhere in
this project, because that needs ROMs.** `-a 2` is `AnalogOutputMode_ACCURATE`,
matching what both of our engines open the synth with
(`port/host/engine_mt32emu.cpp:133`); the three `--record-max-*-silence -1` are
not decoration — the default is 0, which *trims* leading silence and would
shift the whole file against ours.

Because smf2wav cannot exist without ROMs, the no-ROM ladder has one more rung:
`ab_ref`, which drives the same engine seam with none of our code above it. It
is weaker evidence than smf2wav and `ab.sh` says so in its own header. When
ROMs exist, believe smf2wav.

### 10.3 What the rig found, with no ROMs

```
$ ./desktop/ab.sh
  leg         frames     rate      peak  peak dB    rms dB    onset s  sha256/16
  ours         96000    48000      9300  -10.94  -12.58     0.0001  7b48a146ce2cf1c1
  ref-seam     96000    48000      9300  -10.94  -12.58     0.0001  7b48a146ce2cf1c1

-- ours  vs  ref-seam   (192000 samples in common, 2.000 s)
   IDENTICAL: every one of 192000 samples
RESULT: every leg rendered the same samples as ours.
```

On the `voice` vector delivered at t=0, **our whole pipeline and a front end
that shares none of it produce the same 192 000 samples**. That is worth having:
`conform.sh` compares four builds of the *same* code against each other, and
could not have caught a mistake that all four share. This compares that code
against code that does not contain it — the parser, the ring, the block
scheduler and the timestamp arithmetic are all in one leg and none of the
other, and the audio is identical anyway.

It is also, on its own, nearly the weakest possible statement about the
product: both legs ran the fabricated ROMs of
`emu/src/engine_mt32emu_fake_roms.cpp`, a control ROM that is mostly zeroes and
a PCM ROM that is entirely zeroes. Every line of mt32emu ran; the sound is
meaningless. `ab.sh` prints that paragraph at the end of every ROM-less run so
that an output pasted into a chat window carries its own caveat.

### 10.4 Which emulators can hand MIDI to an external device

Read from each project's own source at `raw.githubusercontent.com`, on the
dates in this document. `[read]` = read the code, file and line given. `[search]`
= a search summary, not verified. Nothing in this table was executed.

| emulator | external MIDI out? | how | built-in Munt? | records MIDI? | evidence |
|---|---|---|---|---|---|
| **DOSBox Staging** | yes | `[midi] mididevice = port`, `midiconfig = <client:port>`; `alsa` deprecated → `port` | yes, `mididevice = mt32` | yes, `.mid`, Ctrl+Alt+F6 | `[read]` `src/midi/midi.cpp:867-946`, `src/capture/capture.cpp:656-660`, `src/capture/capture_midi.cpp:28-93` |
| **DOSBox-X** | yes | `[midi] mididevice = alsa`, `midiconfig = <client:port>` or `s` | yes (`src/gui/midi_mt32.h`) | yes, `.mid`, binding `caprawmidi`, **no default key** | `[read]` `src/gui/midi.cpp:613-614`, `src/gui/midi_alsa.h:100-121`, `src/hardware/hardware.cpp:2051-2058`, `:2300` |
| **ScummVM** | yes | `-e alsa` (`--music-driver`), `SCUMMVM_PORT=<client:port>`, **`--native-mt32`** | yes, `-e mt32` | not found | `[read]` `base/commandLine.cpp:139,177,786,924`, `backends/midi/alsa.cpp:346-348,448-456` |
| **MAME (x68000)** | yes | `-exp1 x68k_midi`, then the `midiout` image device takes a **host port name**; back end via `-midiprovider` | n/a | not checked | `[read]` `src/devices/bus/x68k/x68k_midi.cpp:20-27`, `src/devices/imagedev/midiout.cpp:60-67`, `src/mame/sharp/x68k.cpp:920`, `x68k.h:64`, `src/osd/modules/lib/osdobj_common.cpp:151` |
| **86Box** | yes | `midi_device = system_midi`; port index is `midi` under `[System MIDI]` | yes: `mt32`, `mt32_new`, `cm32l` | not checked | `[read]` `src/config.c:912-914`, `src/include/86box/midi.h:98-107`, `src/sound/midi_rtmidi.cpp:90,231-244`, `src/sound/midi_mt32.c:463-492` |
| **PCem** | yes, but **rawmidi only** | `snd_rawmidi_open(…, "hw:c,d,s")`; needs `snd-virmidi` to reach a sequencer client | not checked | not checked | `[read]` `src/midi_alsa.c:100-107` |
| **px68k** | **no** | `midiOutOpen` returns failure and `midiOutShortMsg` is a no-op in its own Win32 shim | n/a | no | `[read]` `win32api/fake.c:101-129`, `x68k/midi.c:318-326`, `x11/juliet.c` (`#if 0` throughout) |
| **XM6 / XM6 TypeG** | probably (Windows MME) | **not verified** | — | — | `[search]` only; sources not reachable from here |

Three things in that table are worth pulling out.

**The X68000 route is MAME, not px68k.** px68k emulates the CZ-6BM1 MIDI board
faithfully enough to have module-type resets for MT-32, CM-32L, CM-64 and
CM-300 in its source — which is itself good evidence that X68000 software
expected those modules — and then throws every byte away, because the Linux
port's replacement for the Win32 MME calls does nothing. MAME's `x68k_midi`
device, by contrast, is wired to `midiout`, which hands its argument to
`osd().create_midi_output()`: a host port.

**This README was wrong about DOSBox Staging.** It said `mididevice = alsa`.
Staging's current source marks `alsa` (and `auto`, `coremidi`, `oss`, `win32`)
deprecated and rewrites them to `port`, which is now the default
(`src/midi/midi.cpp:915-919`). It still works; it is no longer the spelling.

**Two emulators will write the capture for you**, which is the cleanest route
into `ab.sh`: DOSBox Staging on Ctrl+Alt+F6 and DOSBox-X under the same binding
name. Both write an SMF whose delta times are `PIC_Ticks` — milliseconds — so
the capture is quantised to 1 ms before anything of ours sees it. That is
coarser than `midiprep.py`'s 10 µs grid and there is nothing to be done about
it; it is also finer than the 17 ms our own real-time path adds.

### 10.5 What did not work, and what is still unknown

- **`api.github.com` is refused for every repository but this one** ("GitHub
  access to this repository is not enabled for this session"), so there was no
  way to list a directory. `raw.githubusercontent.com` serves files fine, so
  every path above was found by guessing filenames and checking the HTTP
  status. Several guesses were wrong before the right one: DOSBox-X's MIDI code
  is `src/gui/midi.cpp`, not `src/hardware/midi.cpp`; PCem's is
  `src/midi_alsa.c` on branch `main`.
- **`www.vitormach.dev` is blocked by the egress proxy** (`EGRESS_BLOCKED`).
  That was the one search result that looked like it documented XM6 TypeG's
  MIDI routing step by step, so the XM6 row stays `[search]`.
- **No ALSA anywhere**, so not one line of the `aconnect`/`arecordmidi`
  plumbing in README.md has been executed. It is written from the emulators'
  source and from the ALSA tools' documented behaviour, and should be treated
  as `[inferred]` until somebody runs it.
- **`numpy` is not installed**, which is why `abdiff.py` is pure Python and why
  its lag search is a coarse-to-fine search over a subsampled window rather
  than a correlation. On a held note that beats against another, the lag is
  genuinely ambiguous — the search reported 78 ms where the onsets say 17 ms —
  so the onset line is printed as well and the code says to believe it.
- **`ab_ref` is not the parser.** It takes framed events from `midiprep.py` and
  therefore has no sysex cap, no orphan-byte accounting and no truncation. A
  stream that exercises those limits — `conform/vectors.py`'s `bad`, whose
  40 000-byte sysex is larger than `MTP_SYSEX_MAX` (32768) — will be rendered
  differently by the two legs, and that difference is the parser working.
  `midiprep.py` prints a warning when it sees such a sysex. `conform.sh`
  remains the place those limits are tested.
- **Unknown, and it takes ROMs to settle:** whether `ours` and `munt-smf2wav`
  agree at all, and if not, by how much. Everything in this section is about a
  synthesiser running on zeroes.

---

## 11. What real music asks for

Added 2026-09-18, in answer to "can you find some public MIDI captures we can
test with". It found seven files, and then used them to close part of an open
question.

### 11.1 The question it is actually about

`bench/ANALYSIS.md` § 9.1(b) states the hole in the gate exactly:

> "A real score's cost is `fixed + per-partial × (partials that passage
> sounds)`, and this repository cannot learn the second factor: it is a
> property of the timbres in Roland's control ROM and of what the composer
> wrote. § 8 gives the first two terms honestly and leaves the third blank on
> purpose."

That is two unknowns treated as one. **The timbre half is Roland's and stays
unknown here. The composer half is in any MIDI file**, and there was no reason
to leave it blank except that nobody had looked. § 11.3 and § 11.4 are the
measurement; § 11.5 is what it does not settle.

### 11.2 What was collected, and the rule that shaped it

Seven files, from two projects that state a licence for their *music* and not
merely for their code:

| name | source | commit | licence | s | note-ons |
|---|---|---|---|---|---|
| `map24` | `freedoom/freedoom` `musics/d_map24.mid` | `d14dbbee` | BSD-3-Clause | 234.7 | 6413 |
| `map18` | `freedoom/freedoom` `musics/d_map18.mid` | `d14dbbee` | BSD-3-Clause | 521.2 | 7852 |
| `dm07` | `freedoom/freedoom` `musics/d_dm07.mid` | `d14dbbee` | BSD-3-Clause | 144.0 | 8728 |
| `inter` | `freedoom/freedoom` `musics/d_inter.mid` | `d14dbbee` | BSD-3-Clause | 74.6 | 1941 |
| `e1m1` | `freedoom/freedoom` `musics/d_e1m1.mid` | `d14dbbee` | BSD-3-Clause | 186.2 | 4792 |
| `rolling` | `OpenTTD/OpenMSX` `src/keep_on_rolling.mid` | `312ca0ae` | GPL-2.0-only | 195.0 | 6094 |
| `journey` | `OpenTTD/OpenMSX` `src/tttheme2.mid` | `312ca0ae` | GPL-2.0-only | 83.9 | 4056 |

Licences were read out of the source repositories at those commits:
Freedoom's `COPYING.adoc` ("Copyright © 2001-2024 Contributors to the Freedoom
project. … Redistribution and use in source and binary forms … are permitted
provided that…") with `CREDITS-MUSIC` naming each track's composer, and
OpenMSX's `LICENSE` plus `README.md` § 5.0 ("Copyright (C) 2010-2021 OpenMSX
Authors … licensed under GPL v2") with `src/themes.list` naming each file's
composer. `corpus/MANIFEST.tsv` carries all of that per row, plus a sha256.

**They are fetched, not committed.** The root `.gitignore` already sets that
policy for upstream material, and two of the seven are GPL-2.0-only, which has
no business sitting inside an otherwise-0BSD tree when a pinned fetch script
does the job. `corpus/fetch.sh` downloads from `raw.githubusercontent.com` with
the commit hash in the URL and refuses anything whose sha256 does not match.

**They are not MT-32 material and the corpus README says so twice.** Every file
is General MIDI for a software mixer; none carries MT-32 sysex; a General MIDI
program change means something else on an MT-32. For the A/B question — "do we
render what Munt renders?" — the content barely matters, because both legs get
the same file. For "does it sound like an MT-32?", **nothing here helps**, and
the route remains capturing a game you own through the emulator plumbing in
README.md.

### 11.3 What the scores ask for, measured from the files alone

`corpus/scan.py`, no rendering and no ROMs. Notes sounding simultaneously,
damper pedal honoured, restricted to the channels a factory-reset MT-32 listens
to — Munt's `Synth.cpp:897-903` sets `chanAssign` to `{1,2,…,9}` at reset, so
**MIDI channel 1 and channels 11-16 are ignored entirely**. Percentiles are
time-weighted.

```
                                           -- notes sounding, MT-32 channels --
file                          secs   notes note/s sysex  peak   p99   p90   p50
dm07.mid                     144.0    8728   60.6     0    20    16    12     8
e1m1.mid                     186.2    4792   25.7     0     9     9     7     5
inter.mid                     74.6    1941   26.0     1    16    16    16    15
journey.mid                   83.9    4056   48.3     0    24    19    14     6
map18.mid                    521.2    7852   15.1     0    21    19    17     8
map24.mid                    234.7    6413   27.3     0    29    27    16     7
rolling.mid                  195.0    6094   31.2     0    32    19    15    10
```

Three things in that table are worth pulling out.

- **The 32-partial ceiling is not a corner case.** An MT-32 timbre uses one to
  four partials, so N notes demand N to 4N partials against a machine that has
  32. Over the **143 files scanned** to choose these seven, 117 have a peak
  whose 4× demand reaches 32, 100 reach it at p90, and 49 reach it at the
  *median* moment of the piece. One file (`rolling`) reaches 32 on note count
  alone, with no multiplier at all.
- **The damper pedal roughly doubles it.** `map24`'s peak is 29 with CC64
  honoured and 16 without. Anything that counts simultaneous notes and ignores
  the hold pedal is reporting about half the truth.
- **Sysex is essentially absent from this material.** One event in 143 files: a
  single 8-byte Universal Real Time master volume, `f0 7f 7f 04 01 7f 7f f7`,
  in `d_inter.mid` and `d_fdmrdm.mid`. That is fine and expected —
  `conform/vectors.py` covers sysex deliberately, including a 40 000-byte one
  against `MTP_SYSEX_MAX`. Real files were collected for polyphony, not for
  parser edge cases, and they do not pretend otherwise.

### 11.4 What the partial manager actually holds — the measurement

The file tells you what the *score* asks for. What the *synthesiser* holds is a
different number, because the partial manager allocates, reserves and steals.
That needed the emulator, and the emulator needed timbres, and a fabricated
control ROM has none: `common.partialMute` is zero, so a note-on allocates no
partials at all and an honest tool reports zero for three minutes.

`corpus/probe.py` does what `bench/rtf_synth.cpp` does. The Timbre Temporary
Area is RAM, not ROM, so it writes a timbre of exactly *K* partials into all
eight melodic parts over ordinary Roland DT1 sysex, then lets the score play.
`ab_ref --partial-log` samples `Synth::getPartialStates()` every 128 frames —
the **unpacked** overload, `Synth.h:601`, not the one that packs four partials
into a byte and cost `bench/` a long detour (`ANALYSIS.md` § 8.10).

`./desktop/corpus/partials.sh`, whole pieces, 48 kHz, 128-frame sampling:

```
file        K    secs  peak   mean  p50  p90  p99   at 32
map24       1   234.7    21  10.30   10   15   18   0.00%
map24       2   234.7    32  20.38   20   28   32   3.88%
map24       3   234.7    30  24.80   27   30   30   0.00%
map24       4   234.7    32  27.17   28   32   32  35.15%
map18       1   521.2    20   9.66    8   17   19   0.00%
map18       2   521.2    32  17.70   16   28   32   1.65%
map18       3   521.2    30  21.27   24   30   30   0.00%
map18       4   521.2    32  24.95   28   32   32  22.90%
dm07        1   144.0    19   6.95    7   11   14   0.00%
dm07        2   144.0    32  13.87   14   22   28   0.27%
dm07        3   144.0    30  19.35   21   30   30   0.00%
dm07        4   144.0    32  22.43   24   32   32  30.06%
inter       1    74.6    19  14.05   15   17   17   0.00%
inter       2    74.6    32  24.90   26   30   30   0.99%
inter       3    74.6    30  25.59   27   30   30   0.00%
inter       4    74.6    32  28.00   28   32   32  38.22%
e1m1        1   186.2    10   5.94    6    9   10   0.00%
e1m1        2   186.2    20  11.88   12   18   20   0.00%
e1m1        3   186.2    30  17.71   18   27   27   0.00%
e1m1        4   186.2    32  20.95   20   28   32   4.56%
rolling     1   195.0    21   6.64    7   11   14   0.00%
rolling     2   195.0    32  13.24   14   22   28   0.46%
rolling     3   195.0    30  17.45   18   30   30   0.00%
rolling     4   195.0    32  20.07   20   32   32  25.25%
journey     1    83.9    24   6.65    6   13   19   0.00%
journey     2    83.9    32  12.56   12   24   32   2.32%
journey     3    83.9    30  15.21   15   30   30   0.00%
journey     4    83.9    32  17.74   20   32   32  18.01%
```

The measurement validates on a known answer first: `conform/vectors.py`'s
`voice` vector installs a timbre whose `partialMute` is `0x01` and plays one
note, and the sampler reports **peak 1, mean 1.00 of 32** over 750 samples. It
is not guessing.

The `K = 3` rows show the mechanism rather than an artefact: the peak is 30 and
never 32, because partials are allocated three at a time and 32 is not
divisible by 3. That is the real allocator, not a model of one.

**What it says.**

- With **two-partial** timbres, **six of the seven pieces reach the full 32
  partials** at some point. Only `e1m1`, deliberately chosen as the light end,
  does not.
- With **four-partial** timbres, six of seven spend **18 % to 38 % of their
  entire length pinned at 32 partials** — not a transient, a plateau. The
  time-weighted median sits at 20 to 28 partials.
- With **one-partial** timbres — the cheapest timbre set that can exist — the
  mean is 5.9 to 14.1 and the peak 10 to 24. Even that floor is not a small
  number.

**What it means for § 0's gate**, using `bench/ANALYSIS.md` § 8.4's cost line
`489.9 + 511.5 × partials` and § 8.5's 37 500 cycles per frame on one A7 core
at 1.2 GHz and 32 kHz:

| where the score sits | instr/frame | IPC needed for RTF ≤ 0.6 |
|---|---|---|
| 32 partials (six of seven files reach this) | 16 858 | **0.749** |
| `map24`, K=4, time-weighted mean 27.17 | 14 387 | 0.639 |
| `map24`, K=4, median 28 | 14 812 | 0.658 |
| `dm07`, K=1, mean 6.95 | 4 045 | 0.180 |

(16 858 is `489.9 + 511.5 × 32` as those rounded coefficients stand;
`bench/ANALYSIS.md` § 0 quotes 16 859 from the unrounded fit. The difference is
rounding in the published slope and changes no conclusion.)

The conclusion is narrow and it is the point of the exercise: **`bench/`'s
worst-case 32-partial figure is not a synthetic extreme. Real music reaches it,
and with plausible timbres it stays there for a third of a piece.** Sizing the
part for a "typical" partial count below 32 is not an option that the material
supports. The required-IPC row that matters remains **0.749**, and the
difference between the timbre sets is the difference between needing 0.18 and
needing 0.75 — which is to say the timbre half of the unknown is still the
dominant one.

### 11.5 What this does not settle, stated plainly

- **It is not what an MT-32 allocates.** Every timbre in the probe is one this
  directory made up. Which timbres a real game selects is in Roland's control
  ROM. `K = 1` is a floor for any timbre set and `K = 4` a ceiling; the truth is
  between two rows of the table and the table cannot say where.
- **Nothing here is a timing measurement.** The cost line is an instruction
  count from QEMU. `bench/ANALYSIS.md` § 9 lists every bias and they all point
  the same way. Only a Cortex-A7 on a desk answers § 0.
- **Nothing here is about sound.** No ROM has been loaded in any session that
  wrote any of this, and with fabricated ROMs an A/B on a corpus file compares
  silence to silence — `ab.sh` says so before the run and `abdiff.py` after it.
- **The probe makes three choices that are choices.** The eight melodic parts
  are assigned to the file's eight busiest channels rather than to the factory
  mapping (`--factory-channels` does the other thing); the rhythm part is
  switched off, because its timbres would come from ROM rhythm timbres a
  fabricated ROM does not have, which means every drum note in this
  drum-heavy material contributes **zero** and the real demand is *higher* than
  the table says; and the partial reserve is split evenly, four per part, where
  a real MT-32 takes it from the control ROM (`Synth.cpp:897`).
- **Program changes are stripped by the probe, and that was found by running
  it.** With them left in, every file reported exactly zero partials for its
  whole length: `Part::setProgram` calls `resetTimbre`, which copies the
  all-zero ROM timbre back over the one the preamble had just written, and game
  MIDI sends a program change on every channel in its first bar. With real ROMs
  this does not apply — `--keep-programs`, or do not probe at all.

### 11.6 What could not be got, so nobody repeats it

- **The egress proxy blocks essentially everything that is not GitHub.**
  Confirmed from this container: `mutopiaproject.org` and `imslp.org` both give
  `curl: (56) CONNECT tunnel failed, response 403`. Earlier in the same session
  `vgmusic.com`, `bitmidi.com`, `musescore.org`, `colinraffel.com` and
  `magenta.tensorflow.org` all refused the connection. `api.github.com` is 403
  for every repository but this one. **`git clone` and `raw.githubusercontent.com`
  are the entire acquisition channel.**
- **The Mutopia Project has no MIDI in its repository.** `MutopiaProject/
  MutopiaProject` at `2144afd6` is 17 137 files, 10 476 of them `.ily` and
  5 681 `.ly`: LilyPond sources. The `.mid` files Mutopia publishes are built
  artefacts and live on the blocked website, and there is no `lilypond` in this
  container to build them. It was the most promising public-domain source on
  the list and it produced nothing.
- **`cuthbertLab/music21`** (`ddf7c3eb`, BSD-3-Clause, © 2006-2026 Michael
  Scott Asato Cuthbert) has 24 `.mid`, of which 21 are synthetic parser
  fixtures — the one category this corpus was told not to duplicate — and the
  rest are a Mozart quartet movement, legally usable but thinner than anything
  already chosen.
- **`jazz-soft/test-midi-files`** (`ee79d9e3`) has 76 `.mid` and they are all
  generated test cases. Same reason.
- **`mido/mido`, `craigsapp/midifile`, `FluidSynth/fluidsynth`** ship no `.mid`
  at all. They parse MIDI; they do not carry it.
- **No freely licensed MT-32-native material was found and none was pursued
  past two searches.** A game's MT-32 stream belongs to the game. That is what
  the emulator capture route in README.md is for, and it is the only honest
  one.
