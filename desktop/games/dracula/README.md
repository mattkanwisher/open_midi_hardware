# Akumajou Dracula (X68000) → mt32-t113, both ways

The second game through the port, and the first that exercises **both halves
of the module from one MIDI cable**. Konami's 1993 X68000 Castlevania was
written for a Roland module on the CZ-6BM1 MIDI board, and it ships two
separate soundtracks: one arranged for the LA synths (MT-32 / CM-32L / CM-64)
and one for the GS synths (SC-55 family). Which one you get is a menu choice
at boot.

Nothing in the emulator was modified. MAME has had the X68000 MIDI board as an
expansion-slot option (`-exp1 x68k_midi`, a YM3802) and a `-midiout` port for
years; on macOS that port is a CoreMIDI destination, and `mt32-desktop
--midi-seq` publishes one called `mt32-t113`. The chain is:

```
game ──YM3802──▶ MAME x68k_midi ──CoreMIDI──▶ mt32-desktop ──▶ mtp_midi_parser ──▶ engine ──▶ speakers
                                                              (same source as the T113)   mt32emu (LA)
                                                                                          fluidsynth (GS)
```

## Run it

```sh
cmake -S desktop -B desktop/build && cmake --build desktop/build -j
brew install mame fluid-synth                      # once
./desktop/games/dracula/run.sh la                  # MT-32: mt32emu + ~/mt32roms
./desktop/games/dracula/run.sh gs                  # SC-55: fluidsynth + ~/soundfonts/GeneralUser-GS.sf2
./desktop/games/dracula/run.sh sc55                # SC-55: Nuked-SC55, the reference, not the port
```

The disk images are expected as `old_games/Akumajou Dracula (1993)(Konami)(Disk
1 of 2).dim` and `(Disk 2 of 2).dim` (gitignored; `DRACULA_DIR` to point
elsewhere), and the X68000 ROMs — `iplrom.dat`, `cgrom.dat` — in
`old_games/BIOS/Sharp/X68000/` (`X68K_BIOS`). GS mode wants a GS-flavoured
SoundFont; it was tested with [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS)
2.0.3 (32 MB, free licence), which is what mt32-pi ships too. `SOUNDFONT` to
use another.

`sc55` is the control group. It points MAME at our fork of
[Nuked-SC55](../../../bench/vendor/Nuked-SC55) instead of `mt32-desktop`
(`--virtual-port sc55` is the fork's addition: it publishes its own CoreMIDI
input instead of opening an existing one), with the ROMs from `~/sc55roms`
(`SC55_ROMS`). Nothing of ours is in that chain and it prints no counters; it
exists so that option 3 through the GS SoundFont can be heard next to option 3
through a cycle-accurate SC-55, which is the sound the game was mixed for.

MAME opens with a "known problems: imperfectly emulated graphics" box and
waits for a key. Press one. Disk 1 boots to the music menu; press **2** for LA
or **3** for GS, in the window or from a terminal:

```sh
echo "key 3" > desktop/build/dracula/cmd.txt
```

`ctl.lua` types that into the emulated keyboard, so no macOS input permission
is involved. `reset` in the same file returns to the menu. Close the MAME
window and the synth's summary is printed.

## The menu

```
悪魔城ドラキュラ 音楽設定モード              Akumajou Dracula — Music Setup Mode
このゲームは以下の音源に対応しています      This game supports these sound sources:
1・X68000内蔵音源                           1. X68000 built-in (YM2151 FM + ADPCM)
2・ローランド社LA音源  MT-32、CM-32L、CM-64   2. Roland LA: MT-32, CM-32L, CM-64
3・ローランド社GS音源  SC-55、SC-33、SC-155、  3. Roland GS: SC-55, SC-33, SC-155,
                        CM-300、CM-500                       CM-300, CM-500
使用する音源を選択して対応する番号を          Pick your sound source and enter its number
キーボードまたはジョイスティックから          from the keyboard or joystick
２メガRAM対応                                 2 MB RAM
```

## What the counters said, 2026-09-19

Apple silicon, `--ring 8`, both runs through the same parser and ring.

| | **2 · LA** (mt32emu, MT-32 v1.07 ROMs) | **3 · GS** (fluidsynth 2.6, GeneralUser GS) |
|---|---|---|
| length | 1419 s | 1006 s |
| midi bytes | 144 421 | 211 327 |
| short messages | 39 952 | 70 372 |
| sysex | **120** | **70** |
| midi fifo peak | 310 bytes | 111 bytes |
| fifo overruns / orphan data / sysex >32 kB / sysex aborted | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| engine back-pressure | 0 | 0 |
| rtf, average | 0.010 | 0.035 |

The two soundtracks have different shapes on the wire, which is the point of
running both. On **2** the game uploads a custom timbre bank the moment you
choose it — about 18 kB of sysex in the first few seconds, the same thing
Sierra's `MT32.DRV` does for [SQ3](../sq3/README.md) — and every one of those
packets came through MAME's port and CoreMIDI intact. On **3** there is no
upload to do: a GS reset, a handful of parameter sysex, and then roughly
twice the note traffic, because the GS arrangement uses more channels and
more controller data. FluidSynth logged `Instrument not found [bank=8
prog=88]` a few times — the game selects SC-55 variation banks that
GeneralUser GS does not carry, and FluidSynth falls back to bank 0 as an
SC-55 would. That is a SoundFont gap, not a port bug.

**Underruns are the desktop's problem, not the synth's.** A first pass of
each mode logged hundreds of dropouts; both turned out to be the laptop —
a `cmake --build -j` during the LA run, and a stale second MAME instance
eating a quarter of a core during the GS run. A second GS run (36 min,
448 kB, 140 sysex, still 0 parser errors) held **0 underruns for its first
19 minutes** with the synth at 2–4 % of a core and rtf between 0.01 and 0.05,
and only started dropping when `desktop/test.sh` was run three times next to
it. As always, the number that matters is `bench/rtf` on the Cortex-A7, and
nothing here predicts it.

## Files

| | |
|---|---|
| `run.sh` | pick `la`, `gs` or `sc55`, start the matching synth, wait for its CoreMIDI destination, launch MAME with the MIDI board, print the summary |
| `ctl.lua` | MAME autoboot script: `cmd.txt` → emulated keyboard / soft reset / screenshot; optional periodic screenshots with `MAME_SNAP_S` |

## Things worth knowing

- **`-bios ipl11`** is passed explicitly. MAME's `x68000` ROM set also lists the
  CZ-600CE V1.0 IPL as two 64 kB chips, which the usual BIOS packs do not
  include; naming the IPL 1.1 BIOS lets it boot without them.
- **MAME's snapshots go black once the game switches video mode**, though the
  window is fine. If you drive it blind, the first MIDI bytes (46 B, one sysex)
  mean the menu is up; a burst of sysex after your keypress means 2 was taken,
  a small one means 3.
- **Keys typed before the menu appears are lost.** Wait for those 46 bytes.
- **`gs` is not an SC-55; `sc55` is.** Option 3 through the port is a
  SoundFont sampler that answers to GS messages, which is the module's planned
  GM/GS engine (`docs/background.md`). Nuked-SC55 in `sc55` mode is the real
  thing, and the cost shows: about 15 % of an Apple-silicon core for this game
  against 2–4 % for FluidSynth on the same score, the ratio that keeps it out
  of the T113's budget. Where the two differ audibly, the difference is what
  GeneralUser GS gets wrong (or the variation banks it lacks), and that is the
  list to work from if a better bank is ever wanted.
- **Two MAMEs, one destination.** If a MAME is left over from a previous run it
  keeps a dead endpoint and steals your keypresses; `pkill -f 'mame x68000'`
  before starting again.
