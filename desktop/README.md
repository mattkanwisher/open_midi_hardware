# `desktop/` — the port, on a laptop, out of the speakers

This builds the mt32-t113 platform layer against a real sound card and a real
MIDI input, so you can play the thing before the board exists. Everything above
the platform seam is the *same source* that will run on the T113:
`port/src/mtp_midi_parser.c`, `port/src/mtp_render.c` and the `mt32emu` binding
in `port/host/engine_mt32emu.cpp`. What this directory adds is a third
implementation of `port/include` — the first is `port/host` (renders to a WAV
file), the second is `emu/` (bare metal under QEMU), this is the one with
speakers.

That is `docs/PLAN.md` § 0.5 taken literally: if the ring discipline, the sysex
reassembly or the back-pressure handling is wrong, it is wrong here too, and
here you can *hear* it.

**It says nothing about speed.** The real-time factor it prints is an x86 or
Apple-silicon number. The gate in `docs/PLAN.md` § 0 is a Cortex-A7 number and
only `bench/rtf` on real silicon can answer it. See [FINDINGS.md](FINDINGS.md).

---

## From clone to sound, in four commands

No ROMs, no sound card and no MIDI hardware needed for any of this.

```sh
cmake -S desktop -B desktop/build && cmake --build desktop/build -j
./desktop/test.sh                       # 45 assertions; none need hardware
./desktop/conform.sh                    # every implementation of the seam,
                                        # same MIDI, compared sample for sample
./desktop/build/mt32-desktop --engine mt32emu-fakerom --midi-raw demo.syx \
        --tap-wav out.wav --seconds 4   # the real synthesiser, no Roland data
./desktop/corpus/fetch.sh               # seven licensed MIDI files to push it
```

Then, if you have a sound card, drop `--tap-wav` and add `--midi-seq` and plug
a keyboard in; and if you have your own legally dumped ROMs, put them in
`~/mt32roms/` and add `--roms ~/mt32roms`, and it is an MT-32.

Three ways to make a noise, in increasing order of realism:

| | what it is | needs ROMs |
|---|---|---|
| `--engine fake` | eight decaying sine voices. Not a preview of the sound; it exists so the ring, the parser and your cable can be tested | no |
| `--engine mt32emu-fakerom` | **the real mt32emu**, opened on fabricated ROM images. Every line of the emulator runs — the LA32, the envelopes, the partial manager, the analogue filter — and the result is meaningless noise, because the fabricated PCM ROM is zeroes. It is how this repository tests the synthesiser without Roland's data | no |
| `--engine mt32emu --roms DIR` | an MT-32 | yes |

`--engine mt32emu-fakerom` needs a MIDI stream that sets the machine up before
it plays anything, because the fabricated ROMs power up with the master volume
at zero. `desktop/conform/vectors.py` writes one:

```sh
python3 desktop/conform/vectors.py /tmp/vec        # demo, bank, bad, voice
./desktop/build/mt32-desktop --engine mt32emu-fakerom --midi-raw /tmp/vec/voice.syx \
        --audio null --ring 8 --tap-wav /tmp/voice.wav --seconds 2 --counters
```

---

## Build

### Linux

```sh
sudo apt install build-essential cmake        # Debian/Ubuntu
sudo apt install libasound2-dev               # optional: MIDI in from a keyboard or DAW
cmake -S desktop -B desktop/build
cmake --build desktop/build -j
```

Audio playback needs **no** development packages at all: miniaudio opens
`libasound.so.2` or `libpulse.so.0` at run time. `libasound2-dev` is only needed
for `--midi-seq`, and everything else still works without it.

### macOS

```sh
xcode-select --install                        # if you have not already
brew install cmake
cmake -S desktop -B desktop/build
cmake --build desktop/build -j
```

No other dependencies: CoreAudio, CoreMIDI and CoreFoundation ship with the OS.

> The macOS paths in this directory have **never been compiled or run** — the
> session that wrote them had no Mac. Expect to fix something the first time.
> See [FINDINGS.md](FINDINGS.md).

### What the build picks up

```
-- mt32-desktop: OS MIDI port = ALSA sequencer
-- mt32-desktop: linking mt32emu from .../bench/vendor/munt/mt32emu
```

`mt32emu` is found automatically in `bench/vendor/munt/mt32emu` and built out of
source — nothing under `bench/` is written to. The build also picks up
`emu/src/engine_mt32emu_fake_roms.cpp` if it is there, which is what provides
`--engine mt32emu-fakerom`; it is compiled from where it lives and never
copied, so there is one implementation of that fixture in the repository and
not two. To build without `mt32emu` at all:

```sh
cmake -S desktop -B desktop/build -DMT32EMU_SOURCE_DIR=
```

---

## Three commands, with a MIDI keyboard and your own ROMs

```sh
cmake -S desktop -B desktop/build && cmake --build desktop/build -j
mkdir -p ~/mt32roms && cp /wherever/MT32_CONTROL.ROM /wherever/MT32_PCM.ROM ~/mt32roms/
./desktop/build/mt32-desktop --midi-seq --roms ~/mt32roms
```

On Linux, `--midi-seq` creates an ALSA sequencer port called `mt32-t113`;
connect something to it with `aconnect`, or name the source on the command
line. On macOS it is written to connect every CoreMIDI source *and* publish a
virtual destination of the same name — **written, not run**: no part of the
macOS build has ever been compiled. See
[FINDINGS.md § 4](FINDINGS.md#4-macos-is-written-but-unverified), which lists
what a first Mac build should expect to fix, starting with a test that will
fail on a correct program.

---

## MIDI in: five ways, all at once if you like

Any combination of these can be given on one command line; they all funnel into
the same stamped-byte FIFO and through the same parser.

> **Two live sources at once is a byte-level merge, not a MIDI merge.** The seam
> is one byte FIFO, because the T113 has one UART. If two sources are genuinely
> transmitting at the same moment, one can interleave bytes into the middle of
> the other's message and both will be mangled. Running a keyboard and an SMF
> together is fine in practice (the keyboard is idle most of the time) and is
> useful for auditioning over a backing track; two busy sources is not a
> supported configuration, and a proper merge belongs in `aconnect` or your DAW.

### 1. A serial line at 31250 baud — the highest-fidelity test

This is the one that matters, because it is bit-for-bit what the T113's UART2
will see: 8N1, 31250 baud, one byte every 320 µs, with the timing of the wire.

```sh
# Linux
./desktop/build/mt32-desktop --midi-tty /dev/ttyUSB0 --roms ~/mt32roms
# macOS
./desktop/build/mt32-desktop --midi-tty /dev/tty.usbserial-A50285BI --roms ~/mt32roms
# a non-standard rate, if you are testing something odd
./desktop/build/mt32-desktop --midi-tty /dev/ttyUSB0,38400
```

Linux asks for 31250 with `TCSETS2` + `BOTHER` and reads the rate back; if the
driver rounded it, you get a warning rather than noise. macOS uses `IOSSIOSPEED`.
Most FTDI, CP210x and CH340 adapters do 31250 exactly; some cheap CH340 clones
do not, and the warning will tell you.

`Permission denied` on Linux: `sudo usermod -aG dialout $USER`, then log out and
back in.

**What to plug into it.** Not a class-compliant USB-MIDI adapter — those appear
as ALSA rawmidi or CoreMIDI devices, not as a tty, so use `--midi-seq` for them.
This input is for:

- a **USB-serial adapter** with a MIDI DIN input on its RX line. Wire it exactly
  as the board will be wired: DIN pin 4 → 220 Ω → 6N138 anode, DIN pin 5 →
  6N138 cathode, 6N138 output → adapter RX, with the pull-up to the adapter's
  logic rail. This is the same circuit `hw/` will carry, so it is also a test of
  the circuit.
- a **game port MIDI cable from a real DOS machine.** The game port's MIDI OUT
  (pin 12) is TTL at 5 V, so it goes through the same optocoupler, or through a
  divider into a 5 V-tolerant adapter. A DOS game writing to the MPU-401 at
  0x330 then drives this synth directly, which is about as close to the intended
  use as you can get without the board.
- an **MPU-401 card's MIDI OUT**, same circuit.

### 2. The OS MIDI port — a keyboard or a DAW

```sh
./desktop/build/mt32-desktop --midi-seq --roms ~/mt32roms
```

**Linux (ALSA sequencer).** The program creates a client called `mt32-t113` with
one writable port and prints its number. In another terminal:

```sh
./desktop/build/mt32-desktop --list-midi        # what can send to us
aconnect -l                                     # the same thing, ALSA's way
aconnect 'Keystation 49'  'mt32-t113'           # by name
aconnect 24:0 128:0                             # or by number
```

Or let the program subscribe for you:

```sh
./desktop/build/mt32-desktop --midi-seq 24:0
./desktop/build/mt32-desktop --midi-seq 'Keystation'
```

**macOS (CoreMIDI).** `--midi-seq` connects all sources and creates a virtual
destination named `mt32-t113`, which appears in every DAW's MIDI output list.

```sh
./desktop/build/mt32-desktop --list-midi        # sources CoreMIDI can see
./desktop/build/mt32-desktop --midi-seq 'Keystation'   # just that one
./desktop/build/mt32-desktop --midi-seq none    # virtual destination only
```

### 3. A FIFO or stdin — anything that can write bytes

```sh
# a named pipe, created for you if it is not there
./desktop/build/mt32-desktop --midi-fifo /tmp/mt32.midi --roms ~/mt32roms &
amidi -p hw:2,0,0 -r /tmp/mt32.midi             # raw bytes from a USB keyboard
cat captured_session.syx > /tmp/mt32.midi       # replay a capture
printf '\x90\x3c\x64' > /tmp/mt32.midi          # middle C, by hand

# or straight down a pipe
cat captured_session.syx | ./desktop/build/mt32-desktop --midi-fifo - --ring 8
```

The FIFO is opened read/write, so a writer exiting does not end the run — you
can `cat` into it as many times as you like. stdin, by contrast, ends the run a
couple of seconds after EOF, which is what you want for a scripted test.

Note that a FIFO delivers as fast as the writer writes, which for a file is far
faster than 31250 baud. Use `--midi-tty` when the arrival *timing* is what you
are testing.

### 4. A file of raw MIDI bytes — reproducibly

```sh
./desktop/build/mt32-desktop --midi-raw capture.syx --seconds 4 --tap-wav out.wav
./desktop/build/mt32-desktop --midi-wire capture.syx          # ... at 31250 baud
./desktop/build/mt32-desktop --midi-wire capture.syx,38400    # ... or some other rate
```

`--midi-raw` hands the whole file to the render loop before the first block is
rendered, and `--midi-wire` releases it one byte every 320 µs against the same
clock the render loop reads — the arrival pattern of a real cable, without a
cable.

The difference between `--midi-raw` and `--midi-fifo FILE` is worth knowing.
A FIFO is read by a separate thread, so an event can land one pump call — and
therefore 128 frames — later or earlier depending on how the kernel schedules
that thread. `--midi-raw` is pulled on the render thread, so **the same bytes
always produce exactly the same samples**. That is what makes
`./desktop/conform.sh` able to compare this build's audio against `port/host`'s
and against the bare-metal image's, and `./desktop/test.sh` asserts it.

### 5. A Standard MIDI File — unattended runs

```sh
./desktop/build/mt32-desktop --midi-smf demo.mid --roms ~/mt32roms
./desktop/build/mt32-desktop --midi-smf demo.mid --midi-loop      # until Ctrl-C
```

Formats 0, 1 and 2, tempo changes, PPQN and SMPTE division, running status
expanded, sysex reconstructed. The file is turned into a byte stream and paced
against the same clock the render loop reads, so it goes through the parser like
anything else. Meta events are dropped; they have no wire representation.

---

## Driving this from an emulator

Every claim in this section is either something read in the emulator's own
source, with the file and line, or marked as not verified. **None of it has
been run**: the container this was written in has no sound device, no ALSA
sequencer (`/dev/snd` does not exist and neither does `/proc/asound/seq/clients`)
and no emulator installed. Read it as "this is what the source says the setting
is", not as "this was tried".

Start our end first, and note the client:port it prints:

```sh
./desktop/build/mt32-desktop --midi-seq --roms ~/mt32roms &
aconnect -l              # find our client number; the emulator wants it
```

### DOSBox Staging

```ini
[midi]
mididevice = port
midiconfig = 128:0        # the client:port mt32-desktop printed
```

`port` is the default and the current spelling. **`alsa` still works but is
deprecated** and is silently turned into `port`
(`src/midi/midi.cpp:915-919`, `SetDeprecatedWithAlternateValue`), together with
`auto`, `coremidi`, `oss` and `win32`; an older copy of this README told you to
write `alsa`, which now only works by that compatibility path. `midiconfig`
takes the ALSA address and its own help text says where to get it — "use the
Linux command `aconnect -l` to list all open MIDI ports" (`midi.cpp:944-946`).
Staging's *internal* Munt is `mididevice = mt32` with a `[mt32]` section
(`midi.cpp:886-889`); choosing `port` is what turns it off, and there is no way
for both to have the stream.

### DOSBox-X

```ini
[midi]
mididevice = alsa
midiconfig = 128:0
```

Different program, different spelling: DOSBox-X looks the name up in a list of
handlers and the ALSA one answers to `alsa` (`src/gui/midi_alsa.h:121`). Both
keys are read at `src/gui/midi.cpp:613-614`. `midiconfig` is parsed by
`parse_addr()` (`midi_alsa.h:100-118`): `client:port`, or a leading `s` for
"whoever subscribes". It also ships `midi_mt32.h`, so `mididevice = mt32` is
its built-in Munt.

### ScummVM

```sh
SCUMMVM_PORT=128:0 scummvm -e alsa --native-mt32 <game>
```

`-e` is `--music-driver` (`base/commandLine.cpp:139`, `:786`) and the ALSA
plugin's id is `alsa` (`backends/midi/alsa.cpp:346-348`). `SCUMMVM_PORT`
overrides whatever the configuration says, and the code warns that it is doing
so (`alsa.cpp:448-456`) — which makes it the least ambiguous way to point
ScummVM at us. **`--native-mt32` matters** (`commandLine.cpp:177`, `:924`): it
tells ScummVM the thing on the other end really is an MT-32, so it stops
converting the MT-32 data to General MIDI. `-e mt32` is ScummVM's own built-in
Munt and is the thing you are choosing *not* to use.

### MAME, and the X68000

MAME emulates the Sharp CZ-6BM1 MIDI board and wires it to real host MIDI
ports: the device's `device_add_mconfig` creates `MIDI_PORT(config, "mdout",
midiout_slot, "midiout")` (`src/devices/bus/x68k/x68k_midi.cpp:20-27`), and the
`midiout` image device hands its filename straight to the OSD layer —
`m_midi = machine().osd().create_midi_output(filename())`
(`src/devices/imagedev/midiout.cpp:60-67`), so the argument is **a host MIDI
port name, not a file**. The x68000 driver offers the card as the slot option
`x68k_midi` (`src/mame/sharp/x68k.cpp:920`) in either of two expansion slots
tagged `exp1` and `exp2` (`src/mame/sharp/x68k.h:64`):

```sh
mame x68000 -exp1 x68k_midi -midiout "mt32-t113"      # not run here
```

The MIDI back end is chosen with `-midiprovider`
(`src/osd/modules/lib/osdobj_common.cpp:151`). The exact spelling of the
`-midiout` option in a machine that also has a `midiin` device is **not
verified** — MAME derives image-device option names from the device's instance
name, which is `midiout` (`src/devices/imagedev/midiout.h:48-51`), but nothing
here ran MAME to confirm it is not `-mdout` or numbered.

### 86Box

In the `.cfg`, `midi_device = system_midi` selects the host MIDI output
(`src/config.c:912-914`; the internal name is `system_midi` on everything but
Windows, where it is `windows_midi` — `src/include/86box/midi.h:98-107`). Which
host port it opens is a *device* setting: `midi` in the `[System MIDI]` section
(`src/sound/midi_rtmidi.cpp:90` and `:231-244`), an index into the ports RtMidi
enumerates. 86Box's own Munt is `mt32`, `mt32_new` or `cm32l`
(`src/sound/midi_mt32.c:463-492`), and picking `system_midi` is how you get the
stream out to us instead.

### PCem — needs `snd-virmidi`

PCem does not use the ALSA *sequencer* at all. It opens an ALSA **rawmidi**
device by card/device/sub: `snd_rawmidi_open(NULL, &midiout, "hw:%i,%i,%i", …)`
(`src/midi_alsa.c:100-107`), with the index coming from the config key `midi`.
Our `--midi-seq` port is a sequencer client and cannot be opened that way, so
the route is a virtual raw port in between:

```sh
sudo modprobe snd-virmidi                  # gives hw:N,0,0 .. and seq clients
aconnect -l                                # find "Virtual Raw MIDI" client
aconnect <virmidi client>:0 <our client>:0
# then point PCem at the matching hw: device in its MIDI settings
```

### px68k — no MIDI out at all

The obvious X68000 emulator on Linux cannot do this. `px68k` emulates the
CZ-6BM1 (`x68k/midi.c` is headed "MIDI Board (CZ-6BM1) emulator") and sends
every message through the Win32 MME calls it inherited from WinX68k —
`midiOutOpen`, `midiOutShortMsg`, `midiOutLongMsg` — and its own replacement
for those calls does nothing: `midiOutOpen` returns `!MMSYSERR_NOERROR`, which
`MIDI_Init()` turns into `hOut = 0`, and `midiOutShortMsg` is `(void)dwMsg;
return MMSYSERR_NOERROR;` (`win32api/fake.c:101-129`, `x68k/midi.c:318-326`,
checked at `hissorii/px68k` master). Its `juliet.c`, which in WinX68k talks to
a real ROMEO/Juliet card, is `#if 0` from top to bottom. **For an X68000 stream,
use MAME.** Whether some fork of px68k has added MIDI out is not something this
session could check.

### XM6 and XM6 TypeG — not verified

Windows-only, and the sources were not reachable from here. Search results
describe XM6 TypeG as the most complete X68000 emulator and confirm it emulates
a MIDI board, but **no claim about its MIDI-out configuration appears here
because none could be read**. If you are on Windows, the shape to expect is an
MME port selection plus a loopback driver.

---

## From an emulator to an A/B: capture once, render many

Listening to a game through us and then through something else is not a
comparison, because the two runs are not the same MIDI. The stream a game
emits depends on when the emulator's timer fired; play it twice and you have
two different streams. So capture it **once**, and render that one file through
everything offline.

Two ways to capture, and they can both be done while you play:

**1. The emulator's own capture.** DOSBox Staging records MIDI output to a
`.mid` on Ctrl+Alt+F6 (`src/capture/capture.cpp:656-660`, binding name
`caprawmidi`), writing an SMF whose delta times are `PIC_Ticks`, i.e.
milliseconds (`src/capture/capture_midi.cpp:28-93`). DOSBox-X has the same
feature under the same binding name but **with no default key** —
`MAPPER_AddHandler(CAPTURE_MidiEvent, MK_nothing, 0, "caprawmidi", "Record MIDI
output")` (`src/hardware/hardware.cpp:2300`) — so bind it in the mapper or use
the menu item; it writes `.mid` through `OpenCaptureFile("Raw Midi", ".mid")`
(`hardware.cpp:2051-2058`). This captures whatever the game sends whether or
not anything is listening.

**2. ALSA, on the way past.** A sequencer source can have two destinations, so
you can hear it and record it at the same time:

```sh
./desktop/build/mt32-desktop --midi-seq --roms ~/mt32roms &
aconnect -l                                   # find both client numbers
arecordmidi -p <emulator client>:0 capture.mid &
aconnect <emulator client>:0 <our client>:0
# play the game; Ctrl-C arecordmidi when the passage is done
```

`aseqdump -p <emulator client>:0` in place of `arecordmidi` gives you a
readable log of the same stream, which is how you find out whether a game is
really sending MT-32 sysex or has decided you are a General MIDI module.

Then render the capture through every engine you have:

```sh
./desktop/ab.sh capture.mid --roms ~/mt32roms --out ~/ab
```

---

## Audio out

```sh
./desktop/build/mt32-desktop --list-devices
./desktop/build/mt32-desktop --midi-seq --device 'USB Audio'
./desktop/build/mt32-desktop --midi-seq --audio jack        # or alsa, pulse, coreaudio
./desktop/build/mt32-desktop --midi-seq --audio null --tap-wav out.wav   # headless
```

`--audio null` plays nothing and paces in real time. It is how this build is
tested on a machine with no sound device; add `--tap-wav` to keep the audio and
listen to it somewhere else.

### Latency

The defaults are the target's numbers from `port/DESIGN.md` § 2.2: 128-frame
blocks (2.67 ms at 48 kHz) and a 3-block ring, for 8 ms of audio in flight.

```sh
--block 64 --ring 3      # 4 ms of ring; lower latency, less tolerance
--block 128 --ring 3     # the default, and the T113's configuration
--block 256 --ring 4     # 21 ms; what a busy or virtualised desktop wants
--periods 2              # device buffering behind our ring; 2 is the minimum
```

**If you get underruns with the defaults, read the summary before assuming the
render loop is slow.** It prints how long the device actually went between
asking for audio. A desktop OS under load, a VM, or PulseAudio's own buffering
can be 10 ms apart where the T113's DMA interrupt is 2.67 ms apart by
construction; a 3-block ring cannot survive that, however fast the synth is. The
summary says so explicitly when it happens, and `--ring 8` fixes it without
changing anything the target has to do.

---

## When there are no ROMs — which is the normal case

MT-32 and CM-32L ROMs are Roland's. They are not in this repository, they never
will be, and `mt32emu` checks them by SHA-1, so there is no way to fake one.

With no ROMs, `mt32-desktop` says so and falls back to the **fake engine**:
eight decaying sine voices, one internal 32 kHz clock, a deliberately small
sysex store. It is not a preview of the sound and is not meant to be. What it
*is* good for is everything except the sound:

```sh
./desktop/build/mt32-desktop --engine fake --midi-seq --ring 8
```

— the ring, the parser, sysex reassembly, back-pressure, timestamps, the audio
device, your MIDI cable and your routing all get exercised, and the counters at
the end tell you whether any of it is wrong. Wire up your keyboard with the fake
engine first; then, when the ROMs arrive, only the synthesis is new.

There is one more step available before the ROMs arrive. `--engine
mt32emu-fakerom` runs **the real mt32emu** on fabricated ROM images — the
fixture in `emu/src/engine_mt32emu_fake_roms.cpp`, derived from Munt's own test
suite, which hands the library the SHA-1 it expects instead of letting it hash
the fabrication. Everything in the emulator runs: the C++ runtime, the ~949 KiB
`Synth::open()` allocates, `Tables::getInstance()`, the LA32, the envelopes, the
partial manager, the analogue output filter. What it cannot do is sound like an
MT-32, because the PCM ROM it is given is 512 kB of zeroes.

```sh
python3 desktop/conform/vectors.py /tmp/vec
./desktop/build/mt32-desktop --engine mt32emu-fakerom --midi-raw /tmp/vec/voice.syx \
        --ring 8 --seconds 2 --tap-wav /tmp/voice.wav --counters
```

The `voice` stream matters: fabricated ROMs power the machine up with master
volume 0, no partials reserved to any part and every timbre a zero timbre, so a
plain note on renders exact silence. `voice` writes a working System area,
Patch Temporary area and Timbre Temporary area over sysex first, and then the
note renders at peak 9300 of 32767. `desktop/conform.sh` uses it to compare
this synthesiser against the bare-metal one.

Once you have dumped your own ROMs:

```
~/mt32roms/MT32_CONTROL.ROM      64 kB or 128 kB
~/mt32roms/MT32_PCM.ROM          512 kB
~/mt32roms/CM32L_CONTROL.ROM     64 kB     (optional)
~/mt32roms/CM32L_PCM.ROM         1 MB      (optional)
```

```sh
./desktop/build/mt32-desktop --roms ~/mt32roms --machine mt32  --midi-seq
./desktop/build/mt32-desktop --roms ~/mt32roms --machine cm32l --midi-seq
./desktop/build/mt32-desktop --control-rom a.rom --pcm-rom b.rom --midi-seq
```

A ROM that `mt32emu` does not recognise is named and refused rather than played
as noise. That is `port/DESIGN.md` § 4.3 working, and it is the same code path
the T113 will take off the SD card.

---

## Reporting

While it runs, one line every half second:

```
  12.3s  rtf 0.081 (desktop, not the A7 gate)  ring 2  under 0  midi 148B 41m 2sx  err 0
```

At the end, the full set, chosen so that the first question a person asks —
"why does it sound wrong?" — is answered by reading downwards:

- **underruns**, and beside them the two numbers that say whether the render
  loop or the operating system is responsible: how long the device actually
  went between asking for audio, and how much audio the ring holds. When the
  first exceeds the second, the summary says so in full sentences rather than
  leaving you to conclude the design is broken.
- **minimum ring occupancy** and **worst single-block render time** against the
  block budget, as a fraction of one block.
- **how the device behaved**: request count, largest single request, how often
  the render loop slept and how often it woke on its own timer rather than on
  the device — which on the T113 is "did the DMA interrupt arrive".
- **MIDI**: bytes, messages by kind, FIFO peak and overruns, every parser error
  class separately, engine back-pressure, and UART framing errors (always 0 on
  a POSIX tty, and the summary says why).
- **what actually came out**: frames committed, peak sample, how many samples
  were non-zero, and an FNV-1a fingerprint of every sample committed. If a run
  is silent, those three numbers tell you instantly whether the audio path is
  dead or the engine is — and if every sample was zero, the summary says which,
  because with the fabricated ROMs silence is the expected answer.
- the **real-time factor**, with a paragraph explaining what it is not.

`--counters` adds the counter block that `port/host` and `emu/` print, in the
same words, so one set of assertions can be made against all three
implementations. That is what `conform.sh` reads.

`--status-ms 0` turns the live line off. Logs are on stderr, the summary is on
stdout, so `> run.txt` keeps the summary and leaves the log on the terminal.

The process exits non-zero if there was an underrun, so it is usable in a script.

---

## Configuration: `mt32.cfg`

The finished module reads a text file from the root of its microSD card. This
build reads the same file, in the same format, so the format is exercised long
before the card exists. Copy [`mt32.cfg.example`](mt32.cfg.example) — which is
fully commented and is entirely defaults — to `mt32.cfg` beside wherever you
run the binary, or point at one:

```sh
./desktop/build/mt32-desktop --config ~/my.cfg --midi-seq
```

```ini
machine   = mt32          # mt32 | cm32l
rom_dir   = roms
engine    = auto          # auto | fake | mt32emu | mt32emu-fakerom
rate      = 48000
block     = 128
ring      = 3
partials  = 32
reverb    = on
midi_baud = 31250
log       = info
desktop_audio = auto      # keys the module ignores carry this prefix
```

- `key = value`, one per line; `#` or `;` starts a comment, but only at the
  start of a line, because paths contain `#`.
- Booleans are `on off yes no true false 1 0`. Numbers are plain decimal.
- A value runs to the end of the line and needs no quoting, so a path with a
  space in it just works.
- **An unknown key is a warning, never a failure** — an old firmware has to
  read a new card and a new firmware an old one — and the warning names the
  line number.
- **The command line beats the file.** The card is the standing configuration;
  the flags are what you are trying right now.

The parser (`src/desktop_config.h`, `src/desktop_config.c`) allocates nothing,
uses no floating point, and makes one forward pass over a buffer read in one go
by `mtp_storage_load()` — it is written to move into `port/src` unchanged and
run on the T113 off FatFs.

---

## Tests

```sh
./desktop/test.sh
```

45 assertions, none of which need a sound card, MIDI hardware or ROMs: the loop
against a device that is pulling at it, the demo wire stream through stdin, a
16 kB timbre bank dump reassembled while audio flows, a stream that is malformed
in three separate ways, an SMF, `mt32emu` refusing both missing and wrong ROMs,
`--midi-seq` failing cleanly where there is no sequencer, the serial path driven
byte-by-byte through a pty, `--midi-raw` producing identical audio on two runs,
`mt32.cfg` being read and overridden, and the real mt32emu on fabricated ROMs
rendering something audible and then, on a different stream, rendering silence
and saying why.

One class of assertion reports `skip` rather than `FAIL`: underruns that a
*measured* device stall explains. When the null device goes 38 ms between
requests and the ring holds 21 ms, the ring runs dry however fast the render
loop is; the test prints both numbers and moves on, because that is this
machine's scheduler rather than the port. Underruns with no such stall behind
them still fail.

What they do **not** test is whether anything is audible. Nothing on a machine
with no sound device can.

---

## The conformance run: is it the same program on every platform?

```sh
./desktop/conform.sh
```

`docs/PLAN.md` § 0.5 rule 2 says every implementation of the platform seam
passes the same conformance tests. This is the command that checks it. It runs
the *same MIDI bytes* through every implementation that exists —

| | |
|---|---|
| `host-x86` | `port/host`, built for x86-64 |
| `host-armv7` | `port/host`, cross-built for Cortex-A7, under `qemu-arm` |
| `armv7-fpoff` | the same, with `-ffp-contract=off` |
| `emu-a7` | `emu/`'s bare-metal image, from reset, under `qemu-system-arm` |
| `desktop` | this build, against the null device |

— on four MIDI streams and two engines, and compares the rendered PCM sample
for sample plus every counter in the contract. It needs
`gcc-arm-linux-gnueabihf`, `qemu-user` and `qemu-system-arm` for the ARM legs,
and skips them cleanly without. It writes nothing outside `desktop/build`
except `emu/build`, and only if that image is not already built.

Today it reports **seven of eight comparisons byte-identical across every
implementation, and one real divergence**: with the project's current ARM
flags, GCC fuses a multiply-accumulate inside mt32emu's analogue output filter
into `VFMA`, and armv7 then renders 1 LSB differently from x86-64 on 0.07% of
samples. `-ffp-contract=off` removes it exactly. The bisection, the line number
and what it means for the board are in
[FINDINGS.md § 8.2](FINDINGS.md#82-the-divergence-and-its-cause). The script
says at the end what it expects to find, so a *different* result is visible as
a change rather than as noise.

---

## The A/B run: how far are we from Munt?

```sh
./desktop/ab.sh                                     # no ROMs: runs, proves little
./desktop/ab.sh capture.mid --roms ~/mt32roms --out ~/ab
```

`conform.sh` asks "is it the same program on every platform?". This asks a
different question — "is it the same *sound* as somebody else's renderer?" —
and it takes one MIDI file (an SMF, or a raw byte capture) and renders it
through up to four legs, which are **not equal evidence**:

| leg | what it is | evidence |
|---|---|---|
| `ours` | `mt32-desktop`: parser, render loop, ring, block scheduler, engine | what we ship |
| `ref-seam` | `desktop/build/ab/ab_ref`: events straight into the engine at sample-exact times, none of our code above it | isolates our code from the library's |
| `munt-smf2wav` | **Munt's own `mt32emu-smf2wav`**, built out of `bench/vendor/munt` | the one that answers the question — **needs real ROMs** |
| `munt-orig` | the same on the original `.mid` rather than the normalised one | catches our SMF reader disagreeing with libsmf |

It prints frames, peak, RMS, onset and SHA-256 per leg, and for each pair: how
many samples differ and from where (`conform/compare.py`'s own comparison, not
a second copy of it), the RMS of the difference in LSB and dBFS, how far the
onsets are apart, and the shift that best explains the difference — because "the
same audio 17 ms late" and "different audio" are different findings.

**Two modes, and the difference matters.** A raw byte capture has no time in
it, so every leg is handed the whole stream before the first frame and they
must agree *exactly*; `ab.sh` asserts that and exits non-zero if they do not.
An SMF has time in it, and our leg is paced by a wall clock while the reference
legs are paced by arithmetic, so they will not agree sample for sample and it
does not assert they do. What it does instead is quantify the gap.

**Without ROMs it still runs end to end**, on the fabricated images of
`emu/src/engine_mt32emu_fake_roms.cpp`, and says in its own output that what it
compared is not MT-32 audio and proves nothing about how the product sounds.
Munt's renderer cannot join in at all there: it loads ROMs from files and
hashes them, and there is no fabricated-ROM path into it.

What `ab.sh` produces, for a user with ROMs and speakers, is a directory of
WAVs of the same passage rendered by us and by Munt. Listening to those two
files is evidence this project has never had —
[FINDINGS.md § 10](FINDINGS.md#10-the-ab-rig-how-do-we-compare-with-munt).

---

## The corpus: real music, and what it measures

```sh
./desktop/corpus/fetch.sh                        # ~390 kB, pinned and verified
python3 desktop/corpus/scan.py desktop/corpus/files/*.mid
./desktop/ab.sh --corpus map24 --roms ~/mt32roms
./desktop/corpus/partials.sh                     # needs no ROMs
```

Seven MIDI files from two projects that state a licence for their music —
Freedoom (BSD-3-Clause) and OpenTTD's OpenMSX music set (GPL-2.0-only) — chosen
out of 143 candidates for polyphony, length and event rate. They are **fetched,
never committed**, pinned to commit hashes, with the provenance and the
copyright holder of every file in
[`corpus/MANIFEST.tsv`](corpus/MANIFEST.tsv). That is the same treatment
`bench/vendor` and `boot/vendor` get, and the root `.gitignore` says why.

**Be clear about which question they answer.** For "do we render the same audio
as Munt?" the content barely matters — both sides get the same file — and this
corpus is a convenience. For "does this sound like a real MT-32?" **it is no
help at all**: every file is General MIDI written for a software mixer, with no
MT-32 sysex and none of the MT-32's channel and patch conventions. The route to
*that* question is the one above: capture a game you own through an emulator
and render the capture. [`corpus/README.md`](corpus/README.md) will not let you
forget the difference.

What the corpus *is* for is **load**, and it closed part of an open question.
`bench/ANALYSIS.md` § 9.1(b) says a repository with no ROMs cannot know how
many partials a real score sounds, because that depends on Roland's timbres and
on what the composer wrote. The timbre half stays unknown; the composer half is
in the file, and `corpus/scan.py` and `corpus/partials.sh` measure it.
Measured here, on these seven: **six of the seven reach the MT-32's 32-partial
ceiling with two-partial timbres, and with four-partial timbres they sit at the
ceiling for 18 % to 38 % of their length.** The worst case in `bench/`'s cost
line is not a synthetic extreme; it is where real music spends its time.
[FINDINGS.md § 11](FINDINGS.md#11-what-real-music-asks-for) has every number
and what it does not prove.

Without ROMs, note that an A/B on a corpus file compares **silence to silence**
— a fabricated control ROM has no timbres, so nothing sounds. `ab.sh` says so
before the run and `abdiff.py` says so after it. The part of a no-ROM corpus run
that measures something is the partial probe:

```sh
./desktop/ab.sh --corpus map24 --count-partials --probe-partials 4
```

---

## Licences

- **miniaudio** (`vendor/miniaudio/`) — public domain (Unlicense) or MIT-0, at
  your choice. Vendored whole, with its licence text, so this builds on a clean
  machine with no package manager involved.
- **`mt32emu-smf2wav`** — GPL v3 or later, and it is Munt's, not ours.
  `ab.sh` builds it out of `bench/vendor/munt` into `desktop/build/ab` and runs
  it as a separate program; nothing of it is linked into anything here.
- **`mt32emu`** — LGPL 2.1. This program links it statically when it is built
  in, which carries the usual obligations; the boundary is one file,
  `port/host/engine_mt32emu.cpp`, and the library is built from the unmodified
  vendored source in `bench/vendor/munt/`.
- **Everything in `desktop/src`, `desktop/conform`, `desktop/ab`,
  `desktop/corpus`'s own scripts and the shell scripts** — 0BSD, like the rest
  of `port/`.
- **The corpus MIDI files** — not ours and not in this repository.
  `corpus/fetch.sh` downloads them from pinned commits, each under its own
  project's licence: Freedoom's music is BSD-3-Clause (© 2001-2024 Contributors
  to the Freedoom project) and OpenTTD's OpenMSX set is GPL-2.0-only (© 2010-
  2021 OpenMSX Authors). Both require the copyright notice to travel with the
  work, which is why `corpus/MANIFEST.tsv` names the composer of every file and
  `fetch.sh` downloads each project's licence text beside them.
- **ROMs** — Roland's. Dump your own. Never redistribute them.
