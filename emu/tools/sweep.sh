#!/bin/sh
# sweep.sh - the experiment port/DESIGN.md section 2.2 asks for.
#
#   "The block size and ring depth are the only two numbers in this design that
#    should move in response to a measurement, and the harness in host/ takes
#    both as command-line arguments so the experiment is one command."
#
# This is that one command, run against a Cortex-A7 booting from reset instead
# of against a host process, because port/host's sink cannot underrun outside
# --realtime (host_audio_wav.c:107) and therefore cannot answer the question.
# Here the deadline is a generic-timer interrupt that arrives whether or not
# the renderer is ready.
#
# READ THIS BEFORE READING ANY NUMBER BELOW IT.
#
# QEMU's TCG models neither the A7 pipeline nor its caches. Every microsecond
# printed here is an instruction count divided by a constant. What the sweep
# measures is therefore NOT how fast a T113 is -- it is the *shape* of the
# design's margin: how much slower than the block clock the renderer can get,
# at each (block, depth) pair, before the ring runs dry. That shape is a
# property of the ring discipline and of the loop, and it is the same shape on
# silicon. The scale is fictional and is set by -icount.
#
# -icount shift=N makes the guest a deterministic machine of roughly
# 2^(30-N)/1e6 MIPS: guest microseconds come from retired instructions, not
# from the host clock. Doubling N halves the fictional machine's speed, which
# is exactly a knob for "what if workstream A's real-time factor comes back
# twice as bad as we hoped". emu/FINDINGS.md 8.7 shows the render time doubling
# exactly with N, which is the proof that this is what the knob does.
#
# Usage:
#   tools/sweep.sh                      the (block, ring) surface at shift 2
#   tools/sweep.sh --shift 4            the same surface on a 4x slower machine
#   tools/sweep.sh --rtf                the RTF sweep: how slow before it breaks
#   tools/sweep.sh --cliff              the same, in fine steps around RTF 1
#   tools/sweep.sh --engine fake        cheaper engine, same structure
#
# SPDX-License-Identifier: 0BSD

set -e
cd "$(dirname "$0")/.."

QEMU=${QEMU:-qemu-system-arm}
ELF=${ELF:-build/mt32emu-bare.elf}
OUT=build/sweep
TIMEOUT=${TIMEOUT:-300}

ENGINE=mt32emu-fakerom
MIDI=bank
SECONDS_=6
SHIFT=2
MODE=surface
BLOCKS=${BLOCKS:-"32 64 128 256 512"}
RINGS=${RINGS:-"2 3 4 6 8"}
SHIFTS=${SHIFTS:-"0 1 2 3 4 5 6"}

while [ $# -gt 0 ]; do
    case "$1" in
        --engine)  ENGINE=$2; shift 2 ;;
        --midi)    MIDI=$2;   shift 2 ;;
        --seconds) SECONDS_=$2; shift 2 ;;
        --shift)   SHIFT=$2;  shift 2 ;;
        --rtf)     MODE=rtf;  shift ;;
        --cliff)   MODE=cliff; shift ;;
        --surface) MODE=surface; shift ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
done

[ -f "$ELF" ] || { echo "build first: make mt32emu"; exit 2; }
mkdir -p "$OUT"

QBASE="-M virt -cpu cortex-a7 -m 256 -nographic -nodefaults -serial mon:stdio -no-reboot"

# run <logfile> <icount-shift> <args...>
run() {
    log=$1; sh_=$2; shift 2
    args="arg=sweep"
    for a in "$@"; do args="$args,arg=$a"; done
    timeout "$TIMEOUT" $QEMU $QBASE -icount shift=$sh_ \
        -semihosting-config "enable=on,target=native,$args" \
        -kernel "$ELF" 2>&1 | tr -d '\r' > "$log" || true
}

field() { grep -E "$2" "$1" | head -1 | awk '{print $NF}'; }

if [ "$MODE" = surface ]; then
cat <<EOF
# (block, ring) surface -- $ENGINE, --midi $MIDI --realtime, ${SECONDS_}s,
# qemu -icount shift=$SHIFT.  Latency columns are DESIGN.md 2.5's model applied
# to this point, NOT a measurement: nothing in QEMU can measure audio latency.
# 'flight' is ring x block period, the audio in the ring.
# 'peak/period' is the worst single render divided by the block period: above
# 1.00 means one block took longer than its own playing time and the ring had
# to absorb it.
#
# 'late_us' is the worst tick-to-service latency the image saw: how late the
# sink's own interrupt was. 'cause' applies emu/test.sh's rule -- an underrun
# with late_us greater than the slack the ring covers, (ring-1) x period, is
# the HOST having descheduled QEMU, not the render loop having missed
# anything. -icount removes most of that but not all: when the render loop
# WFIs no instructions retire, so the virtual clock falls back on real time
# through the idle path, and a busy container leaks in there. (-icount
# sleep=off, which would close that door, hangs -- FINDINGS.md 9, re-confirmed
# 2026-09-18: killed after 200 s with no output.)
#
EOF
printf "%6s %5s %9s %9s %6s %7s %7s %6s %10s %9s %9s %9s %s\n" \
    block ring period_us flight_ms under bursts worstrun min_q peak/period late_us lat_typ_ms lat_max_ms cause
for b in $BLOCKS; do
  for r in $RINGS; do
    log="$OUT/s_${b}_${r}_$SHIFT.txt"
    run "$log" "$SHIFT" --engine "$ENGINE" --midi "$MIDI" --realtime \
        --seconds "$SECONDS_" --block "$b" --ring "$r"
    if ! grep -q -- "--- end ---" "$log"; then
        printf "%6s %5s %s\n" "$b" "$r" "   RUN DID NOT FINISH ($log)"
        continue
    fi
    per=$(grep -E "^worst render" "$log" | sed -n 's/.*block period \([0-9]*\) us.*/\1/p')
    und=$(grep -E "^underruns" "$log" | awk '{print $NF}')
    wr=$(grep -E "^worst render" "$log" | awk '{print $3}')
    minq=$(grep -E "^min ring occupancy" "$log" | awk '{print $4}')
    bursts=$(grep -E "^underrun bursts" "$log" | awk '{print $3}')
    worstrun=$(grep -E "^underrun bursts" "$log" | sed -n 's/.*worst run \([0-9]*\) periods.*/\1/p')
    late=$(grep -E "^timer ticks" "$log" | sed -n 's/.*worst tick latency \([0-9]*\) us.*/\1/p')
    python3 - "$b" "$r" "$per" "$und" "$bursts" "$worstrun" "$minq" "$wr" "$late" <<'PY'
import sys
b,r,per,und,bursts,worstrun,minq,wr,late = sys.argv[1:10]
b=int(b); r=int(r); per=int(per); wr=int(wr); late=int(late or 0)
flight = r*per/1000.0
# DESIGN.md 2.5, applied to this point. Fixed costs (wire 0.32, DAC ~0.4,
# I2S frame 0.02) are the same at every point and are added so the numbers are
# comparable with DESIGN.md's table directly.
FIXED = 0.32+0.4+0.02
typ = FIXED + 0.5*per/1000.0 + max(r-2,0)*per/1000.0
mx  = FIXED + 1.0*per/1000.0 + max(r-1,0)*per/1000.0
slack = (r-1)*per
if int(und) == 0:          cause = "-"
elif late > slack:         cause = "sink late (host)"
else:                      cause = "RENDER LATE"
print("%6d %5d %9d %9.2f %6s %7s %7s %6s %10.2f %9d %9.2f %9.2f %s"
      % (b,r,per,flight,und,bursts,worstrun,minq,wr/float(per),late,typ,mx,cause))
PY
  done
done
exit 0
fi

# --- RTF mode -------------------------------------------------------------
if [ "$MODE" = rtf ]; then
cat <<EOF
# How slow can the renderer get before each (block, ring) pair breaks?
#
# Each row is one -icount shift, i.e. one fictional machine speed. 'rtf' is
# measured on the SAME image with --sink none, which retires every block the
# instant it is committed so that wall/audio is the pipeline's own cost rather
# than the metronome's. 'peak' is the worst single block's render divided by
# the block period on that same run.
#
# The pass/fail columns are separate paced runs at that shift: '.' = zero
# underruns, a number = underruns. This is the fallback table DESIGN.md 2.2
# needs if workstream A's real-time factor comes back bad.
#
# engine $ENGINE, --midi $MIDI --realtime, ${SECONDS_}s
#
EOF
RTF_POINTS=${RTF_POINTS:-"128/2 128/3 128/4 64/3 256/3 128/8"}
printf "%6s %8s %7s" shift rtf peak
for p in $RTF_POINTS; do printf " %7s" "$p"; done
printf "\n"
for sh_ in $SHIFTS; do
    log="$OUT/rtf_$sh_.txt"
    run "$log" "$sh_" --engine "$ENGINE" --midi "$MIDI" --seconds 2 --sink none
    rtf=$(grep -E "^real-time factor" "$log" | awk '{print $NF}')
    per=$(grep -E "^worst render" "$log" | sed -n 's/.*block period \([0-9]*\) us.*/\1/p')
    wr=$(grep -E "^worst render" "$log" | awk '{print $3}')
    [ -n "$per" ] || { echo "$sh_ RUN DID NOT FINISH"; continue; }
    peak=$(python3 -c "print('%.2f' % ($wr/float($per)))")
    printf "%6s %8s %7s" "$sh_" "$rtf" "$peak"
    for p in $RTF_POINTS; do
        b=${p%/*}; r=${p#*/}
        l="$OUT/rtf_${sh_}_${b}_${r}.txt"
        run "$l" "$sh_" --engine "$ENGINE" --midi "$MIDI" --realtime \
            --seconds "$SECONDS_" --block "$b" --ring "$r"
        if ! grep -q -- "--- end ---" "$l"; then printf " %7s" "TIMEOUT"; continue; fi
        u=$(grep -E "^underruns" "$l" | awk '{print $NF}')
        [ "$u" = "0" ] && printf " %7s" "." || printf " %7s" "$u"
    done
    printf "\n"
done
exit 0
fi

# --- cliff mode -----------------------------------------------------------
# -icount's shift is an integer, so the RTF sweep above can only step the
# fictional machine's speed by factors of two, and the interesting region --
# RTF 0.6 to 1.2 -- falls between two steps. This mode gets a continuous knob a
# different way: it keeps the fictional machine fixed and moves the DEADLINE.
#
# --rate tells the audio sink the stream is R Hz, so the block period becomes
# frames/R. The engine still renders the same 128 frames of the same work, so
# the work per deadline is unchanged and only the deadline moves. Setting
# R = 48000*k is therefore exactly an RTF multiplier of k. It is NOT a real
# 48k-to-Rk mode and the audio it produces would play at the wrong speed --
# nothing here listens to it.
if [ "$MODE" = cliff ]; then
cat <<EOF
# RTF cliff, in fine steps. -icount shift=$SHIFT throughout; the RTF is moved
# by shortening the block period with --rate (see tools/sweep.sh). 'rtf' is
# the shift-$SHIFT free-run figure scaled by the rate factor.
# '.' = zero underruns.  engine $ENGINE, --midi $MIDI --realtime, ${SECONDS_}s
#
EOF
BASE_RTF=${BASE_RTF:-0.592}
RATES=${RATES:-"48000 57600 62400 67200 72000 76800 86400 96000"}
CPOINTS=${CPOINTS:-"128/2 128/3 128/4 128/6 128/8 64/3 256/3"}
printf "%8s %7s" rate rtf
for p in $CPOINTS; do printf " %7s" "$p"; done
printf "\n"
for R in $RATES; do
    rtf=$(python3 -c "print('%.2f' % ($BASE_RTF*$R/48000.0))")
    printf "%8s %7s" "$R" "$rtf"
    for p in $CPOINTS; do
        b=${p%/*}; r=${p#*/}
        l="$OUT/cliff_${R}_${b}_${r}.txt"
        run "$l" "$SHIFT" --engine "$ENGINE" --midi "$MIDI" --realtime \
            --seconds "$SECONDS_" --block "$b" --ring "$r" --rate "$R"
        if ! grep -q -- "--- end ---" "$l"; then printf " %7s" "TIMEOUT"; continue; fi
        u=$(grep -E "^underruns" "$l" | awk '{print $NF}')
        [ "$u" = "0" ] && printf " %7s" "." || printf " %7s" "$u"
    done
    printf "\n"
done
exit 0
fi
