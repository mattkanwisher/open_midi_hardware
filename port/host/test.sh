#!/bin/sh
# test.sh - exercises the port's structure end to end, with no synthesiser and
# no silicon. Run from port/host after `make`.
#
# Each case asserts on the counters the harness prints, because those counters
# are the contract: a MIDI parser that silently eats a running-status note or a
# ring that silently underruns is exactly the failure this layer exists to make
# visible.
#
# HOW STRONG EACH UNDERRUN ASSERTION IS. Cases 1-3 run with the free-running
# sink, where the play cursor is pulled by the renderer and the ring therefore
# cannot run dry. Their "underruns 0" lines are STRUCTURAL: they check that the
# counter is wired up and nothing pathological happened, not that a deadline
# was met. Only case 4 (--realtime) measures anything, and case 5 exists to
# prove that what it measures can actually go wrong -- a counter no test has
# ever seen fire is a counter nobody should trust. The strongest form of this
# assertion in the project is emu/test.sh's, where the deadline is a timer
# interrupt on a Cortex-A7 from reset. See emu/FINDINGS.md 8.6.

set -e
BIN=build/mtp_host
cd "$(dirname "$0")"
[ -x "$BIN" ] || { echo "build first: make"; exit 2; }
mkdir -p build/t
cd build/t
BIN=../mtp_host
fail=0

expect() {  # expect <label> <pattern> <value> <output-file>
    got=$(grep -E "$2" "$4" | head -1 | awk '{print $NF}')
    if [ "$got" != "$3" ]; then
        echo "FAIL $1: expected $3, got '$got'"
        fail=1
    else
        echo "ok   $1 ($3)"
    fi
}

# --- 1. built-in demo: running status, one sysex, one real-time byte --------
$BIN --seconds 1 --wav demo.wav > d.txt 2>&1
expect "demo short msgs"   "^short messages"   8  d.txt
expect "demo sysex"        "^sysex messages"   1  d.txt
expect "demo underruns (structural)" "^underruns" 0  d.txt
expect "demo realtime dropped" "^realtime dropped" 0 d.txt
expect "demo sink stalls"  "^sink stalls"      0  d.txt
# Deterministic, and it is the fix itself: with the free-running sink the ring
# never reaches the target, so the safety margin has nothing to report and says
# so. The counter this replaced reported "1 of 3" here -- a number produced
# entirely by the ring filling from empty on the first commit.
grep -q "^min ring occupancy  n/a" d.txt \
  && echo "ok   a free-running sink reports no safety margin, not a fake one" \
  || { echo "FAIL $(grep '^min ring occupancy' d.txt)"; fail=1; }
test -s demo.wav && echo "ok   demo wav written"

# --- 2. a 16 kB timbre bank dump interleaved with notes ---------------------
python3 - <<'PY'
def sysex(addr, data):
    body = list(addr) + list(data)
    return bytes([0xF0,0x41,0x10,0x16,0x12]) + bytes(body) + \
           bytes([(-sum(body)) & 0x7f, 0xF7])
out = bytearray([0x90,60,100, 64,100, 67,100])
for i in range(64):
    out += sysex((0x08,(i*2)&0x7f,0x00), bytes([(j*7+i)&0x7f for j in range(246)]))
    if i % 8 == 0:
        out += bytes([0xFE, 72+(i%5), 90])   # real time, then running status
out += bytes([0xB0,123,0])
open('bank.syx','wb').write(bytes(out))
PY
$BIN --midi bank.syx --seconds 30 --block 64 --ring 2 --wav bank.wav > b.txt 2>&1
expect "bank sysex count"  "^sysex messages"  64  b.txt
expect "bank short msgs"   "^short messages"  12  b.txt
expect "bank underruns (structural)" "^underruns" 0  b.txt
expect "bank realtime dropped" "^realtime dropped" 0 b.txt
grep -q "orphan data 0, sysex truncated 0, sysex aborted 0" b.txt \
  && echo "ok   bank parsed cleanly" || { echo "FAIL bank parse"; fail=1; }

# --- 3. a stream that is wrong in three ways --------------------------------
python3 - <<'PY'
bad = bytearray([40,50,60])                                   # orphan data
bad += bytes([0xF0,0x41,0x10,0x16,0x12]) + bytes(100)         # unterminated
bad += bytes([0x90,60,100])                                   # aborts it
bad += bytes([0xF0]) + bytes([0x7f]*40000) + bytes([0xF7])    # oversize
bad += bytes([0x80,60,0])
open('bad.syx','wb').write(bytes(bad))
PY
$BIN --midi bad.syx --seconds 3 --wav bad.wav > x.txt 2>&1
expect "bad sysex emitted" "^sysex messages"   0  x.txt
expect "bad short msgs"    "^short messages"   2  x.txt
expect "bad underruns (structural)" "^underruns" 0  x.txt
grep -q "orphan data 3" x.txt && echo "ok   3 orphan data bytes counted" || \
  { echo "FAIL orphan data count"; fail=1; }
grep -q "sysex truncated 1" x.txt && echo "ok   oversize sysex refused" || \
  { echo "FAIL oversize sysex"; fail=1; }
grep -q "sysex aborted 1" x.txt && echo "ok   unterminated sysex aborted" || \
  { echo "FAIL sysex abort"; fail=1; }

# --- 4. real-time pacing: the ring must never empty -------------------------
# Here the play cursor is driven by the clock, so this one is a measurement.
$BIN --seconds 1 --realtime --wav rt.wav > r.txt 2>&1
expect "realtime underruns (measured)" "^underruns" 0  r.txt
expect "realtime sink stalls" "^sink stalls"    0  r.txt

# The safety margin, which is only meaningful once the ring has filled. Before
# this was fixed the counter sampled occupancy while the ring filled from empty,
# where the first commit leaves occupancy 1 by definition, so it read 1 on every
# run that ever started and said nothing at all about the steady state.
#
# What is asserted is the invariant, not a particular value: the line must be in
# steady-state form, and it must report at least one commit EXCLUDED from the
# sample. That excluded commit is the start-up one, and its presence is the
# whole fix. The value itself is 2 of 3 on an unloaded machine but is a timing
# observation, and this suite also runs under qemu-user, where asserting it
# would be asserting how busy the container is.
python3 - r.txt <<'PYMQ' || fail=1
import re, sys
t = open(sys.argv[1]).read()
m = re.search(r'^min ring occupancy\s+(\d+) of (\d+)\s+\(steady state, after (\d+) '
              r'start-up blocks\)', t, re.M)
if not m:
    print("FAIL min ring occupancy: " +
          (re.search(r'^min ring occupancy.*', t, re.M).group(0) if
           re.search(r'^min ring occupancy.*', t, re.M) else "line missing"))
    sys.exit(1)
mq, ring, startup = (int(x) for x in m.groups())
if startup >= 1 and 1 <= mq <= ring:
    print("ok   min ring occupancy is steady state: %d of %d, %d start-up "
          "block(s) excluded" % (mq, ring, startup))
else:
    print("FAIL min %d of %d with %d start-up blocks excluded" % (mq, ring, startup))
    sys.exit(1)
PYMQ

# The whole iteration is what has to fit in a block period, not render() alone,
# so the harness prints both and the larger one must be the whole iteration.
python3 - r.txt <<'PYWB' || fail=1
import re, sys
t = open(sys.argv[1]).read()
r = int(re.search(r'^worst render\s+(\d+) us', t, re.M).group(1))
b = int(re.search(r'^worst block\s+(\d+) us', t, re.M).group(1))
if b >= r:
    print("ok   worst block (%d us) >= worst render (%d us), both reported" % (b, r))
else:
    print("FAIL worst block %d us < worst render %d us" % (b, r)); sys.exit(1)
PYWB

# --- 5. the underrun counter must be able to fire ---------------------------
# Burn longer than a block period inside every commit and the paced sink has to
# notice. Without this case, "underruns 0" everywhere is equally consistent
# with a counter that is never incremented -- which is what it was.
set +e
$BIN --seconds 1 --realtime --stall-us 4000 --wav st.wav > s.txt 2>&1
rc=$?
set -e
u=$(grep -E "^underruns" s.txt | awk '{print $NF}')
if [ "${u:-0}" -gt 0 ]; then
    echo "ok   a renderer slower than real time is counted ($u underruns)"
else
    echo "FAIL a deliberately late renderer produced no underruns"; fail=1
fi
if [ "$rc" != "0" ]; then echo "ok   underruns make the harness exit non-zero ($rc)"
else echo "FAIL underruns did not fail the run"; fail=1; fi
grep -q "^min ring occupancy  n/a" s.txt \
  && echo "ok   a ring that never filled reports n/a, not a fake minimum" \
  || { echo "FAIL $(grep '^min ring occupancy' s.txt)"; fail=1; }

# --- 6. the engine seam's gain control --------------------------------------
$BIN --seconds 1 --gain 0 --wav g0.wav > g.txt 2>&1
python3 - <<'PYGAIN' || fail=1
import wave, array, sys
def peak(p):
    w = wave.open(p); d = array.array('h')
    d.frombytes(w.readframes(w.getnframes())); w.close()
    return max(abs(x) for x in d) if d else 0
loud, quiet = peak('demo.wav'), peak('g0.wav')
if loud > 1000 and quiet == 0:
    print("ok   --gain 0 silences the engine (peak %d -> %d)" % (loud, quiet))
else:
    print("FAIL gain: peak %d -> %d" % (loud, quiet)); sys.exit(1)
PYGAIN

# --- 7. panic ---------------------------------------------------------------
# Notes with no note-offs, so the engine is still sounding when the panic
# lands. After it, the rest of the run must be digital silence -- and the same
# stream without a panic must not be, or the case is proving nothing.
python3 -c "open('hold.syx','wb').write(bytes([0x90,60,100, 64,100, 67,100, 72,100]))"
$BIN --midi hold.syx --seconds 1 --wav hold.wav                 > h1.txt 2>&1
$BIN --midi hold.syx --seconds 1 --panic-at 0.3 --wav panic.wav > h2.txt 2>&1
python3 - <<'PYPANIC' || fail=1
import wave, array, sys
def tail_peak(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels()
    d = array.array('h'); d.frombytes(w.readframes(n)); w.close()
    half = (n // 2) * ch
    return max(abs(x) for x in d[half:]) if len(d) > half else 0
held, panicked = tail_peak('hold.wav'), tail_peak('panic.wav')
if held > 100 and panicked == 0:
    print("ok   panic silences held notes (tail peak %d -> %d)" % (held, panicked))
else:
    print("FAIL panic: tail peak %d -> %d" % (held, panicked)); sys.exit(1)
PYPANIC
grep -q "panic at block" h2.txt && echo "ok   panic reached the engine" || \
  { echo "FAIL panic not reported"; fail=1; }

echo
[ $fail -eq 0 ] && echo "all tests passed" || echo "FAILURES"
exit $fail
