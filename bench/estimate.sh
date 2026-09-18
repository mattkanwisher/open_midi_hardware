#!/usr/bin/env bash
#
# bench/estimate.sh -- regenerate every number in bench/ANALYSIS.md sections 8
# and 9, from a clean tree, in one command.
#
#   ./bench/estimate.sh            full run   (~35 min, most of it qemu)
#   ./bench/estimate.sh --quick    short run  (~4 min, fewer sweep points)
#
# WHAT THIS PRODUCES, AND WHAT IT DOES NOT.
#
# It does NOT answer docs/PLAN.md section 0's gate. There are no MT-32 ROMs in
# this repository and no Cortex-A7 silicon in this container, and neither of
# those facts is fixable by running code. What it produces instead is:
#
#   1. an armv7-a DYNAMIC INSTRUCTION COUNT per output frame, as a function of
#      the number of sounding partials, measured exactly, by executing the
#      cross-built harness under qemu-arm with one guest instruction per
#      translation block and counting the trace;
#   2. a host x86-64 wall-clock real-time factor for the same workload, which
#      is useless for the gate but bounds the algorithmic cost and catches
#      gross errors in the ARM path;
#   3. a projected Cortex-A7 RTF *as a function of an assumed IPC*, with the
#      break-even IPC stated, because IPC is the thing this container cannot
#      measure.
#
# Every workload here is driven by FABRICATED ROMs. See bench/rtf_synth.cpp.
#
# Requirements: cmake, a host C++ compiler, arm-linux-gnueabihf-g++,
# qemu-arm (user mode), python3, and Munt cloned into bench/vendor/munt.
#
# No copyright asserted on this file.

set -eu

QUICK=0
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

BENCH="$(cd "$(dirname "$0")" && pwd)"
OUT="$BENCH/results"
MUNT="$BENCH/vendor/munt"

if [ ! -f "$MUNT/mt32emu/CMakeLists.txt" ]; then
  echo "error: Munt is not vendored (it is deliberately not committed)." >&2
  echo "  git clone https://github.com/munt/munt.git $MUNT" >&2
  echo "  git -C $MUNT checkout 6e7c01fba7e1d50c8fa705834889fd0eac136075" >&2
  exit 1
fi
for t in cmake python3 qemu-arm arm-linux-gnueabihf-g++ arm-linux-gnueabihf-objdump; do
  command -v "$t" >/dev/null 2>&1 || { echo "error: $t not found in PATH" >&2; exit 1; }
done

mkdir -p "$OUT"
: > "$OUT/arm-insn.tsv"
: > "$OUT/host-rtf.tsv"
RUNLOG="$OUT/run.log"
: > "$RUNLOG"

say() { printf '%s\n' "$*" | tee -a "$RUNLOG"; }
run() { printf '$ %s\n' "$*" >> "$RUNLOG"; "$@" >> "$RUNLOG" 2>&1; }

say "=== bench/estimate.sh ==="
say "date            : $(date -u '+%Y-%m-%d %H:%M:%SZ')"
say "munt commit     : $(git -C "$MUNT" rev-parse HEAD 2>/dev/null || echo unknown)"
say "host g++        : $(g++ --version | head -1)"
say "cross g++       : $(arm-linux-gnueabihf-g++ --version | head -1)"
say "qemu-arm        : $(qemu-arm --version | head -1)"
say "host CPU        : $(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //' || echo unknown)"
say ""

# ---------------------------------------------------------------- builds ----

B_HOST="$BENCH/build-host"
B_ARM="$BENCH/build-armv7-count"
B_ARM_NONEON="$BENCH/build-armv7-noneon"
TC="$BENCH/cmake/toolchain-armv7a-neon.cmake"

say "--- building ---"
run cmake -S "$BENCH" -B "$B_HOST" -DCMAKE_BUILD_TYPE=Release
run cmake --build "$B_HOST" -j "$(nproc)"

# Static, so that the instruction count is a property of this binary alone and
# not of whatever ld.so qemu-arm happens to find.
run cmake -S "$BENCH" -B "$B_ARM" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_EXE_LINKER_FLAGS=-static
run cmake --build "$B_ARM" -j "$(nproc)"

run cmake -S "$BENCH" -B "$B_ARM_NONEON" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_EXE_LINKER_FLAGS=-static \
      -DT113_FPU=vfpv3-d16
run cmake --build "$B_ARM_NONEON" -j "$(nproc)"
say "host, armv7-a NEON and armv7-a no-NEON builds: ok"
say ""

# ------------------------------------------------------- instruction count --
#
# qemu-arm 8.2 in Debian/Ubuntu is built WITHOUT TCG plugin support
# (`qemu-arm -plugin help` answers "unknown option"), so libinsn.so is not an
# option here. What does work is -one-insn-per-tb plus -d exec: one trace line
# per translation block, one instruction per translation block, therefore one
# line per executed guest instruction. Verified against a hand-counted loop:
# a six-instruction ARM loop body produced exactly 6.000 lines per iteration.
#
# The count is of the WHOLE PROCESS, so every measurement is a difference
# between two runs that differ only in how much audio they render. Everything
# fixed -- process start, ROM fabrication, Synth::open, the probe block -- is
# identical between the two and cancels exactly.

count_insns() {   # count_insns <stdout-file> <args...>
  local outfile="$1"; shift
  { qemu-arm -one-insn-per-tb -d exec -D /dev/fd/3 \
      "$BIN" --count-mode "$@" > "$outfile" 2>/dev/null; } 3>&1 | wc -l
}

BLOCK=256
S_LO=0          # 0 frames
S_HI=0.016      # 512 frames = 2 blocks of 256 at 32000 Hz

# arm_cost <label> <s_hi> <args...>
arm_cost() {
  local label="$1"; shift
  local shi="$1"; shift
  local o1 o2 n1 n2 f1 f2 rate active
  o1="$(mktemp)"; o2="$(mktemp)"
  n1="$(count_insns "$o1" --block "$BLOCK" --seconds "$S_LO" "$@")"
  n2="$(count_insns "$o2" --block "$BLOCK" --seconds "$shi" "$@")"
  # NB the space before "frames=" matters: "block_frames=" must not match.
  f1="$(sed -n 's/.*SYNTHRESULT .* frames=\([0-9]*\) .*/\1/p' "$o1")"
  f2="$(sed -n 's/.*SYNTHRESULT .* frames=\([0-9]*\) .*/\1/p' "$o2")"
  rate="$(sed -n 's/.*SYNTHRESULT .*out_rate=\([0-9]*\) .*/\1/p' "$o2")"
  active="$(sed -n 's/.*SYNTHRESULT partials=[0-9]* active=\([0-9]*\) .*/\1/p' "$o2")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$label" "$active" "$rate" "$f1" "$f2" "$n1" "$n2" "$*" >> "$OUT/arm-insn.tsv"
  say "$(printf '  %-26s active=%-3s out_rate=%-6s frames %s->%s  insns %s->%s  = %s insn/frame' \
      "$label" "$active" "$rate" "$f1" "$f2" "$n1" "$n2" \
      "$(python3 -c "print('%.1f' % (($n2-$n1)/float($f2-$f1)))")")"
  rm -f "$o1" "$o2"
}

# host_rtf <label> <args...>
#
# HOST_REPS runs, and we keep the one with the lowest CPU time. This container
# is shared and its wall clock is contended: back-to-back runs of an identical
# workload have been observed to differ by more than 3x. The minimum is the
# least-interfered-with sample, and it is the only host statistic worth
# quoting. The spread is reported too, so the reader can see how bad it was.
HOST_REPS=5
host_rtf() {
  local label="$1"; shift
  local i line best best_cpu cpu worst_cpu
  best=""; best_cpu=""; worst_cpu=""
  for i in $(seq 1 $HOST_REPS); do
    line="$("$B_HOST/rtf-synth" --block "$BLOCK" "$@" | grep '^SYNTHRESULT')"
    cpu="$(printf '%s' "$line" | sed -n 's/.* cpu_s=\([0-9.]*\) .*/\1/p')"
    if [ -z "$best_cpu" ] || [ "$(python3 -c "print(1 if $cpu < $best_cpu else 0)")" = 1 ]; then
      best_cpu="$cpu"; best="$line"
    fi
    if [ -z "$worst_cpu" ] || [ "$(python3 -c "print(1 if $cpu > $worst_cpu else 0)")" = 1 ]; then
      worst_cpu="$cpu"
    fi
  done
  printf '%s\t%s\tspread=%s\n' "$label" "$best" \
      "$(python3 -c "print('%.2f' % ($worst_cpu/float($best_cpu)))")" >> "$OUT/host-rtf.tsv"
  say "  $(printf '%-26s %s  [best of %d, worst/best=%sx]' "$label" \
      "$(echo "$best" | sed 's/SYNTHRESULT //')" "$HOST_REPS" \
      "$(python3 -c "print('%.2f' % ($worst_cpu/float($best_cpu)))")")"
}

# ------------------------------------------------------------- host sweep ---

say "--- host x86-64 wall clock (NOT the gate; a cross-check only) ---"
if [ "$QUICK" = 1 ]; then HOST_NS="1 8 32"; else HOST_NS="0 1 2 4 8 12 16 20 24 28 32"; fi
for n in $HOST_NS; do
  host_rtf "host-p$n" --partials "$n" --seconds 2.0 --warmup 0.25
done
if [ "$QUICK" = 0 ]; then
  host_rtf "host-p32-reverb-off"  --partials 32 --seconds 2.0 --warmup 0.25 --reverb off
  host_rtf "host-p32-float"       --partials 32 --seconds 2.0 --warmup 0.25 --renderer float
  host_rtf "host-p32-float-pcm"   --partials 32 --seconds 2.0 --warmup 0.25 --renderer float --structure 2
  host_rtf "host-p32-saw"         --partials 32 --seconds 2.0 --warmup 0.25 --waveform saw
  host_rtf "host-p32-struct1"     --partials 32 --seconds 2.0 --warmup 0.25 --structure 1
  host_rtf "host-p32-struct2-pcm" --partials 32 --seconds 2.0 --warmup 0.25 --structure 2
  host_rtf "host-p32-48k"         --partials 32 --seconds 2.0 --warmup 0.25 --sample-rate 48000
  host_rtf "host-p32-analog-dig"  --partials 32 --seconds 2.0 --warmup 0.25 --analog digital
  host_rtf "host-p32-retrig2ms"   --partials 32 --seconds 2.0 --warmup 0.25 --retrigger 2
fi
say ""

# ------------------------------------------------ armv7 instruction counts --

BIN="$B_ARM/rtf-synth"
say "--- armv7-a dynamic instruction count under qemu-arm (exact) ---"
if [ "$QUICK" = 1 ]; then ARM_NS="1 8 32"; else ARM_NS="1 2 4 8 16 24 32"; fi
for n in $ARM_NS; do
  arm_cost "arm-p$n" "$S_HI" --partials "$n"
done

if [ "$QUICK" = 0 ]; then
  arm_cost "arm-p32-reverb-off"   "$S_HI"  --partials 32 --reverb off
  arm_cost "arm-p32-saw"          "$S_HI"  --partials 32 --waveform saw
  arm_cost "arm-p32-struct1"      "$S_HI"  --partials 32 --structure 1
  arm_cost "arm-p32-struct2-pcm"  "$S_HI"  --partials 32 --structure 2
  arm_cost "arm-p32-analog-dig"   "$S_HI"  --partials 32 --analog digital
  arm_cost "arm-p32-48k"          "$S_HI"  --partials 32 --sample-rate 48000
  arm_cost "arm-p32-block64"      "$S_HI"  --partials 32 --block 64
  arm_cost "arm-p32-block512"     "$S_HI"  --partials 32 --block 512
  # 2 ms is a deliberately absurd re-strike rate: every 64 frames, all 8 parts
  # get an All Sound Off and a fresh note-on, i.e. 4000 note-ons per second
  # where a busy score is tens. It is an upper bound on what note churn costs,
  # not an estimate of it. 512 frames / 64 = 7 re-strikes inside the window.
  arm_cost "arm-p32-retrig2ms"    "$S_HI"  --partials 32 --retrigger 2
  # The float renderer is expensive enough that it needs a shorter window.
  arm_cost "arm-p8-int"           0.004    --partials 8
  arm_cost "arm-p8-float"         0.004    --partials 8 --renderer float
  # structure 2 puts a PCM partial in each pair, which is the only way to reach
  # LA32FloatWaveGenerator's fmod() and its second cos() (ANALYSIS.md 3).
  arm_cost "arm-p8-int-pcm"       0.004    --partials 8 --structure 2
  arm_cost "arm-p8-float-pcm"     0.004    --partials 8 --structure 2 --renderer float
fi

if [ "$QUICK" = 0 ]; then
  RS_BASE="$(grep -P '^arm-p32\t' "$OUT/arm-insn.tsv" | head -1 | cut -f7)"
  RS_BASE0="$(grep -P '^arm-p32\t' "$OUT/arm-insn.tsv" | head -1 | cut -f6)"
  RS_RT="$(grep -P '^arm-p32-retrig2ms\t' "$OUT/arm-insn.tsv" | head -1 | cut -f7)"
  RS_RT0="$(grep -P '^arm-p32-retrig2ms\t' "$OUT/arm-insn.tsv" | head -1 | cut -f6)"
  say "  cost of one full re-strike (8 All Sound Off + 8 note-ons = 32 partials"
  say "  torn down and reallocated), from 7 re-strikes in 512 frames:"
  say "    $(python3 -c "print('%.0f instructions' % ((($RS_RT-$RS_RT0)-($RS_BASE-$RS_BASE0))/7.0))")"
fi

# NEON ablation: the same source, same -O2, same -mcpu, NEON simply not
# available to the compiler.
BIN="$B_ARM_NONEON/rtf-synth"
arm_cost "arm-p32-NO-NEON" "$S_HI" --partials 32
if [ "$QUICK" = 0 ]; then
  arm_cost "arm-p1-NO-NEON" "$S_HI" --partials 1
fi
BIN="$B_ARM/rtf-synth"
say ""

# ------------------------------------ an IPC reference point, on the host ----
#
# The one thing this container cannot supply is cycles per instruction on a
# Cortex-A7. It can supply cycles per instruction on THIS code on the host,
# which is a useful reference in one direction only: the host is a wide
# out-of-order x86-64 with megabytes of cache, and the A7 is an in-order,
# partial-dual-issue core with 32 KiB of L1 behind DDR3. Whatever IPC the host
# reaches here is an upper reference, not a prediction.
#
# valgrind/callgrind counts host instructions exactly, the same way qemu counts
# guest ones, and the same 0-frames-versus-512-frames differencing applies.

if command -v valgrind >/dev/null 2>&1; then
  say "--- host x86-64: exact instruction count, and the IPC it implies ---"
  vg_insns() {
    valgrind --tool=callgrind --callgrind-out-file=/dev/null \
      "$B_HOST/rtf-synth" --count-mode --block "$BLOCK" "$@" 2>&1 \
      | sed -n 's/.*I *refs: *\([0-9,]*\)/\1/p' | tr -d ','
  }
  VG_LO="$(vg_insns --partials 32 --seconds "$S_LO")"
  VG_HI="$(vg_insns --partials 32 --seconds "$S_HI")"
  HOST_NS32="$(grep -P '^host-p32\t' "$OUT/host-rtf.tsv" | head -1 \
               | sed -n 's/.*ns_per_frame=\([0-9.]*\).*/\1/p')"
  HOST_GHZ="$(grep -m1 'model name' /proc/cpuinfo | sed -n 's/.*@ *\([0-9.]*\)GHz.*/\1/p')"
  [ -n "$HOST_GHZ" ] || HOST_GHZ=0
  say "  x86-64 insn/frame at 32 partials : $(python3 -c "print('%.1f' % (($VG_HI-$VG_LO)/512.0))")"
  say "  armv7-a insn/frame at 32 partials: $(grep -P '^arm-p32\t' "$OUT/arm-insn.tsv" | head -1 \
        | awk -F'\t' '{printf "%.1f", ($7-$6)/512.0}')"
  if [ -n "$HOST_NS32" ]; then
    say "  host best wall clock             : $HOST_NS32 ns/frame"
    say "  => host retires this code at     : $(python3 -c "print('%.2f instructions per nanosecond' % ((($VG_HI-$VG_LO)/512.0)/$HOST_NS32))")"
    say "     an A7 at 1.2 GHz and IPC 1.0 would manage 1.20, so the host is"
    say "     $(python3 -c "print('%.1fx' % (((($VG_HI-$VG_LO)/512.0)/$HOST_NS32)/1.2))") faster per unit time on this code."
    if [ "$HOST_GHZ" != "0" ]; then
      NOMIPC="$(python3 -c "print('%.2f' % ((($VG_HI-$VG_LO)/512.0)/($HOST_NS32*$HOST_GHZ)))")"
      say "  nominal host IPC at $HOST_GHZ GHz  : $NOMIPC"
      if [ "$(python3 -c "print(1 if $NOMIPC > 4.0 else 0)")" = 1 ]; then
        say "     ^ ABOVE 4: NOT A REAL IPC. No x86-64 core retires that many"
        say "       instructions per cycle on code like this. It means the clock in"
        say "       /proc/cpuinfo is not the clock this container actually runs at,"
        say "       which is normal under virtualisation. Do not quote it; quote the"
        say "       instructions-per-nanosecond figure above, which needs no clock."
      fi
    fi
  fi
  say ""
else
  say "valgrind not present: skipping the host IPC reference point"
  say ""
fi

# ------------------------------------------------------ where the time goes --
#
# qemu's exec trace prints the symbol the PC falls in, so the same trace that
# counts instructions also profiles them, exactly, with no sampling error.
# Two profiles -- with and without audio -- subtract to the render path alone.

profile_one() {   # profile_one <outfile> <args...>
  local outfile="$1"; shift
  { qemu-arm -one-insn-per-tb -d exec -D /dev/fd/3 \
      "$BIN" --count-mode --block "$BLOCK" "$@" > /dev/null 2>/dev/null; } 3>&1 \
    | awk -F'] ' '{ s = $2; if (s == "") s = "(no-symbol)"; c[s]++ }
                  END { for (k in c) printf "%d\t%s\n", c[k], k }' \
    | sort -rn > "$outfile"
}

say "--- exact instruction profile of the render path, 32 partials ---"
profile_one "$OUT/prof-p32-render.tsv" --partials 32 --seconds "$S_HI"
profile_one "$OUT/prof-p32-setup.tsv"  --partials 32 --seconds 0
python3 - "$OUT/prof-p32-render.tsv" "$OUT/prof-p32-setup.tsv" 512 <<'PY' | c++filt | tee -a "$RUNLOG"
import sys
render, setup, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
def load(path):
    d = {}
    for line in open(path):
        n, sym = line.rstrip('\n').split('\t', 1)
        d[sym] = d.get(sym, 0) + int(n)
    return d
r, s = load(render), load(setup)
delta = {}
for k in set(list(r) + list(s)):
    v = r.get(k, 0) - s.get(k, 0)
    if v > 0: delta[k] = v
total = sum(delta.values())
print()
print("  %d instructions attributable to rendering %d frames (%.1f per frame)"
      % (total, frames, total / float(frames)))
print()
print("  %8s  %6s  %s" % ("insn/fr", "share", "symbol"))
for k in sorted(delta, key=lambda x: -delta[x])[:20]:
    print("  %8.1f  %5.1f%%  %s" % (delta[k] / float(frames),
                                    100.0 * delta[k] / total, k))
PY
say ""

# ------------------------------- what NEON actually bought, attributed ------
#
# -dfilter restricts the exec log to one address range, so this counts the
# instructions executed *inside* Synth::loadPCMROM and nothing else, in the
# NEON build and in the no-NEON build. That turns "NEON only helps the ROM
# decode" from an inference into a measurement.

pcmrom_insns() {   # pcmrom_insns <binary>
  local bin="$1" line start size
  line="$(arm-linux-gnueabihf-nm -C --defined-only -S "$bin" \
          | grep -m1 'MT32Emu::Synth::loadPCMROM')"
  [ -n "$line" ] || { echo "0"; return; }
  start="0x$(printf '%s' "$line" | awk '{print $1}')"
  size="0x$(printf '%s' "$line" | awk '{print $2}')"
  { qemu-arm -dfilter "${start}+${size}" -one-insn-per-tb -d exec -D /dev/fd/3 \
      "$bin" --count-mode --block "$BLOCK" --partials 0 --seconds 0 \
      > /dev/null 2>/dev/null; } 3>&1 | wc -l
}

say "--- instructions executed inside Synth::loadPCMROM (one-time, at open) ---"
PCM_NEON="$(pcmrom_insns "$B_ARM/rtf-synth")"
PCM_SCALAR="$(pcmrom_insns "$B_ARM_NONEON/rtf-synth")"
say "  NEON build    : $PCM_NEON"
say "  no-NEON build : $PCM_SCALAR"
if [ "$PCM_NEON" -gt 0 ]; then
  say "  ratio         : $(python3 -c "print('%.2fx' % ($PCM_SCALAR/float($PCM_NEON)))")"
  say "  per PCM ROM byte (524288 B): $(python3 -c "print('%.2f vs %.2f instructions' % ($PCM_NEON/524288.0, $PCM_SCALAR/524288.0))")"
fi
say ""

# ------------------------------------------------------- static code census --

say "--- NEON in the generated code (q-register operands per object) ---"
OBJ="$B_ARM/mt32emu/CMakeFiles/mt32emu.dir/src"
{
  printf 'object\tqreg_insns\n'
  for o in "$OBJ"/*.o "$OBJ"/srchelper/*.o "$OBJ"/srchelper/srctools/src/*.o; do
    [ -f "$o" ] || continue
    c=$(arm-linux-gnueabihf-objdump -d "$o" \
        | grep -cE '^[[:space:]]*[0-9a-f]+:.*[[:space:]]q[0-9]+' || true)
    printf '%s\t%s\n' "$(basename "$o")" "$c"
  done
} > "$OUT/neon-census.tsv"
tail -n +2 "$OUT/neon-census.tsv" | sort -k2 -t"$(printf '\t')" -nr | head -12 | tee -a "$RUNLOG"
say "  (full census in $OUT/neon-census.tsv; objects with 0 are the render path)"
say ""

say "--- libm calls out of the float wave generator (why float is disqualified) ---"
arm-linux-gnueabihf-objdump -dr "$OBJ/LA32FloatWaveGenerator.cpp.o" \
  | grep -oE 'R_ARM_[A-Z_0-9]+[[:space:]]+[A-Za-z_][A-Za-z_0-9]*' \
  | awk '{print $2}' | sort | uniq -c | sort -rn | tee "$OUT/float-libm.txt" | tee -a "$RUNLOG"
say ""

say "--- size(1), armv7-a Release ---"
arm-linux-gnueabihf-size -t "$B_ARM/mt32emu/libmt32emu.a" | tail -1 | tee -a "$RUNLOG"
arm-linux-gnueabihf-size "$B_ARM/rtf-synth" "$B_ARM/rtf" | tee -a "$RUNLOG"
say ""

# -------------------------------------------------------------- the model ---

say "--- the cost model, and what it implies ---"
python3 - "$OUT/arm-insn.tsv" "$OUT/host-rtf.tsv" "$OUT/model.txt" <<'PY' | tee -a "$RUNLOG"
import sys

arm_path, host_path, out_path = sys.argv[1:4]

rows = []
for line in open(arm_path):
    f = line.rstrip('\n').split('\t')
    if len(f) < 7: continue
    label, active, rate, f1, f2, n1, n2 = f[0], int(f[1]), float(f[2]), int(f[3]), int(f[4]), int(f[5]), int(f[6])
    rows.append(dict(label=label, active=active, rate=rate,
                     ipf=(n2 - n1) / float(f2 - f1), fixed=n1))

by = {r['label']: r for r in rows}

CLK = 1.2e9          # docs/PLAN.md section 1: Cortex-A7 at 1.2 GHz
TARGET = 0.6         # docs/PLAN.md section 0

def rtf(ipf, rate, ipc):
    """RTF = (instructions per output frame / IPC) * output frames per second / clock."""
    return (ipf / ipc) * rate / CLK

print()
print("armv7-a instructions per output frame, by active partial count")
print("  (exact dynamic count; qemu-arm -one-insn-per-tb -d exec, differenced)")
print()
print("  partials  insn/frame   insn/partial/frame (marginal)")
sweep = sorted([r for r in rows if r['label'].startswith('arm-p') and
                r['label'][5:].isdigit()], key=lambda r: r['active'])
prev = None
for r in sweep:
    marg = '' if prev is None else '%10.1f' % ((r['ipf'] - prev['ipf']) / (r['active'] - prev['active']))
    print("  %8d  %10.1f   %s" % (r['active'], r['ipf'], marg))
    prev = r

# Least squares fit over the sweep: insn/frame = a + b * partials
if len(sweep) >= 2:
    n = len(sweep)
    sx = sum(r['active'] for r in sweep); sy = sum(r['ipf'] for r in sweep)
    sxx = sum(r['active'] ** 2 for r in sweep); sxy = sum(r['active'] * r['ipf'] for r in sweep)
    b = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    a = (sy - b * sx) / n
    resid = [r['ipf'] - (a + b * r['active']) for r in sweep]
    worst = max(abs(x) for x in resid)
    print()
    print("  least-squares fit : insn/frame = %.1f + %.1f * partials" % (a, b))
    print("  worst residual    : %.1f insn/frame (%.2f%% of the 32-partial value)"
          % (worst, 100.0 * worst / (a + 32 * b)))

p32 = next((r for r in sweep if r['active'] == 32), None)
if p32:
    print()
    print("PROJECTED Cortex-A7 RTF at 1.2 GHz, 32 sounding partials, 32 kHz out")
    print("  This is arithmetic on the instruction count above and an ASSUMED IPC.")
    print("  IPC is exactly what this container cannot measure: qemu's TCG models")
    print("  neither the A7 pipeline nor its caches.")
    print()
    print("  assumed IPC   cycles/frame   RTF     vs target %.2f" % TARGET)
    for ipc in (1.2, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4):
        cyc = p32['ipf'] / ipc
        v = rtf(p32['ipf'], p32['rate'], ipc)
        print("  %11.2f   %12.0f   %5.3f   %s" %
              (ipc, cyc, v, "PASS" if v <= TARGET else ("real time" if v <= 1.0 else "FAILS")))
    breakeven_target = p32['ipf'] * p32['rate'] / (CLK * TARGET)
    breakeven_rt = p32['ipf'] * p32['rate'] / CLK
    print()
    print("  break-even IPC for RTF <= %.2f (the gate) : %.3f" % (TARGET, breakeven_target))
    print("  break-even IPC for RTF <= 1.00 (real time): %.3f" % breakeven_rt)
    print()
    print("  HARD FLOOR: the Cortex-A7 is at best partial-dual-issue, so IPC <= 2")
    print("  is an architectural ceiling no tuning can beat. RTF >= %.3f at 32"
          % rtf(p32['ipf'], p32['rate'], 2.0))
    print("  partials, whatever else is true.")

    if len(sweep) >= 2:
        print()
        print("INVERTED: how many sounding partials fit inside RTF %.2f, per assumed IPC" % TARGET)
        print("  from the fit insn/frame = %.1f + %.1f * partials" % (a, b))
        print()
        print("  assumed IPC   partials that fit")
        for ipc in (1.2, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4):
            budget = TARGET * CLK * ipc / p32['rate']    # instructions per frame
            n = (budget - a) / b
            print("  %11.2f   %s" % (ipc, ("%.1f" % n) if n > 0 else "none"))

print()
print("variants, armv7-a instructions per output frame")
for r in rows:
    if r['label'] in ('arm-p32', 'arm-p1'): continue
    if r['label'].startswith('arm-p') and r['label'][5:].isdigit(): continue
    base = by.get('arm-p32')
    ratio = ''
    if base and r['label'].startswith('arm-p32'):
        ratio = '  (%.2fx of arm-p32)' % (r['ipf'] / base['ipf'])
    if r['label'] == 'arm-p8-float' and 'arm-p8-int' in by:
        ratio = '  (%.2fx of arm-p8-int)' % (r['ipf'] / by['arm-p8-int']['ipf'])
    print("  %-22s %10.1f insn/frame  out_rate=%d%s" % (r['label'], r['ipf'], r['rate'], ratio))

print()
print("one-time cost: instructions executed before any audio is rendered")
print("  (process start + ROM fabrication + Synth::open + one probe block)")
for lbl in ('arm-p32', 'arm-p32-NO-NEON', 'arm-p1', 'arm-p1-NO-NEON'):
    if lbl in by:
        print("  %-22s %12d instructions" % (lbl, by[lbl]['fixed']))

print()
print("host x86-64 wall clock, same harness, same workload (NOT the gate)")
for line in open(host_path):
    label, rest = line.rstrip('\n').split('\t', 1)
    d = dict(kv.split('=', 1) for kv in rest.replace('SYNTHRESULT ', '').replace('\t', ' ').split())
    print("  %-22s active=%-3s rtf=%-9s worst_block_rtf=%-9s ns/frame=%-10s worst/best=%s"
          % (label, d['active'], d['rtf'], d['worst_rtf'], d['ns_per_frame'],
             d.get('spread', '?')))

with open(out_path, 'w') as fh:
    fh.write("see run.log; regenerate with bench/estimate.sh\n")
PY

say ""
say "raw data: $OUT/arm-insn.tsv  $OUT/host-rtf.tsv  $OUT/neon-census.tsv  $OUT/run.log"
say ""
say "########################################################################"
say "## Every workload above ran on FABRICATED ROMs. The projected RTF is  ##"
say "## arithmetic over an exact instruction count and an ASSUMED IPC. It  ##"
say "## does NOT close docs/PLAN.md section 0's gate. bench/ANALYSIS.md 9. ##"
say "########################################################################"
