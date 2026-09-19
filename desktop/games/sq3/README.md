# Space Quest III → mt32-t113

The first real game through the port. SQ3 (Sierra, 1989, SCI0) ships an
`MT32.DRV` that was written for the actual hardware: on start-up it uploads a
patch bank, a set of timbres and a "Space Quest III" display message as a burst
of Roland sysex, and then plays the score through an MPU-401. DOSBox-X emulates
the MPU-401 and forwards every byte to a CoreMIDI destination; `mt32-desktop
--midi-seq` publishes one called `mt32-t113`. Nothing in between is ours except
the name.

```
SCIV.EXE + MT32.DRV ──port 330h──▶ DOSBox-X MPU-401 ──CoreMIDI──▶ mt32-desktop ──▶ mtp_midi_parser ──▶ engine ──▶ speakers
                                                                                   (same source as the T113)
```

## Run it

```sh
cmake -S desktop -B desktop/build && cmake --build desktop/build -j
brew install dosbox-x                     # once
./desktop/games/sq3/run.sh
```

The game is expected at `old_games/SQ3/` (gitignored; set `SQ3_DIR` to point
elsewhere). ROMs are looked for in `~/mt32roms` or `$MT32_ROMS`; without them
the fake engine plays a sine stand-in, which is enough to hear that notes
arrive and for every counter below to be real. Extra arguments go to
`mt32-desktop`, so `--tap-wav sq3.wav` keeps the audio.

Close the DOSBox-X window, or type `exit` at the prompt after the game, and the
script stops the synth and prints its summary.

## What the counters said, 2026-09-19

140 s from the title screen through the Arcada intro and into the first rooms,
fake engine, Apple silicon:

```
midi bytes           32536
messages             6135 short, 314 sysex, 0 realtime
midi fifo peak       486 bytes
midi fifo overruns   0
parse: orphan data   0
parse: sysex > 32 kB 0
parse: sysex aborted 0
engine back-pressure 0
underruns            2
```

The 314 sysex are the driver's start-up upload (about 14 kB in the first five
seconds — watch the `sx` column in the status line climb) and the patch changes
that follow each room. All of them arrived intact through CoreMIDI's packet
boundaries and DOSBox-X's own sysex buffering. The two underruns were a 26 ms
scheduler stall at 29 s on a laptop doing other things; ring occupancy never
went below 1 of 8, so the ring absorbed it as designed and the second block
was what spilled.

## Files

| | |
|---|---|
| `run.sh` | start synth, wait for the destination, launch DOSBox-X, print the summary |
| `dosbox-x.conf` | EGA, 4000 cycles, `mididevice=coremidi midiconfig=mt32-t113`, no Sound Blaster, no quit prompt |
| `RESOURCE.CFG` | what Sierra's installer would write: `soundDrv=MT32.DRV`. Copied into the game directory if it has none |

## Things worth knowing

- **Intelligent-mode MPU-401 is required.** `MT32.DRV` initialises the MPU in
  intelligent mode before switching it to UART mode and prints `MPU INIT ERROR`
  if that handshake fails. `mpu401=uart` breaks it.
- **No Sound Blaster on purpose.** `sbtype=none` means the only route to the
  speakers is the one under test.
- **The Dracula disks in `old_games/` are X68000 images** — a different machine
  with its own YM2151 and no MIDI, so they are not part of this.
- **This says nothing about the Cortex-A7.** The `rtf` line in the summary is a
  desktop number; the gate is `bench/rtf` on silicon.
