#!/usr/bin/env python3
"""probe.py - make a real score allocate partials on ROMs that have no timbres.

THE PROBLEM. `bench/ANALYSIS.md` section 8.2 states it exactly: "a synthetic
ROM cannot tell you what a real score costs, because the ROM is what decides
how much work a note is". A fabricated control ROM carries no timbres at all --
every one is zeroes, `common.partialMute` is 0, and a note-on therefore
allocates NO partials. Point `ab_ref --partial-log` at a corpus file with no
ROMs and it will honestly report zero partials for three minutes.

THE SAME TRICK `bench/rtf_synth.cpp` USES, APPLIED TO A REAL STREAM. The Timbre
Temporary Area is RAM, not ROM, and an ordinary Roland DT1 sysex writes it. So
this prepends a preamble to the event stream that gives all eight melodic parts
a timbre of EXACTLY K partials, and then lets the score play. What comes out of
`Synth::getPartialStates()` after that is the real partial manager, with the
real allocation and the real stealing, driven by what a composer actually
wrote.

WHAT THE ANSWER IS AND IS NOT. It is: "if every timbre in this passage used K
partials, the synthesiser would hold this many". It is not: "an MT-32 playing
this file holds this many", because the timbres a real MT-32 would use are
Roland's and are not here. Sweep K from 1 to 4 -- the range TimbreParam allows
-- and the truth for any real timbre set is bracketed. That is a bound, which
is the honest shape of the answer, and it is the same shape `rtf_synth` chose.

TWO CHOICES IN THE PREAMBLE THAT ARE CHOICES, NOT FACTS:

1. **Channel assignment.** A factory MT-32 listens on MIDI channels 2-10 and
   ignores the rest (Munt `Synth.cpp:897-903` sets chanAssign to {1,2,...,9} at
   reset). Game MIDI is written for 16 General MIDI channels, so a plain
   factory mapping throws away whatever is on channels 1 and 11-16. This
   preamble instead assigns the eight melodic parts to the EIGHT BUSIEST
   CHANNELS OF THE FILE, so the measurement is of the music rather than of the
   mapping. `--factory-channels` uses the reset mapping instead, and the
   difference between the two is worth looking at.
2. **The rhythm part is switched off** (chanAssign[8] = 16, which `Synth.cpp:
   2048`'s `if (chan > 15) continue;` treats as unassigned). Rhythm notes take
   their timbres from the Rhythm Setup Area, which indexes ROM rhythm timbres,
   which a fabricated ROM does not have -- so a rhythm part here would
   contribute zero however busy the drum track is. Switching it off and letting
   a melodic part have that channel measures something instead of nothing, and
   says so. On a real MT-32 with real ROMs, drop --probe entirely: the ROM has
   the timbres and none of this is needed.
3. **Program changes are stripped from the score.** This one was found by
   running it: with them left in, every corpus file reported exactly zero
   partials for its whole length. A program change makes `Part::setProgram`
   call `resetTimbre`, which copies the timbre named by the patch's group and
   number back over Timbre Temp -- and on a fabricated ROM that timbre is all
   zeroes, `partialMute` included. So the first program change in the file
   silently undid the preamble. Game MIDI sends one on every channel in its
   first bar, which is why the answer was zero and not merely low. On a real
   MT-32 a program change selects a real timbre and must NOT be stripped;
   that is what `--keep-programs` is for, and with real ROMs you want it.
4. **The partial reserve table** is 32 partials split evenly over the eight
   melodic parts (4 each). A real MT-32 takes it from the control ROM
   (`Synth.cpp:897`, `memcpy(... controlROMMap->reserveSettings ...)`) and a
   game usually overwrites it anyway. It shapes WHICH note gets stolen when
   demand crosses 32; it does not change that demand crosses 32.

    python3 probe.py IN.evt OUT.evt --partials K [--factory-channels]
                                          [--keep-programs]
    python3 probe.py SOME.mid OUT.evt --partials K

SPDX-License-Identifier: 0BSD
"""
import os
import sys

sys.dont_write_bytecode = True
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, os.pardir, "ab"))
sys.path.insert(0, os.path.join(_HERE, os.pardir, "conform"))
import midiprep                                         # noqa: E402
from vectors import roland_sysex                        # noqa: E402

PARTS = 8                  # melodic parts; the rhythm part is the ninth
TIMBRE_BYTES = 246         # TimbreParam: 14 common + 4 x 58 (Structures.h)
PATCH_BYTES = 16           # MemParams::PatchTemp


def addr(msb, offset):
    """Roland three-byte address: seven bits per byte."""
    return (msb + (offset >> 14), (offset >> 7) & 0x7F, offset & 0x7F)


def timbre(npartials):
    """conform/vectors.py's `_timbre`, with partialMute opened to K partials.

    Everything else is deliberately plain -- flat pitch envelope, no LFO, TVF
    envelope depth zero, TVA straight to sustain -- for the reason
    bench/ANALYSIS.md 9.2 gives: plain timbres make envelope stage transitions
    rare, which biases an instruction count slightly low. Here we are counting
    partials, not instructions, and a partial is allocated or it is not.
    """
    if not 1 <= npartials <= 4:
        raise ValueError("a TimbreParam has four partial blocks; "
                         "K must be 1..4, not %r" % npartials)
    t = bytearray()
    t += b'PROBE%d    ' % npartials             # common.name[10]
    t += bytes([0,                              # partialStructure12: two synth
                0,                              # partialStructure34
                (1 << npartials) - 1,           # partialMute: K partials live
                0])                             # noSustain: normal
    for _ in range(4):
        t += bytes([36, 50, 11, 1, 0, 0, 50, 7])
        t += bytes([0, 0, 0]) + bytes(4) + bytes([50] * 5)
        t += bytes([0, 0, 0])
        t += bytes([100, 0, 11, 0, 7, 0, 0, 0, 0]) + bytes(5) + bytes([100] * 4)
        t += bytes([100, 50, 0, 0, 0, 0, 0, 0]) + bytes(5) + bytes([100] * 4)
    assert len(t) == TIMBRE_BYTES, len(t)
    return bytes(t)


def busiest_channels(events, n=PARTS):
    """The n MIDI channels with the most note-ons, in descending order."""
    count = {}
    for _, m in events:
        if (m[0] & 0xF0) == 0x90 and len(m) > 2 and m[2]:
            count[m[0] & 0x0F] = count.get(m[0] & 0x0F, 0) + 1
    order = sorted(count, key=lambda c: (-count[c], c))
    return order[:n], count


def preamble(channels, npartials):
    """The sysex that turns a machine with no timbres into one with K-partial
    timbres on every melodic part. Order is load-bearing and is
    bench/ANALYSIS.md 8.2's: System, then Patch Temp, then Timbre Temp --
    a Patch Temp write calls Part::resetTimbre() and would erase a timbre
    written before it."""
    out = bytearray()

    chan_assign = list(channels) + [16] * (PARTS - len(channels)) + [16]
    reserve = [32 // PARTS] * PARTS + [0]
    out += roland_sysex(addr(0x10, 0),
                        bytes([64, 0, 3, 3] + reserve + chan_assign + [100]))

    for p in range(PARTS):
        out += roland_sysex(addr(0x03, p * PATCH_BYTES),
                            bytes([2, 0, 24, 50, 12, 0, 0, 0, 100, 7,
                                   0, 0, 0, 0, 0, 0]))
    tb = timbre(npartials)
    for p in range(PARTS):
        out += roland_sysex(addr(0x04, p * TIMBRE_BYTES), tb)
    return bytes(out)


def read_events(path):
    data = open(path, 'rb').read()
    if data[:4] == b'MThd':
        ev, _, _, _ = midiprep.parse_smf(data)
        return midiprep.quantise(ev)
    # An .evt as ab/midiprep.py writes it: a comment line, or "<us> <hex>".
    # Tested explicitly rather than by "does it look like text", because a raw
    # MIDI capture is arbitrary bytes and must not be guessed at.
    first = data.split(b'\n', 1)[0]
    is_evt = first.startswith(b'#') or (
        len(first.split()) == 2 and first.split()[0].isdigit()
        and all(c in b'0123456789abcdefABCDEF' for c in first.split()[1]))
    if is_evt:
        ev = []
        for line in open(path):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            t, hx = line.split(None, 1)
            ev.append((int(t), bytes.fromhex(hx)))
        return ev
    ev, _ = midiprep.parse_raw(data)
    return midiprep.quantise(ev)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    if len(args) < 2:
        sys.exit(__doc__)
    src, dst = args[0], args[1]
    k = 1
    if '--partials' in argv:
        k = int(argv[argv.index('--partials') + 1])
    factory = '--factory-channels' in argv
    keep_programs = '--keep-programs' in argv

    events = read_events(src)
    stripped = 0
    if not keep_programs:
        kept = []
        for t, m in events:
            if (m[0] & 0xF0) == 0xC0:
                stripped += 1
                continue
            kept.append((t, m))
        events = kept
    busy, counts = busiest_channels(events)
    if factory:
        # Munt Synth.cpp:897-903: chanAssign = {1,2,...,9} at reset. The rhythm
        # part keeps channel 10 here, and will contribute nothing; that is the
        # point of showing the two mappings side by side.
        chans = list(range(1, 9))
    else:
        chans = sorted(busy)

    head = preamble(chans, k)
    pre = midiprep.parse_raw(head)[0]

    with open(dst, 'w') as f:
        f.write("# corpus/probe.py from %s\n" % os.path.abspath(src))
        f.write("# %d-partial timbre on parts 1-%d; rhythm part off\n"
                % (k, PARTS))
        f.write("# parts 1-%d on MIDI channels %s (1-based)\n"
                % (PARTS, [c + 1 for c in chans]))
        f.write("# note-ons per channel: %s\n"
                % {c + 1: counts[c] for c in sorted(counts)})
        f.write("# %d program change(s) %s\n"
                % (stripped, "stripped: on a fabricated ROM each one would "
                             "reset the timbre to all zeroes and silence the "
                             "part" if stripped else "kept"))
        span = events[-1][0] if events else 0
        f.write("# events %d  span_us %d\n" % (len(pre) + len(events), span))
        for _, m in pre:
            f.write("0 %s\n" % m.hex())
        for t, m in events:
            f.write("%d %s\n" % (t, m.hex()))

    dropped = sum(n for c, n in counts.items() if c not in chans)
    total = sum(counts.values())
    print("probe: %d-partial timbres, parts 1-%d on channels %s"
          % (k, PARTS, [c + 1 for c in chans]))
    print("  %-22s %d sysex, %d bytes" % ("preamble", len(pre), len(head)))
    print("  %-22s %d of %d note-ons are on a channel no part is listening to"
          % ("not heard", dropped, total))
    if dropped:
        print("  %-22s %s"
              % ("", "channels " + str(sorted(c + 1 for c in counts
                                              if c not in chans))
                 + " -- the MT-32 has eight melodic parts and this file "
                   "uses more channels than that"))
    if stripped:
        print("  %-22s %d program change(s) removed; on fabricated ROMs each "
              "one resets the" % ("stripped", stripped))
        print("  %-22s part's timbre to zeroes and the part stops sounding"
              % "")
    print("  wrote %s" % dst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
