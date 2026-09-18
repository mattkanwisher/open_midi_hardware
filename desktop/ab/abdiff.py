#!/usr/bin/env python3
"""abdiff.py - read a directory of A/B renders and say how they differ.

`conform/compare.py` already answers "is this byte-identical?" across the
implementations of the seam, and this does not duplicate it: it imports its
`pcm()` reader and its `compare_pair()` so that "how many samples differ, from
where, by how much" is computed by exactly one piece of code in this
repository. What it adds is the part an A/B needs and a conformance run does
not:

  * level, per leg -- peak and RMS, in LSB and in dBFS, so a render that is
    quieter, clipped or silent is visible without listening;
  * the RMS of the *difference*, which is the one number that says "these are
    the same sound" or "these are two different sounds". Two renders that
    differ on 100% of samples by 1 LSB and two that differ on 40% by 8000 LSB
    both "differ"; only this separates them;
  * a lag search. A real-time renderer places an event in the block whose
    render call follows it, so its output can be a few milliseconds late
    without being wrong. If shifting one leg by N samples collapses the
    difference, the legs agree and the timing does not -- which is a different
    finding from "the synthesis differs", and must not be reported as one.

Input is a directory of `<leg>.wav`, as `ab.sh` writes it. Nothing here knows
what a leg is; it names them and compares them all against one.

    python3 abdiff.py OUTDIR [--ref LEG] [--lag N] [--strict]

--strict makes any divergence an exit status, which is right for a raw-byte
run (every leg is deterministic, so they must agree) and wrong for an SMF run
(one leg is paced by a wall clock). ab.sh passes it only in the first case.

SPDX-License-Identifier: 0BSD
"""
import hashlib
import math
import os
import sys

# Import compare.py without leaving a __pycache__ behind in conform/: that
# directory is committed, and a byte-cache is not ours to add to it.
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, "conform"))
from compare import pcm, compare_pair          # noqa: E402  (path first)


def level(a):
    """(peak, rms) in LSB over a sample array."""
    peak = 0
    acc = 0
    for v in a:
        if v < 0:
            av = -v
        else:
            av = v
        if av > peak:
            peak = av
        acc += v * v
    rms = math.sqrt(acc / len(a)) if len(a) else 0.0
    return peak, rms


def onset(a, peak):
    """First sample above 2% of this leg's own peak: where the sound starts.

    Unambiguous where a correlation lag is not. Two renders of a held note
    beat against each other, so several shifts fit almost equally well and a
    minimum-cost search picks whichever is a hair lower; the first sample that
    is not silence cannot be mistaken for a different one."""
    thr = max(64, peak // 50)
    for i, v in enumerate(a):
        if v > thr or v < -thr:
            return i
    return -1


def dbfs(x):
    if x <= 0:
        return float("-inf")
    return 20.0 * math.log10(x / 32768.0)


def fmt_db(x):
    return "   -inf" if x == float("-inf") else "%7.2f" % x


def diff_rms(a, b, lag=0):
    """RMS of (a - b shifted by lag), over the overlap."""
    n = min(len(a), len(b) - lag if lag >= 0 else len(b))
    lo = max(0, -lag)
    hi = min(len(a), len(b) - lag)
    if hi <= lo:
        return float("inf"), 0
    acc = 0
    for i in range(lo, hi):
        d = a[i] - b[i + lag]
        acc += d * d
    n = hi - lo
    return math.sqrt(acc / n), n


def best_lag(a, b, span, window=200000, stride=8):
    """Coarse-to-fine search for the shift of b that best explains a.

    Pure Python, so it is deliberately cheap: a subsampled window, coarse
    steps, then one fine pass. It is a diagnostic, not a measurement -- it
    says "about this many samples", and the sample counts it reports are
    computed on the full arrays once the lag is chosen."""
    n = min(len(a), len(b), window)
    if n == 0 or span == 0:
        return 0, None
    ia = a[:n]
    ib = b[:n]

    def cost(lag):
        acc = 0
        cnt = 0
        for i in range(max(0, -lag), min(n, n - lag), stride):
            d = ia[i] - ib[i + lag]
            acc += d * d
            cnt += 1
        return acc / cnt if cnt else float("inf")

    # A held note is periodic, and two of them beat, so shifts by whole
    # periods fit almost as well as the true one and a plain minimum picks
    # whichever came out a hair lower -- on the run this was written against,
    # 7500 samples rather than the 1666 the onsets show. Among the lags within
    # 20% of the best cost, take the SMALLEST: the smallest shift that explains
    # the difference is the only one that can be a scheduling delay. Where that
    # still disagrees with the onset line above, believe the onset line.
    def pick(lags):
        costs = [(lag, cost(lag)) for lag in lags]
        bestc = min(c for _, c in costs)
        near = [lag for lag, c in costs if c <= bestc * 1.20]
        lag = min(near, key=lambda x: (abs(x), x))
        return lag, bestc

    coarse = max(1, span // 64)
    coarse_lags = list(range(-span, span + 1, coarse))
    if 0 not in coarse_lags:
        coarse_lags.append(0)
    best, _ = pick(coarse_lags)
    best, bestc = pick([lag for lag in range(best - coarse, best + coarse + 1)
                        if -span <= lag <= span])
    return best, bestc


def main(argv):
    outdir = argv[1] if len(argv) > 1 else "."
    ref = None
    strict = "--strict" in argv
    lagspan = 24000                       # +/- 0.25 s at 48 kHz, stereo-interleaved
    if "--ref" in argv:
        ref = argv[argv.index("--ref") + 1]
    if "--lag" in argv:
        lagspan = int(argv[argv.index("--lag") + 1])

    wavs = {}
    for fn in sorted(os.listdir(outdir)):
        if fn.endswith(".wav"):
            wavs[fn[:-4]] = pcm(os.path.join(outdir, fn))
    if not wavs:
        print("abdiff: no .wav files in %s" % outdir)
        return 2

    names = list(wavs)
    if ref is None or ref not in wavs:
        ref = names[0]
    others = [n for n in names if n != ref]
    width = max(len(n) for n in names)

    print("=" * 78)
    print("A/B RENDER COMPARISON   reference leg: %s" % ref)
    print("=" * 78)
    print()
    print("  %-*s %9s %8s %9s %8s %9s %10s  %s"
          % (width, "leg", "frames", "rate", "peak", "peak dB", "rms dB",
             "onset s", "sha256/16"))
    onsets = {}
    peaks = {}
    for n in names:
        a, rate, ch = wavs[n]
        peak, rms = level(a)
        o = onset(a, peak)
        onsets[n] = o
        peaks[n] = peak
        print("  %-*s %9d %8d %9d %s %s %10s  %s"
              % (width, n, len(a) // max(ch, 1), rate, peak,
                 fmt_db(dbfs(peak)), fmt_db(dbfs(rms)),
                 "-" if o < 0 else "%.4f" % (o / 2.0 / rate),
                 hashlib.sha256(a.tobytes()).hexdigest()[:16]))

    if len(names) < 2:
        print("\nonly one leg rendered -- nothing to compare against")
        return 0

    failures = 0
    for n in others:
        a = wavs[ref][0]
        b = wavs[n][0]
        m = min(len(a), len(b))
        print()
        print("-- %s  vs  %s   (%d samples in common, %.3f s)"
              % (ref, n, m, m / 2.0 / wavs[ref][1]))
        if wavs[ref][1] != wavs[n][1] or wavs[ref][2] != wavs[n][2]:
            print("   FORMAT MISMATCH: %d Hz/%dch versus %d Hz/%dch -- the "
                  "sample-for-sample comparison below is meaningless"
                  % (wavs[ref][1], wavs[ref][2], wavs[n][1], wavs[n][2]))
            failures += 1
        same, msg = compare_pair(ref, a[:m], n, b[:m])
        if same:
            if peaks[ref] == 0 and peaks[n] == 0:
                # The same trap conform.sh fell into once: two renders of
                # digital silence agree perfectly and mean nothing.
                # FINDINGS.md 8.3.
                print("   IDENTICAL over %d samples -- BUT BOTH ARE SILENT, "
                      "so this compares nothing. With fabricated ROMs a plain "
                      "note-on renders exact silence; use a stream that "
                      "programs the machine first (conform/vectors.py's "
                      "`voice`)." % m)
            else:
                print("   IDENTICAL: every one of %d samples" % m)
            continue
        failures += 1
        print("   %s" % msg)
        if onsets[ref] >= 0 and onsets[n] >= 0:
            d = onsets[n] - onsets[ref]
            print("   onset: %s starts %d samples (%.2f ms) %s"
                  % (n, abs(d), abs(d) / 2.0 / wavs[ref][1] * 1000.0,
                     "later" if d > 0 else "earlier" if d < 0
                     else "at the same sample"))
        r0, cnt = diff_rms(a[:m], b[:m])
        pa = level(a[:m])[1]
        print("   difference RMS %.1f LSB (%s dBFS); reference RMS %.1f LSB "
              "(%s dBFS)" % (r0, fmt_db(dbfs(r0)), pa, fmt_db(dbfs(pa))))
        if pa > 0:
            rel = 20.0 * math.log10(r0 / pa) if r0 > 0 else float("-inf")
            print("   difference is %s dB relative to the reference's own RMS "
                  "(negative = quieter than the signal)" % fmt_db(rel).strip())
        lag, _ = best_lag(a[:m], b[:m], lagspan)
        if lag:
            rl, _ = diff_rms(a[:m], b[:m], lag)
            print("   best alignment: %s by %d samples (%.2f ms); difference "
                  "RMS there %.1f LSB"
                  % ("the other leg is late" if lag > 0 else
                     "the other leg is early", abs(lag),
                     abs(lag) / 2.0 / wavs[ref][1] * 1000.0, rl))
            if r0 > 0 and rl < r0 * 0.5:
                print("   -> shifting explains most of it: this looks like a "
                      "TIMING difference, not a synthesis difference")
        else:
            print("   best alignment is 0 samples: shifting does not explain "
                  "it, so the two renders really are different audio")

    print()
    print("=" * 78)
    if failures:
        print("RESULT: %d leg(s) differ from %s. Each line above says how."
              % (failures, ref))
    else:
        print("RESULT: every leg rendered the same samples as %s." % ref)
    print("=" * 78)
    return 1 if (failures and strict) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
