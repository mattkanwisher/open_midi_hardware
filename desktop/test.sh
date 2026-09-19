#!/bin/sh
# test.sh - what this build can prove on a machine with no sound card, no MIDI
# hardware and no ROMs. Run from desktop/ after building.
#
# It is the same idea as port/host/test.sh -- assert on the counters, because
# the counters are the contract -- with two differences that are the point of
# this directory: the audio ring is being drained by a device on another thread
# rather than by a virtual cursor, and the MIDI bytes arrive through a file
# descriptor rather than out of a buffer.
#
# Every case here runs with --audio null. That backend paces in real time and
# plays nothing, so these assertions say nothing about whether audio is audible
# -- see FINDINGS.md.
#
# SPDX-License-Identifier: 0BSD

set -e
cd "$(dirname "$0")"
BIN=build/mt32-desktop
[ -x "$BIN" ] || { echo "build first: cmake -S . -B build && cmake --build build"; exit 2; }
mkdir -p build/t
cd build/t
BIN=../mt32-desktop
fail=0

# The null backend on a loaded or virtualised machine asks for audio in bursts
# of up to ~10 ms, four times the 2.67 ms block period, so a 3-block ring runs
# dry through no fault of the render loop. Every case uses a deeper ring; the
# summary explains this whenever it happens.
COMMON="--audio null --engine fake --ring 8 --status-ms 0"

expect() {  # expect <label> <pattern> <value> <output-file>
    got=$(grep -E "$2" "$4" | head -1 | awk '{print $NF}')
    if [ "$got" != "$3" ]; then
        echo "FAIL $1: expected $3, got '$got'"
        fail=1
    else
        echo "ok   $1 ($3)"
    fi
}
expect_grep() {  # expect_grep <label> <pattern> <file>
    if grep -q "$2" "$3"; then echo "ok   $1"
    else echo "FAIL $1: '$2' not in output"; fail=1; fi
}

# --- 1. the loop runs against a device that is pulling at it ---------------
$BIN $COMMON --seconds 2 --tap-wav silence.wav > r1.txt 2>&1
expect "idle underruns"     "^underruns"           0 r1.txt
expect "idle midi bytes"    "^midi bytes"          0 r1.txt
test -s silence.wav && echo "ok   wav tap written"

# --- 2. the demo wire stream, through stdin --------------------------------
# Running status, a display sysex, and an Active Sensing byte mid-stream.
cp ../../../port/host/demo.syx demo.syx 2>/dev/null || \
python3 -c "
open('demo.syx','wb').write(bytes([0x90,60,100, 64,100, 67,100, 72,100,
  0xF0,0x41,0x10,0x16,0x12,0x20,0x00,0x00]) +
  b'HELLO MT-32         ' + bytes([0x00,0xF7, 0xFE,
  0x80,60,0, 64,0, 67,0, 72,0]))"
$BIN $COMMON --midi-fifo - --tap-wav demo.wav < demo.syx > r2.txt 2>&1
grep -E "^messages" r2.txt | grep -q "8 short, 1 sysex, 1 realtime" \
  && echo "ok   demo parsed (8 short, 1 sysex, 1 realtime)" \
  || { echo "FAIL demo parse: $(grep -E '^messages' r2.txt)"; fail=1; }
expect "demo underruns"     "^underruns"           0 r2.txt
expect "demo orphan data"   "^parse: orphan data"  0 r2.txt
expect "demo realtime dropped" "^realtime dropped" 0 r2.txt
expect "demo sink stalls"   "^sink stalls"         0 r2.txt
python3 - <<'PY' || fail=1
import wave, array, sys
w = wave.open('demo.wav'); d = array.array('h'); d.frombytes(w.readframes(w.getnframes()))
peak = max(abs(x) for x in d)
print("ok   demo audio reached the device (peak %d)" % peak) if peak > 1000 else sys.exit(1)
PY

# --- 3. a 16 kB timbre bank dump, reassembled while audio keeps flowing ----
python3 - <<'PY'
def sysex(addr, data):
    body = list(addr) + list(data)
    return bytes([0xF0,0x41,0x10,0x16,0x12]) + bytes(body) + \
           bytes([(-sum(body)) & 0x7f, 0xF7])
out = bytearray([0x90,60,100, 64,100, 67,100])
for i in range(64):
    out += sysex((0x08,(i*2)&0x7f,0x00), bytes([(j*7+i)&0x7f for j in range(246)]))
    if i % 8 == 0:
        out += bytes([0xFE, 72+(i%5), 90])
out += bytes([0xB0,123,0])
open('bank.syx','wb').write(bytes(out))
PY
$BIN $COMMON --midi-fifo - < bank.syx > r3.txt 2>&1
grep -E "^messages" r3.txt | grep -q "12 short, 64 sysex" \
  && echo "ok   bank parsed (12 short, 64 sysex)" \
  || { echo "FAIL bank parse: $(grep -E '^messages' r3.txt)"; fail=1; }
expect "bank fifo overruns" "^midi fifo overruns"  0 r3.txt
expect "bank orphan data"   "^parse: orphan data"  0 r3.txt
expect "bank truncated"     "^parse: sysex > 32 kB" 0 r3.txt
expect "bank aborted"       "^parse: sysex aborted" 0 r3.txt
expect "bank underruns"     "^underruns"           0 r3.txt
expect "bank realtime dropped" "^realtime dropped" 0 r3.txt

# The safety margin. It is only meaningful once the ring has actually filled:
# from empty the first commit leaves occupancy 1 by definition, so the counter
# used to read 1 on every run that ever started while the live readout showed 4
# or 5 (desktop/FINDINGS.md 6.1). It now reports the steady state, or "n/a" if
# the ring never got there, and neither of those is ever a bare 1 here.
python3 - r3.txt <<'PYMQ' || fail=1
import re, sys
t = open(sys.argv[1]).read()
if re.search(r'^min ring occupancy\s+n/a', t, re.M):
    print("ok   min ring occupancy reported honestly as n/a")
    sys.exit(0)
m = re.search(r'^min ring occupancy\s+(\d+) of (\d+)\s+\(steady state, after '
              r'(\d+) start-up blocks\)', t, re.M)
if not m:
    print("FAIL min ring occupancy line is not in steady-state form")
    sys.exit(1)
mq, ring, startup = (int(x) for x in m.groups())
# The invariant, not a value: at least one commit excluded from the sample.
if startup >= 1 and 1 <= mq <= ring:
    print("ok   min ring occupancy is steady state: %d of %d, %d start-up "
          "block(s) excluded" % (mq, ring, startup))
else:
    print("FAIL min %d of %d with %d start-up blocks excluded"
          % (mq, ring, startup))
    sys.exit(1)
PYMQ

# worst_block_us was collected by the render loop and printed by nobody
# (emu/FINDINGS.md 8.5). It is the whole iteration -- MIDI drain plus render --
# which is what actually has to fit in a block period.
python3 - r3.txt <<'PYWB' || fail=1
import re, sys
t = open(sys.argv[1]).read()
r = int(re.search(r'^worst block render\s+(\d+) us', t, re.M).group(1))
b = int(re.search(r'^worst whole iteration\s+(\d+) us', t, re.M).group(1))
if b >= r:
    print("ok   worst whole iteration (%d us) >= worst render (%d us)" % (b, r))
else:
    print("FAIL whole iteration %d us < render %d us" % (b, r)); sys.exit(1)
PYWB

# --- 4. a stream that is wrong in three ways -------------------------------
python3 - <<'PY'
bad  = bytearray([40,50,60])                              # data with no status
bad += bytes([0xF0,0x41,0x10,0x16,0x12]) + bytes(100)     # sysex that never ends
bad += bytes([0x90,60,100])                               # ... a status aborts it
bad += bytes([0xF0]) + bytes(40000) + bytes([0xF7])       # sysex over 32 kB
bad += bytes([0x80,60,0])
open('bad.syx','wb').write(bytes(bad))
PY
$BIN $COMMON --midi-fifo - < bad.syx > r4.txt 2>&1
expect "bad fifo overruns"    "^midi fifo overruns"   0 r4.txt
expect "orphan data counted"  "^parse: orphan data"   3 r4.txt
expect "sysex aborted"        "^parse: sysex aborted" 1 r4.txt
expect "oversize refused"     "^parse: sysex > 32 kB" 1 r4.txt
grep -E "^messages" r4.txt | grep -q "2 short, 0 sysex" \
  && echo "ok   nothing malformed reached the engine" \
  || { echo "FAIL malformed leaked: $(grep -E '^messages' r4.txt)"; fail=1; }

# --- 5. a Standard MIDI File ------------------------------------------------
python3 - <<'PY'
import struct
def vlq(n):
    out=[n & 0x7f]; n >>= 7
    while n: out.insert(0, (n & 0x7f) | 0x80); n >>= 7
    return bytes(out)
trk  = vlq(0) + b'\xff\x51\x03' + bytes([0x07,0xa1,0x20])   # 120 bpm
trk += vlq(0) + b'\xc0\x30'                                  # program change
for i, note in enumerate([60,64,67,72]):                     # running status
    trk += vlq(0 if i == 0 else 240) + (b'\x90' if i == 0 else b'') + bytes([note,100])
body = bytes([0x41,0x10,0x16,0x12,0x20,0x00,0x00]) + b'SMF PLAYBACK OK     ' + bytes([0x00,0xf7])
trk += vlq(240) + b'\xf0' + vlq(len(body)) + body
for note in [60,64,67,72]:
    trk += vlq(240) + bytes([0x80,note,0])
trk += vlq(0) + b'\xff\x2f\x00'
open('t.mid','wb').write(b'MThd' + struct.pack('>IHHH',6,0,1,480) +
                         b'MTrk' + struct.pack('>I',len(trk)) + trk)
PY
$BIN $COMMON --midi-smf t.mid > r5.txt 2>&1
grep -E "^messages" r5.txt | grep -q "9 short, 1 sysex" \
  && echo "ok   smf parsed (9 short incl. expanded running status, 1 sysex)" \
  || { echo "FAIL smf parse: $(grep -E '^messages' r5.txt)"; fail=1; }
expect "smf underruns"      "^underruns"            0 r5.txt
expect_grep "smf ends the run by itself" "^audio produced" r5.txt

# --- 5b. gain and panic, the two controls the engine seam grew -------------
# desktop/FINDINGS.md 6.4: a desktop wants a master volume, and anything that
# can be stopped wants an all-notes-off on the way out. Both are now on the
# vtable, and both are asserted through the audio rather than through a log
# line: --gain 0 must produce digital silence, and the blocks rendered after
# the panic must be silent even though the notes were never released.
$BIN $COMMON --seconds 1 --gain 0 --midi-fifo - --tap-wav g0.wav < demo.syx > r5b.txt 2>&1
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

# Note ons with no note offs: the engine is still sounding when the run ends.
python3 -c "open('hold.syx','wb').write(bytes([0x90,60,100, 64,100, 67,100, 72,100]))"
$BIN $COMMON --seconds 1 --midi-fifo - --tap-wav hold.wav < hold.syx > r5c.txt 2>&1
expect_grep "panic runs on the way out" "panic: all notes off" r5c.txt
python3 - <<'PYPANIC' || fail=1
import wave, array, sys
w = wave.open('hold.wav'); n = w.getnframes(); ch = w.getnchannels()
d = array.array('h'); d.frombytes(w.readframes(n)); w.close()
tail = d[-512*ch:]                      # the last four blocks, after the panic
body = d[:n*ch//2]
if max(abs(x) for x in body) > 100 and max(abs(x) for x in tail) == 0:
    print("ok   held notes are silenced before the device closes")
else:
    print("FAIL panic: body peak %d, tail peak %d"
          % (max(abs(x) for x in body), max(abs(x) for x in tail)))
    sys.exit(1)
PYPANIC

# --- 6. the real engine, which must refuse to invent a ROM -----------------
if $BIN --help | grep -q mt32emu; then
    set +e
    $BIN --audio null --engine mt32emu --roms /nonexistent --seconds 1 > r6.txt 2>&1
    rc=$?
    set -e
    [ "$rc" != "0" ] && echo "ok   mt32emu without ROMs exits non-zero ($rc)" \
                     || { echo "FAIL mt32emu without ROMs exited 0"; fail=1; }
    expect_grep "mt32emu names the missing file" "MT32_CONTROL.ROM" r6.txt

    head -c 65536  /dev/urandom > CTRL.ROM
    head -c 524288 /dev/urandom > PCM.ROM
    set +e
    $BIN --audio null --engine mt32emu --control-rom CTRL.ROM --pcm-rom PCM.ROM \
         --seconds 1 > r7.txt 2>&1
    rc=$?
    set -e
    [ "$rc" != "0" ] && echo "ok   right-sized wrong-content ROMs rejected ($rc)" \
                     || { echo "FAIL bad ROMs accepted"; fail=1; }
    expect_grep "mt32emu says why" "not recognised" r7.txt
else
    echo "skip mt32emu cases (built without it)"
fi

# --- 7. the OS MIDI port, which may legitimately not exist -----------------
set +e
$BIN --audio null --midi-seq --seconds 1 > r8.txt 2>&1
set -e
if grep -q "unavailable\|no OS MIDI port" r8.txt; then
    echo "ok   --midi-seq fails cleanly where there is no sequencer"
else
    expect_grep "--midi-seq opened a port" "midi in:" r8.txt
fi

# --- 8. the serial path, against a pty -------------------------------------
# A pty is not a UART -- nothing here runs at 31250 baud in the electrical
# sense -- but it is a real tty, so TCSETS2/BOTHER (Linux) or IOSSIOSPEED
# (macOS) really is executed, the port really is put in raw 8N1 mode, and the
# bytes really do arrive one at a time through poll() and read().
python3 - <<'PYTTY' || fail=1
import os, pty, subprocess, sys, time
m, s = pty.openpty()
p = subprocess.Popen(["../mt32-desktop", "--audio", "null", "--engine", "fake",
                      "--ring", "8", "--midi-tty", os.ttyname(s) + ",31250",
                      "--seconds", "3", "--status-ms", "0"],
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
time.sleep(1.0)
for b in open("demo.syx", "rb").read():
    os.write(m, bytes([b]))
    time.sleep(0.00032)                 # 320 us: one byte at 31250 baud
out = p.communicate(timeout=30)[0]
open("r9.txt", "w").write(out)
ok = ("8 short, 1 sysex, 1 realtime" in out) and ("not 31250" not in out)
if not ok and sys.platform == "darwin" and "IOSSIOSPEED" in out:
    # macOS ptys reject IOSSIOSPEED (ENOTTY); only a real serial driver
    # (FTDI, CDC-ACM) takes it. The code path is right, the test double is
    # not. Verified 2026-09-19 on a Mac: this is the one case that cannot
    # be proven without an adapter on the desk.
    print("skip tty source: a macOS pty does not take IOSSIOSPEED; needs a real adapter")
    sys.exit(0)
print("ok   tty source: 31250 baud set, 49 bytes parsed" if ok else
      "FAIL tty source:\n" + out)
sys.exit(0 if ok else 1)
PYTTY

echo
[ "$fail" = "0" ] && echo "all tests passed" || echo "SOME TESTS FAILED"
exit $fail
