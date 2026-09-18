#!/usr/bin/env python3
"""compare.py - does every implementation of the seam render the same audio?

Reads a directory of runs produced by conform.sh, one pair of files per run:

    <impl>__<engine>__<vector>.wav      the PCM that implementation committed
    <impl>__<engine>__<vector>.txt      everything it printed

and answers two questions for each (engine, vector) pair:

 1. Is the rendered PCM identical, sample for sample, across implementations?
    Compared over the common prefix, because the run length is not part of the
    contract: port/host renders as fast as it can and overshoots the block
    budget by up to seven blocks, while a paced sink stops exactly on it. What
    IS part of the contract is that sample N is the same number everywhere.

 2. Do the counters that cannot depend on the platform agree? Those are the
    parser's -- bytes in, messages out, each error class -- and the engine
    back-pressure count. The ones that legitimately differ (wall time, blocks
    committed, underruns, worst render time, minimum ring occupancy) are
    printed side by side and never asserted on: an underrun on a desktop under
    load says something about the desktop, not about the port.

A difference is a finding, not a failure to hide. Where the PCM differs this
prints where, by how much, and over how many samples, because "the last 12 bits
of every sample differ by one" and "the second note is missing" are different
bugs.

SPDX-License-Identifier: 0BSD
"""
import array
import hashlib
import os
import re
import sys
import wave

# Counters that must be identical on every implementation: they are computed by
# port/src/mtp_midi_parser.c and port/src/mtp_render.c, which are the same
# translation units everywhere. If one of these differs, the platform layer
# under them is losing or duplicating bytes.
INVARIANT = [
    ("midi bytes",       r'^midi bytes\s+(\d+)'),
    ("short messages",   r'^short messages\s+(\d+)'),
    ("sysex messages",   r'^sysex messages\s+(\d+)'),
    ("realtime",         r'^parser: short \d+ sysex \d+ realtime (\d+)'),
    ("orphan data",      r'^parser: orphan data (\d+),'),
    ("sysex truncated",  r'^parser: orphan data \d+, sysex truncated (\d+),'),
    ("sysex aborted",    r'^parser: orphan data \d+, sysex truncated \d+, sysex aborted (\d+)'),
    ("back-pressure",    r'^engine back-pressure\s+(\d+)'),
]

# Reported, never asserted on.
PLATFORM = [
    ("blocks",           r'^blocks committed\s+(\d+)'),
    ("underruns",        r'^underruns\s+(\d+)'),
    ("worst render us",  r'^worst render\s+(\d+)'),
    ("min ring",         r'^min ring occupancy\s+(\d+)'),
]


def counters(path, table):
    text = open(path, errors="replace").read()
    out = {}
    for name, pattern in table:
        m = re.search(pattern, text, re.M)
        out[name] = m.group(1) if m else "-"
    return out


def pcm(path):
    with wave.open(path) as w:
        a = array.array('h')
        a.frombytes(w.readframes(w.getnframes()))
        return a, w.getframerate(), w.getnchannels()


def compare_pair(name_a, a, name_b, b):
    """Returns (identical, message)."""
    n = min(len(a), len(b))
    if n == 0:
        return False, "one of the two runs produced no samples at all"
    diffs = [i for i in range(n) if a[i] != b[i]]
    if not diffs:
        return True, ""
    worst = max(abs(a[i] - b[i]) for i in diffs)
    first = diffs[0]
    return False, ("%d of %d samples differ (%.4f%%), first at sample %d "
                   "(%d vs %d), largest difference %d LSB"
                   % (len(diffs), n, 100.0 * len(diffs) / n, first,
                      a[first], b[first], worst))


def main(outdir):
    runs = {}
    for fn in sorted(os.listdir(outdir)):
        if not fn.endswith(".txt"):
            continue
        impl, engine, vector = fn[:-4].split("__")
        runs.setdefault((engine, vector), []).append(impl)

    failures = 0
    print("=" * 78)
    print("CROSS-IMPLEMENTATION CONFORMANCE  --  docs/PLAN.md 0.5 rule 2")
    print("=" * 78)

    for (engine, vector), impls in sorted(runs.items()):
        print()
        print("### engine %-16s vector %-8s  (%d implementations)"
              % (engine, vector, len(impls)))

        # ---- counters -------------------------------------------------
        inv = {i: counters(os.path.join(outdir, "%s__%s__%s.txt"
                                        % (i, engine, vector)), INVARIANT)
               for i in impls}
        plat = {i: counters(os.path.join(outdir, "%s__%s__%s.txt"
                                         % (i, engine, vector)), PLATFORM)
                for i in impls}

        width = max(max(len(i) for i in impls), len("platform"))
        print("  %-*s %s" % (width, "counter", "".join(
            "%18s" % n for n, _ in INVARIANT)))
        for i in impls:
            print("  %-*s %s" % (width, i, "".join(
                "%18s" % inv[i][n] for n, _ in INVARIANT)))

        ref = impls[0]
        for i in impls[1:]:
            for n, _ in INVARIANT:
                if inv[i][n] != inv[ref][n]:
                    print("  COUNTER MISMATCH: %s = %s on %s but %s on %s"
                          % (n, inv[i][n], i, inv[ref][n], ref))
                    failures += 1

        print("  %-*s %s" % (width, "platform", "".join(
            "%18s" % n for n, _ in PLATFORM)))
        for i in impls:
            print("  %-*s %s" % (width, i, "".join(
                "%18s" % plat[i][n] for n, _ in PLATFORM)))

        # ---- PCM ------------------------------------------------------
        wavs = {}
        for i in impls:
            p = os.path.join(outdir, "%s__%s__%s.wav" % (i, engine, vector))
            if os.path.exists(p):
                wavs[i] = pcm(p)
        if len(wavs) < 2:
            print("  PCM: fewer than two implementations produced a wav")
            continue

        names = list(wavs)
        n = min(len(wavs[i][0]) for i in names)
        peak = max(max((abs(v) for v in wavs[i][0][:n]), default=0)
                   for i in names)
        nonzero = sum(1 for v in wavs[names[0]][0][:n] if v)
        print("  PCM: %d samples compared (%.3f s), peak %d, %d non-zero"
              % (n, n / 2.0 / wavs[names[0]][1], peak, nonzero))
        for i in names:
            a = wavs[i][0]
            print("       %-*s %7d samples  sha256 %s"
                  % (width, i, len(a),
                     hashlib.sha256(a[:n].tobytes()).hexdigest()[:32]))
        allsame = True
        for i in names[1:]:
            same, msg = compare_pair(names[0], wavs[names[0]][0],
                                     i, wavs[i][0])
            if not same:
                allsame = False
                print("  PCM DIVERGENCE  %s vs %s: %s" % (names[0], i, msg))
                failures += 1
        if allsame:
            if peak == 0:
                print("  PCM: identical on all %d -- but every sample is zero, "
                      "so this compares silence. See FINDINGS.md 8.3."
                      % len(names))
            else:
                print("  PCM: byte-identical on all %d implementations"
                      % len(names))

    print()
    print("=" * 78)
    if failures:
        print("RESULT: %d divergence(s). Read them above; each one is a finding."
              % failures)
    else:
        print("RESULT: every implementation agreed, on every counter and every "
              "sample.")
    print("=" * 78)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
