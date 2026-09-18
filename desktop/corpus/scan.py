#!/usr/bin/env python3
"""scan.py - what does a real score actually ask an MT-32 for?

`bench/ANALYSIS.md` section 9.1(b) leaves one term of the cost line blank on
purpose:

    armv7-a instructions per output frame = 489.9 + 511.5 x (sounding partials)

-- and says the second factor "is a property of the timbres in Roland's control
ROM and of what the composer wrote", so a repository with no ROMs cannot learn
it. That is half right. The *timbre* half is unknowable here. The *composer*
half is sitting in any MIDI file, and this measures it.

WHAT IT MEASURES, and what each number is worth:

  simultaneous notes      exact, from the file. A note is sounding from its
                          note-on to its note-off (or note-on velocity 0), or
                          to the release of the damper pedal if CC64 was down.
  MT-32 channels only     the same count restricted to the channels a
                          factory-reset MT-32 listens on. Munt's Synth.cpp:897
                          -903 sets chanAssign to {1,2,...,9} at reset, i.e.
                          parts 1-8 on MIDI channels 2-9 and rhythm on channel
                          10; channel 1 and channels 11-16 are ignored
                          entirely. A 16-channel General MIDI file therefore
                          asks a real MT-32 for LESS than it asks a GM module
                          for, and the difference is measured here rather than
                          assumed.
  partial demand          a BOUND, not a measurement: an MT-32 timbre uses one
                          to four partials (Structures.h TimbreParam, four
                          partial blocks; common.partialMute selects which are
                          live), so N sounding notes demand between N and 4N
                          partials, and the machine has 32 (globals.h:97). The
                          interesting question is whether a passage's demand
                          crosses 32, because above that the synthesiser is
                          partial-limited and the cost line is pinned at its
                          maximum whatever the timbres are.

  Percentiles are TIME-WEIGHTED: p50 is the count that half the piece's
  duration is at or below, not the median of a list of events. An untimed
  median would be dominated by the instant around every chord change.

WHAT IT CANNOT MEASURE. How many partials a real MT-32 really allocates, which
needs the real control ROM's timbres. And nothing about sound. See
`corpus/README.md`.

    python3 scan.py FILE...                 one line per file, plus detail
    python3 scan.py --tsv FILE...           machine-readable, one row per file
    python3 scan.py --brief FILE...         one line per file only

SPDX-License-Identifier: 0BSD
"""
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, "ab"))
import midiprep                                    # noqa: E402

# Munt Synth.cpp:897-903: chanAssign = {1,2,...,9} at reset. Zero-based MIDI
# channels 1..9, which are channels 2..10 as a musician numbers them.
MT32_DEFAULT_CHANNELS = frozenset(range(1, 10))
MT32_MAX_PARTIALS = 32                             # mt32emu globals.h:97


def sounding_timeline(events, channels=None, damper=True):
    """[(t_us, count)] -- the number of notes sounding after each change.

    `channels` is a set of zero-based MIDI channels to count, or None for all.
    `damper` honours CC64 as the MT-32 does: a note released while the pedal is
    down keeps sounding until the pedal comes up.
    """
    held = {}                      # (chan, key) -> refcount of note-ons
    pedal_held = set()             # (chan, key) released under the pedal
    down = set()                   # channels whose CC64 is >= 64
    out = []
    count = 0

    def emit(t):
        if out and out[-1][0] == t:
            out[-1] = (t, count)
        else:
            out.append((t, count))

    for t, msg in events:
        st = msg[0]
        if st >= 0xF0:
            continue
        ch = st & 0x0F
        if channels is not None and ch not in channels:
            continue
        kind = st & 0xF0
        if kind == 0x90 and len(msg) > 2 and msg[2] != 0:
            key = (ch, msg[1])
            pedal_held.discard(key)
            held[key] = held.get(key, 0) + 1
            count += 1
            emit(t)
        elif kind == 0x80 or (kind == 0x90 and len(msg) > 2 and msg[2] == 0):
            key = (ch, msg[1])
            if held.get(key):
                if damper and ch in down:
                    pedal_held.add(key)
                else:
                    held[key] -= 1
                    count -= 1
                    if not held[key]:
                        del held[key]
                    emit(t)
        elif kind == 0xB0 and len(msg) > 2:
            cc, val = msg[1], msg[2]
            if cc == 64 and damper:
                if val >= 64:
                    down.add(ch)
                else:
                    down.discard(ch)
                    for key in list(pedal_held):
                        if key[0] != ch:
                            continue
                        pedal_held.discard(key)
                        n = held.pop(key, 0)
                        count -= n
                    emit(t)
            elif cc in (120, 123):             # all sound off / all notes off
                for key in list(held):
                    if key[0] == ch:
                        count -= held.pop(key)
                        pedal_held.discard(key)
                emit(t)
    return out


def weighted_stats(timeline, span_us):
    """(peak, mean, {p: value}) with every count weighted by how long it held."""
    if not timeline:
        return 0, 0.0, {}
    dur = {}
    for i, (t, c) in enumerate(timeline):
        nxt = timeline[i + 1][0] if i + 1 < len(timeline) else span_us
        if nxt > t:
            dur[c] = dur.get(c, 0) + (nxt - t)
    total = sum(dur.values())
    if total <= 0:
        return max(c for _, c in timeline), 0.0, {}
    mean = sum(c * d for c, d in dur.items()) / float(total)
    pct = {}
    acc = 0
    for c in sorted(dur):
        acc += dur[c]
        for p in (50, 90, 95, 99):
            if p not in pct and acc >= total * p / 100.0:
                pct[p] = c
    for p in (50, 90, 95, 99):
        pct.setdefault(p, max(dur))
    return max(c for _, c in timeline), mean, pct


def scan(path):
    data = open(path, 'rb').read()
    if data[:4] == b'MThd':
        events, fmt, ntrks, division = midiprep.parse_smf(data)
        kind = "smf%d" % fmt
    else:
        events, _ = midiprep.parse_raw(data)
        fmt, ntrks, division = -1, 0, 0
        kind = "raw"

    span = events[-1][0] if events else 0
    notes = [m for _, m in events
             if (m[0] & 0xF0) == 0x90 and len(m) > 2 and m[2] != 0]
    chans = sorted(set(m[0] & 0x0F for _, m in events if m[0] < 0xF0))
    progs = sorted(set(m[1] for _, m in events if (m[0] & 0xF0) == 0xC0))
    sysex = [m for _, m in events if m[0] == 0xF0]
    damper_used = any((m[0] & 0xF0) == 0xB0 and len(m) > 2 and m[1] == 64
                      for _, m in events)

    all_tl = sounding_timeline(events)
    mt_tl = sounding_timeline(events, MT32_DEFAULT_CHANNELS)
    nodamp_tl = sounding_timeline(events, MT32_DEFAULT_CHANNELS, damper=False)

    r = {}
    r['path'] = path
    r['bytes'] = len(data)
    r['kind'] = kind
    r['tracks'] = ntrks
    r['division'] = division
    r['seconds'] = span / 1e6
    r['events'] = len(events)
    r['notes'] = len(notes)
    r['notes_per_s'] = len(notes) / (span / 1e6) if span else 0.0
    r['channels'] = chans
    r['mt32_channels'] = [c for c in chans if c in MT32_DEFAULT_CHANNELS]
    r['programs'] = progs
    r['sysex'] = len(sysex)
    r['sysex_bytes'] = sum(len(m) for m in sysex)
    r['sysex_max'] = max([len(m) for m in sysex] or [0])
    r['roland_sysex'] = sum(1 for m in sysex if len(m) > 1 and m[1] == 0x41)
    r['damper'] = damper_used
    for tag, tl in (('all', all_tl), ('mt32', mt_tl), ('nodamp', nodamp_tl)):
        peak, mean, pct = weighted_stats(tl, span)
        r[tag + '_peak'] = peak
        r[tag + '_mean'] = mean
        for p in (50, 90, 95, 99):
            r['%s_p%d' % (tag, p)] = pct.get(p, 0)
    return r


COLS = ['path', 'seconds', 'events', 'notes', 'notes_per_s', 'sysex',
        'sysex_bytes', 'damper', 'all_peak', 'mt32_peak', 'mt32_mean',
        'mt32_p50', 'mt32_p90', 'mt32_p95', 'mt32_p99', 'nodamp_peak']


def fmt_cell(v):
    if isinstance(v, float):
        return "%.2f" % v
    if isinstance(v, bool):
        return "yes" if v else "no"
    return str(v)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    tsv = '--tsv' in argv
    brief = '--brief' in argv
    if not args:
        sys.exit(__doc__)
    rows = [scan(p) for p in args]

    if tsv:
        print("\t".join(COLS))
        for r in rows:
            print("\t".join(fmt_cell(r[c]) if c != 'path'
                            else os.path.basename(r['path']) for c in COLS))
        return 0

    print("%79s" % "-- notes sounding, MT-32 channels --")
    print("%-26s %7s %7s %6s %5s %5s %5s %5s %5s"
          % ("file", "secs", "notes", "note/s", "sysex", "peak", "p99", "p90",
             "p50"))
    for r in rows:
        print("%-26s %7.1f %7d %6.1f %5d %5d %5d %5d %5d"
              % (os.path.basename(r['path']), r['seconds'], r['notes'],
                 r['notes_per_s'], r['sysex'], r['mt32_peak'], r['mt32_p99'],
                 r['mt32_p90'], r['mt32_p50']))
    if brief:
        return 0

    for r in rows:
        lo = r['mt32_peak']
        hi = min(MT32_MAX_PARTIALS, 4 * r['mt32_peak'])
        print()
        print("%s" % r['path'])
        print("  %-22s %s, %d track(s), division %d, %d bytes"
              % ("container", r['kind'], r['tracks'], r['division'],
                 r['bytes']))
        print("  %-22s %.1f s, %d events, %d note-ons (%.1f/s)"
              % ("size", r['seconds'], r['events'], r['notes'],
                 r['notes_per_s']))
        print("  %-22s %s" % ("MIDI channels (1-based)",
                              [c + 1 for c in r['channels']]))
        print("  %-22s %s" % ("of those, heard by a",
                              [c + 1 for c in r['mt32_channels']]))
        print("  %-22s %s" % ("factory-reset MT-32", ""))
        print("  %-22s %d (%d bytes, largest %d, %d Roland)"
              % ("sysex", r['sysex'], r['sysex_bytes'], r['sysex_max'],
                 r['roland_sysex']))
        print("  %-22s %s" % ("damper pedal used", "yes" if r['damper'] else "no"))
        print("  %-22s peak %d, p99 %d, p90 %d, p50 %d, mean %.1f"
              % ("notes sounding (all)", r['all_peak'], r['all_p99'],
                 r['all_p90'], r['all_p50'], r['all_mean']))
        print("  %-22s peak %d, p99 %d, p90 %d, p50 %d, mean %.1f"
              % ("... MT-32 channels", r['mt32_peak'], r['mt32_p99'],
                 r['mt32_p90'], r['mt32_p50'], r['mt32_mean']))
        print("  %-22s peak %d (damper ignored)"
              % ("... sanity check", r['nodamp_peak']))
        print("  %-22s %d..%d partials at the peak%s"
              % ("partial DEMAND bound", lo, hi,
                 "  <-- crosses the MT-32's 32" if 4 * r['mt32_peak'] >= 32
                 else ""))
        print("  %-22s %.0f instructions/frame at %d partials "
              "(bench/ANALYSIS.md 8.4)"
              % ("cost line, upper end", 489.9 + 511.5 * hi, hi))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
