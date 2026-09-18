#!/bin/sh
# partials.sh - how many partials does real music actually hold?
#
#   ./desktop/corpus/partials.sh                 # every corpus file, K = 1..4
#   ./desktop/corpus/partials.sh map24 rolling   # just these
#   K="2 4" ./desktop/corpus/partials.sh         # just these timbre sizes
#
# THE QUESTION. bench/ANALYSIS.md section 9.1(b): a real score's cost is
# `fixed + per-partial x (partials that passage sounds)`, and "this repository
# cannot learn the second factor: it is a property of the timbres in Roland's
# control ROM and of what the composer wrote". The timbre half is still
# unknowable here. The composer half is not, and this measures it.
#
# HOW. For each file and each K in 1..4 -- the number of partials one MT-32
# timbre can use -- corpus/probe.py prepends a sysex preamble that gives all
# eight melodic parts a timbre of exactly K partials, and ab_ref renders the
# whole piece while sampling Synth::getPartialStates() every 128 frames. The
# real partial manager allocates and steals; nothing is modelled.
#
# WHAT COMES OUT IS A BRACKET, NOT A NUMBER. Read corpus/probe.py's header
# before quoting any of it, and corpus/README.md before drawing a conclusion.
#
# SPDX-License-Identifier: 0BSD

set -e
cd "$(dirname "$0")"
HERE=$(pwd)
DESK=$HERE/..
REF=$DESK/build/ab/ab_ref
WORK=$DESK/build/ab/partials
RATE=${RATE:-48000}
EVERY=${EVERY:-128}
K=${K:-1 2 3 4}
TAIL=${TAIL:-2}

names="$*"
if [ -z "$names" ]; then
    names=$(grep -v '^#' "$HERE/MANIFEST.tsv" | tail -n +2 | cut -f1)
fi

[ -x "$REF" ] || {
    echo "building ab_ref first"
    "$DESK/ab.sh" --build-only > "$DESK/build/ab/partials-build.log" 2>&1 || {
        tail -20 "$DESK/build/ab/partials-build.log"; exit 2; }
}
[ -x "$REF" ] || { echo "no $REF"; exit 2; }

mkdir -p "$WORK"
TSV=$WORK/summary.tsv
printf 'file\tK\tseconds\tsamples\tpeak\tmean\tp50\tp90\tp99\tat_ceiling_pct\tinstr_per_frame_peak\n' > "$TSV"

printf '%-10s %2s %7s %5s %6s %4s %4s %4s %7s\n' \
       file K secs peak mean p50 p90 p99 "at 32"
printf '%s\n' "----------------------------------------------------------------"

for n in $names; do
    mid=$HERE/files/$n.mid
    [ -f "$mid" ] || { echo "$n: not fetched -- run ./desktop/corpus/fetch.sh"; continue; }
    for k in $K; do
        evt=$WORK/$n-p$k.evt
        python3 "$HERE/probe.py" "$mid" "$evt" --partials "$k" > "$WORK/$n-p$k.probe.txt"
        span=$(sed -n 's/^# events [0-9]* *span_us \([0-9]*\)/\1/p' "$evt")
        [ -n "$span" ] || span=0
        frames=$(python3 -c "print(int($span * $RATE // 1000000) + $TAIL * $RATE)")
        "$REF" --events "$evt" --wav "$WORK/$n-p$k.wav" \
               --engine mt32emu-fakerom --rate $RATE --frames "$frames" \
               --partials 32 --partial-every $EVERY \
               --partial-log "$WORK/$n-p$k.tsv" \
               > "$WORK/$n-p$k.txt" 2>&1 || true
        rm -f "$WORK/$n-p$k.wav"        # 100 MB a piece, and nobody can hear it
        python3 - "$WORK/$n-p$k.txt" "$n" "$k" "$span" "$TSV" <<'PY'
import re, sys
txt, name, k, span, tsv = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
s = open(txt).read()
def g(pat, d="0"):
    m = re.search(pat, s, re.M)
    return m.group(1) if m else d
peak = g(r"^partials peak\s+(\d+)")
mean = g(r"^partials mean\s+([\d.]+)")
p = re.search(r"^partials p50 p90 p99\s+(\d+) (\d+) (\d+)", s, re.M)
p50, p90, p99 = p.groups() if p else ("0", "0", "0")
samples = g(r"^partials sampled\s+(\d+)")
ceil = g(r"^partials at the ceiling.*\(([\d.]+)%\)")
secs = float(span) / 1e6
instr = 489.9 + 511.5 * float(peak)
open(tsv, "a").write("\t".join([name, k, "%.1f" % secs, samples, peak, mean,
                                p50, p90, p99, ceil, "%.0f" % instr]) + "\n")
print("%-10s %2s %7.1f %5s %6s %4s %4s %4s %6s%%"
      % (name, k, secs, peak, mean, p50, p90, p99, ceil))
PY
    done
done

echo
echo "per-sample logs and this table: $WORK"
cat <<'NOTE'

WHAT THESE NUMBERS ARE. For each file, with every melodic part given a timbre
of exactly K partials, the count Synth::getPartialStates() reported every 128
frames over the whole piece. The allocation and the stealing are the library's.

WHAT THEY ARE NOT. Not what an MT-32 playing this file would hold, because the
timbres an MT-32 would pick live in Roland's control ROM. K = 1 is the floor
for any timbre set, K = 4 the ceiling; the truth for a real score sits between
the two rows, nearer the top for anything with layered or PCM timbres.

And not a claim about sound. Nothing in this repository has ever made an MT-32
noise; see corpus/README.md.
NOTE
