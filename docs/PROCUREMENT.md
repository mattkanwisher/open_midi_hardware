# Things to buy

Nothing here is ordered. This is the list, with what each purchase unblocks, so
it can be acted on in one sitting. Prices are September 2026 readings from
`hw/HARDWARE.md`; re-quote before ordering, and note that DRAM-bearing parts are
moving 10–20 % a month.

## 1. The gate: any T113 board, today

The real-time-factor measurement does not care which T113 variant it runs on —
the cores are the same dual Cortex-A7 at 1.2 GHz. **Do not wait for a T113-i
part to close the gate.** The cheapest board that boots Linux answers it.

| Item | Why | Est. |
|---|---|---|
| A T113-S3 board — DongshanPI, MangoPi MQ-R, 100ask | Run `bench/rtf` on real A7 silicon. This is the whole project's go/no-go | $15–25 |
| microSD, 8 GB+ | Boot media | $5 |
| USB-C cable, and a 3.3 V USB-serial adapter | Console, and FEL recovery | $10 |

## 2. The T113-i path: SoM plus its dev board

Workstream B's recommendation is a vendor module on our own carrier rather than
our own BGA layout, because the module vendor has already answered the two
questions we cannot read: the power-up sequence and the DDR3 address/command
routing (`hw/HARDWARE.md` §§ 2.1, 5.4).

| Item | Why | Est. |
|---|---|---|
| **Forlinx FET113i-S SoM** (T113-i, 256 MB DDR3, 256 MB NAND) | The module our carrier would host | $15–23 |
| **Forlinx OK113i-S dev board** | Proves the module and ships a working U-Boot with a known-good DRAM parameter set and AC remapping table | $62–75 |
| Alternative: MYIR MYC-YT113i SoM (512 MB or 1 GB) and MYD-YT113i board | Second source; MYIR's own SoM is 6-layer, which is evidence for our carrier | $19.80–31.80 / board TBD |

Buy the SoM **and** its dev board together. The board is what makes the module
useful before our carrier exists, and it is the artefact that settles the
remapping question — one `xfel read32 0x03006228` on arrival.

## 3. Audio and MIDI front end, for the bench

| Item | Why | Est. |
|---|---|---|
| PCM5102A module (or the bare IC, C107671) | I²S DAC, the output path | $2–4 |
| H11L1S optocoupler (C78589) ×2 | MIDI in. Schmitt output, in spec at 3.3 V. **Note the output is pin 4, not pin 6** — `hw/HARDWARE.md` § 3.3 said pin 6 and was wrong | $0.50 |
| BSS138 ×2, and a 74LVC1G07 (non-inverting open-drain — **not** the 1G06) | The daughterboard's 5 V→3.3 V MIDI shifter and the THRU driver. Lets both front ends be breadboarded before a carrier exists | $0.30 |
| MIDI DIN breakout or a game-port MIDI cable | Feed it from a real DOS machine | $10 |

## 3b. Driving it from a retro machine, for listening tests

Not a purchase, but it belongs beside them: the intended way to hear this
against other MT-32 implementations is to drive it from an emulator over real
MIDI. Established 2026-09-18 by reading each project's own source;
`desktop/README.md` has the detail and the citations.

| | |
|---|---|
| **X68000** | **Use MAME** (`-exp1 x68k_midi`), **not px68k.** Note MAME **cannot record MIDI to a file** — `create_output()` calls PortMidi's `Pm_OpenOutput()`, a device stream, with no file path anywhere. So with MAME you must route to a real port and record *from that port* (`arecordmidi`/`aseqdump`). DOSBox captures to an SMF by itself, which is one moving part fewer. px68k emulates the CZ-6BM1 board, but its Win32 shim makes `midiOutOpen` return failure and `midiOutShortMsg`/`midiOutLongMsg` no-ops — so on Linux **it discards every MIDI byte**. Confirmed in `win32api/fake.c` |
| **DOSBox Staging** | `mididevice = port` plus `midiconfig = <client:port>`. **Not `alsa`** — that value is deprecated and silently rewritten. It can also capture the stream to an SMF (Ctrl-Alt-F6), which is the cleanest feed into the A/B rig |
| **DOSBox-X** | `mididevice = alsa` here — a different project with a different parser. It captures too, but the capture key is unbound by default |
| **ScummVM** | `-e alsa` with `SCUMMVM_PORT=<client:port>` |

**A capture host needs a real MIDI port.** MAME and PCem write to a device, not a
file, so a machine with no ALSA sequencer cannot capture from them at all —
verified the hard way: MAME 0.264 installed cleanly in this project's container
and reports `open /dev/snd/seq failed … No MIDI ports were found`, and the
kernel here has no sound support to add. Use a normal desktop Linux box, or use
DOSBox's own capture.

Capture the stream once, then render it offline with `desktop/ab.sh`. A live A/B
is not repeatable — see `docs/PLAN.md` § 0.5.

## 4. Not purchasable

**MT-32 or CM-32L ROM dumps.** Roland's, and the gate cannot be closed without
them — `bench/rtf` refuses to run otherwise, and mt32emu checks them by SHA-1, so
an approximation will not do. Dump your own from hardware you own. These never
enter this repository.

## On arrival, in order

1. `xfel read32 0x03006228` — bits [11:8] are the AC remapping selector. 13 or 14
   means stock mainline works with no patch (`hw/HARDWARE.md` § 5.4).
2. **Test the audio PLL inference before anything else that matters**, because
   it is the one unknown with a copper consequence. `port/t113` prints its
   assumptions at boot; if PLL_AUDIO0 does not lock at 24.576 MHz, the fallback
   is *not* 32 kHz (same family) — see `docs/PLAN.md` § 4. One `xfel write32`
   retries a different pattern word.
3. `bench/rtf` on the busiest MT-32 score you have, pinned to one core, governor
   at performance, at 32 kHz and again at 48 kHz.
4. The four hardware PDFs (`hw/HARDWARE.md` § 7 items 1 and 7) from a network
   that can reach whycan, 100ask and Forlinx — this session cannot.
5. **Read the SoM's hardware manual into `hw/ref/`** — specifically its pin-mux
   table and its power-sequencing section. Rows 1–7 of `hw/CARRIER.md` § 3 are
   the whole of what stands between that document and a schematic, and every one
   of them ships in the same box as the dev board in § 2 above.
