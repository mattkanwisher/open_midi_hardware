#!/usr/bin/env bash
#
# bench/estimate.sh -- regenerate every number in bench/ANALYSIS.md sections 8
# and 9, from a clean tree, in one command.
#
#   ./bench/estimate.sh            full run   (~20-30 min, most of it qemu)
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
host_rtf() {
  local label="$1"; shift
  local line
  line="$("$B_HOST/rtf-synth" --block "$BLOCK" "$@" | grep '^SYNTHRESULT')"
  printf '%s\t%s\n' "$label" "$line" >> "$OUT/host-rtf.tsv"
  say "  $(printf '%-26s %s' "$label" "$(echo "$line" | sed 's/SYNTHRESULT //')")"
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
  host_rtf "host-p32-saw"         --partials 32 --seconds 2.0 --warmup 0.25 --waveform saw
  host_rtf "host-p32-struct1"     --partials 32 --seconds 2.0 --warmup 0.25 --structure 1
  host_rtf "host-p32-struct2-pcm" --partials 32 --seconds 2.0 --warmup 0.25 --structure 2
  host_rtf "host-p32-48k"         --partials 32 --seconds 2.0 --warmup 0.25 --sample-rate 48000
  host_rtf "host-p32-analog-dig"  --partials 32 --seconds 2.0 --warmup 0.25 --analog digital
  host_rtf "host-p32-retrig8ms"   --partials 32 --seconds 2.0 --warmup 0.25 --retrigger 8
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
  # 8 ms is a deliberately absurd re-strike rate (1000 note-ons/s across 8
  # parts, where a busy score is tens per second). It is an upper bound on
  # what note churn costs, not an estimate of it.
  arm_cost "arm-p32-retrig8ms"    "$S_HI"  --partials 32 --retrigger 8
  # The float renderer is expensive enough that it needs a shorter window.
  arm_cost "arm-p8-int"           0.004    --partials 8
  arm_cost "arm-p8-float"         0.004    --partials 8 --renderer float
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
print("host x86-64 wall clock, same harness, same workload (NOT the gate)")
for line in open(host_path):
    label, rest = line.rstrip('\n').split('\t', 1)
    d = dict(kv.split('=', 1) for kv in rest.replace('SYNTHRESULT ', '').split())
    print("  %-22s active=%-3s rtf=%-9s worst_block_rtf=%-9s ns/frame=%s"
          % (label, d['active'], d['rtf'], d['worst_rtf'], d['ns_per_frame']))

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
