#!/usr/bin/env python3
"""vectors.py - the MIDI byte streams the cross-implementation run compares on.

Three of them are NOT new test data. `demo`, `bank` and `bad` are the bytes
port/host/test.sh and emu/ already assert against, reproduced here so that the
two implementations that read a file (port/host, desktop/) can be given exactly
what the bare-metal image has in .rodata. That is checked rather than assumed:
--verify parses emu/src/midi_vectors.c and compares byte for byte, and
conform.sh refuses to draw a conclusion if they differ.

The fourth, `voice`, is this directory's, and it exists because of a finding:

    With the fabricated ROMs of emu/src/engine_mt32emu_fake_roms.cpp, the
    MT-32's own start-up state is all zeroes -- master volume 0, every part's
    output level 0, no partials reserved to any part, every timbre a zero
    timbre -- so `demo` renders 48000 frames of digital silence. Comparing
    silence between four implementations proves nothing about the
    synthesiser.

`voice` writes a playable machine state over sysex before it plays a note:
the System area (master volume, partial reserve, channel assignment), part 1's
Patch Temporary area (output level, key shift, panning), and part 1's Timbre
Temporary area (one square-wave partial, TVA and TVF wide open). The result is
a note that runs the LA32 wave generator, the TVA and TVF envelopes, the
partial manager and the analog output filter for real, and it renders with a
peak of 9300 out of 32767 rather than 0.

Addresses are MT-32 sysex addresses straight out of Munt's MemoryRegion.h:
System 0x100000, Patch Temp 0x030000, Timbre Temp 0x040000. The timbre layout
is TimbreParam in Structures.h: 14 bytes of common plus 4 x 58 bytes of
partial = 246, which is also why the `bank` vector's payloads are 246 bytes.

    python3 vectors.py OUTDIR            write <name>.syx for every vector
    python3 vectors.py OUTDIR --verify EMU_MIDI_VECTORS_C

SPDX-License-Identifier: 0BSD
"""
import os
import re
import sys


# --- port/host/main.c DEMO[] ------------------------------------------------
def demo():
    out = bytearray([0x90, 60, 100, 64, 100, 67, 100, 72, 100])
    out += bytes([0xF0, 0x41, 0x10, 0x16, 0x12, 0x20, 0x00, 0x00])
    out += b'HELLO MT-32         '
    out += bytes([0x00, 0xF7])
    out += bytes([0xFE])
    out += bytes([0x80, 60, 0, 64, 0, 67, 0, 72, 0])
    return bytes(out)


def roland_sysex(addr, data):
    body = list(addr) + list(data)
    return (bytes([0xF0, 0x41, 0x10, 0x16, 0x12]) + bytes(body)
            + bytes([(-sum(body)) & 0x7f, 0xF7]))


# --- port/host/test.sh case 2 -----------------------------------------------
def bank():
    out = bytearray([0x90, 60, 100, 64, 100, 67, 100])
    for i in range(64):
        out += roland_sysex((0x08, (i * 2) & 0x7f, 0x00),
                            bytes([(j * 7 + i) & 0x7f for j in range(246)]))
        if i % 8 == 0:
            out += bytes([0xFE, 72 + (i % 5), 90])
    out += bytes([0xB0, 123, 0])
    return bytes(out)


# --- port/host/test.sh case 3 -----------------------------------------------
def bad():
    b = bytearray([40, 50, 60])
    b += bytes([0xF0, 0x41, 0x10, 0x16, 0x12]) + bytes(100)
    b += bytes([0x90, 60, 100])
    b += bytes([0xF0]) + bytes([0x7f] * 40000) + bytes([0xF7])
    b += bytes([0x80, 60, 0])
    return bytes(b)


# --- this directory's: a state the fabricated ROMs can actually sound -------
def _timbre():
    """One TimbreParam: partial 1 only, square wave, envelopes wide open."""
    t = bytearray()
    t += b'SQUAREVOIX'                      # common.name[10]
    t += bytes([0,      # partialStructure12: S1, two synth partials
                0,      # partialStructure34
                0x01,   # partialMute: partial 1 only
                0])     # noSustain: normal
    for _ in range(4):
        # wg: coarse, fine, keyfollow, bender, waveform(0=square), pcmWave,
        #     pulseWidth, pulseWidthVeloSens
        t += bytes([36, 50, 11, 1, 0, 0, 50, 7])
        # pitchEnv: depth, veloSens, timeKeyfollow, time[4], level[5]
        t += bytes([0, 0, 0]) + bytes(4) + bytes([50] * 5)
        # pitchLFO: rate, depth, modSens
        t += bytes([0, 0, 0])
        # tvf: cutoff, resonance, keyfollow, biasPoint, biasLevel, envDepth,
        #      envVeloSens, envDepthKeyfollow, envTimeKeyfollow,
        #      envTime[5], envLevel[4]
        t += bytes([100, 0, 11, 0, 7, 0, 0, 0, 0]) + bytes(5) + bytes([100] * 4)
        # tva: level, veloSens, biasPoint1, biasLevel1, biasPoint2, biasLevel2,
        #      envTimeKeyfollow, envTimeVeloSens, envTime[5], envLevel[4]
        t += bytes([100, 50, 0, 0, 0, 0, 0, 0]) + bytes(5) + bytes([100] * 4)
    assert len(t) == 246, len(t)
    return bytes(t)


def voice():
    out = bytearray()
    # System area, all 23 bytes: masterTune, reverbMode, reverbTime,
    # reverbLevel, partial reserve x9, channel assign x9, masterVol.
    # The reserve table is what the fabricated control ROM gets wrong-by-zero:
    # with no partials reserved to any part, nothing sounds however loud it is.
    out += roland_sysex((0x10, 0x00, 0x00),
                        bytes([64, 0, 3, 3]
                              + [32, 0, 0, 0, 0, 0, 0, 0, 0]
                              + [0, 1, 2, 3, 4, 5, 6, 7, 8]
                              + [100]))
    # Patch Temporary area, part 1: timbre group Memory, key shift 0 (=24),
    # fine tune 0 (=50), bender 12, output level 100, pan centre (7).
    out += roland_sysex((0x03, 0x00, 0x00),
                        bytes([2, 0, 24, 50, 12, 0, 0, 0, 100, 7,
                               0, 0, 0, 0, 0, 0]))
    # Timbre Temporary area, part 1.
    out += roland_sysex((0x04, 0x00, 0x00), _timbre())
    out += bytes([0x90, 60, 100])           # and the note
    return bytes(out)


VECTORS = [("demo", demo), ("bank", bank), ("bad", bad), ("voice", voice)]

# The three that the bare-metal image also carries, and can therefore be run on
# all four implementations. `voice` is not among them: emu/src/midi_vectors.c is
# generated into a directory this workstream does not own, so the bare-metal
# image cannot be given a stream that is not already compiled into it.
SHARED = ["demo", "bank", "bad"]


def parse_c_arrays(path):
    """Pull `const uint8_t midi_<name>[] = { 0x.., ... };` out of a C file."""
    text = open(path).read()
    out = {}
    for m in re.finditer(r'const\s+uint8_t\s+midi_(\w+)\[\]\s*=\s*\{(.*?)\};',
                         text, re.S):
        name, body = m.group(1), m.group(2)
        out[name] = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    for name, fn in VECTORS:
        data = fn()
        with open(os.path.join(outdir, name + ".syx"), "wb") as f:
            f.write(data)
        print("%-8s %6d bytes" % (name, len(data)))

    if "--verify" in sys.argv:
        ref_path = sys.argv[sys.argv.index("--verify") + 1]
        if not os.path.exists(ref_path):
            print("VERIFY SKIPPED: no %s" % ref_path)
            return 0
        ref = parse_c_arrays(ref_path)
        bad_ones = 0
        for name in SHARED:
            mine = dict(VECTORS)[name]()
            theirs = ref.get(name)
            if theirs is None:
                print("VERIFY %-8s MISSING from %s" % (name, ref_path))
                bad_ones += 1
            elif theirs != mine:
                print("VERIFY %-8s DIFFERS: %d bytes here, %d there"
                      % (name, len(mine), len(theirs)))
                bad_ones += 1
            else:
                print("VERIFY %-8s identical to the bare-metal image's copy "
                      "(%d bytes)" % (name, len(mine)))
        return 1 if bad_ones else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
