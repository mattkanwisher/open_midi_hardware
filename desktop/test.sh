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
# 45 assertions. The last four sections were added with the cross-
# implementation work: the deterministic --midi-raw source, mt32.cfg, the real
# mt32emu on fabricated ROMs, and the shared counter block that
# desktop/conform.sh asserts across port/host, emu/ and this build. For the
# four-way audio comparison itself, run ./conform.sh -- it is a separate
# command because it builds three other things.
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
# Underruns are the contract -- but on a desktop the *device* can stop asking
# for audio for longer than any ring we are allowed to configure can cover, and
# then the ring runs dry however fast the render loop is. That is this OS's
# scheduling granularity and it is what FINDINGS.md 5 is about. So: zero
# underruns passes; underruns with a measured device stall big enough to
# explain them are reported, with the numbers, and do not fail the suite;
# underruns with no such stall are a real failure of the render loop.
expect_underruns() {  # expect_underruns <label> <output-file>
    u=$(grep -E "^underruns" "$2" | head -1 | awk '{print $2}')
    if [ "$u" = "0" ]; then echo "ok   $1 (0 underruns)"; return 0; fi
    gap=$(grep "worst request gap" "$2" | awk '{print $4}')
    hold=$(grep "worst request gap" "$2" | sed 's/.*ring holds \([0-9]*\) us.*/\1/')
    if [ -n "$gap" ] && [ -n "$hold" ] && [ "$gap" -gt "$hold" ] 2>/dev/null; then
        echo "skip $1: $u underruns, but the device went $gap us between requests"
        echo "     and the ring holds only $hold us -- this machine's scheduler,"
        echo "     not the render loop. See FINDINGS.md 5."
    else
        echo "FAIL $1: $u underruns and no device stall to explain them"
        fail=1
    fi
}

expect_grep() {  # expect_grep <label> <pattern> <file>
    if grep -q "$2" "$3"; then echo "ok   $1"
    else echo "FAIL $1: '$2' not in output"; fail=1; fi
}

# --- 1. the loop runs against a device that is pulling at it ---------------
$BIN $COMMON --seconds 2 --tap-wav silence.wav > r1.txt 2>&1 || true
expect_underruns "idle underruns" r1.txt
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
$BIN $COMMON --midi-fifo - --tap-wav demo.wav < demo.syx > r2.txt 2>&1 || true
grep -E "^messages" r2.txt | grep -q "8 short, 1 sysex, 1 realtime" \
  && echo "ok   demo parsed (8 short, 1 sysex, 1 realtime)" \
  || { echo "FAIL demo parse: $(grep -E '^messages' r2.txt)"; fail=1; }
expect_underruns "demo underruns" r2.txt
expect "demo orphan data"   "^parse: orphan data"  0 r2.txt
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
$BIN $COMMON --midi-fifo - < bank.syx > r3.txt 2>&1 || true
grep -E "^messages" r3.txt | grep -q "12 short, 64 sysex" \
  && echo "ok   bank parsed (12 short, 64 sysex)" \
  || { echo "FAIL bank parse: $(grep -E '^messages' r3.txt)"; fail=1; }
expect "bank fifo overruns" "^midi fifo overruns"  0 r3.txt
expect "bank orphan data"   "^parse: orphan data"  0 r3.txt
expect "bank truncated"     "^parse: sysex > 32 kB" 0 r3.txt
expect "bank aborted"       "^parse: sysex aborted" 0 r3.txt
expect_underruns "bank underruns" r3.txt

# --- 4. a stream that is wrong in three ways -------------------------------
python3 - <<'PY'
bad  = bytearray([40,50,60])                              # data with no status
bad += bytes([0xF0,0x41,0x10,0x16,0x12]) + bytes(100)     # sysex that never ends
bad += bytes([0x90,60,100])                               # ... a status aborts it
bad += bytes([0xF0]) + bytes(40000) + bytes([0xF7])       # sysex over 32 kB
bad += bytes([0x80,60,0])
open('bad.syx','wb').write(bytes(bad))
PY
$BIN $COMMON --midi-fifo - < bad.syx > r4.txt 2>&1 || true
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
$BIN $COMMON --midi-smf t.mid > r5.txt 2>&1 || true
grep -E "^messages" r5.txt | grep -q "9 short, 1 sysex" \
  && echo "ok   smf parsed (9 short incl. expanded running status, 1 sysex)" \
  || { echo "FAIL smf parse: $(grep -E '^messages' r5.txt)"; fail=1; }
expect_underruns "smf underruns" r5.txt
expect_grep "smf ends the run by itself" "^audio produced" r5.txt

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
$BIN --audio null --midi-seq --seconds 1 > r8.txt 2>&1 || true
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
print("ok   tty source: 31250 baud set, 49 bytes parsed" if ok else
      "FAIL tty source:\n" + out)
sys.exit(0 if ok else 1)
PYTTY

# --- 9. the deterministic raw source: same bytes in, same PCM out ----------
# --midi-fifo cannot promise this and does not claim to: it is read by the
# poll thread, so an event can land one pump call later and therefore 128
# frames later. --midi-raw is pulled on the render thread, which is what makes
# desktop/conform.sh's comparison against port/host and emu/ mean anything.
$BIN $COMMON --midi-raw demo.syx --seconds 1 --tap-wav det1.wav > rA1.txt 2>&1 || true
$BIN $COMMON --midi-raw demo.syx --seconds 1 --tap-wav det2.wav > rA2.txt 2>&1 || true
# The two runs may stop a block apart -- mtp_render_pump() produces up to eight
# blocks at a time, so the last one can overshoot --seconds by a few. What must
# not differ is sample N, so compare the common prefix rather than the files.
python3 - <<'PYDET' || fail=1
import sys, wave, array
def rd(p):
    w = wave.open(p); a = array.array('h')
    a.frombytes(w.readframes(w.getnframes())); return a
a, b = rd('det1.wav'), rd('det2.wav')
n = min(len(a), len(b))
if n and a[:n] == b[:n]:
    print("ok   --midi-raw is reproducible (%d samples identical over two runs)" % n)
else:
    d = [i for i in range(n) if a[i] != b[i]]
    print("FAIL --midi-raw: %d of %d samples differ between two runs" % (len(d), n))
    sys.exit(1)
PYDET
grep -E "^messages" rA1.txt | grep -q "8 short, 1 sysex, 1 realtime" \
  && echo "ok   --midi-raw parsed the demo stream" \
  || { echo "FAIL --midi-raw parse: $(grep -E '^messages' rA1.txt)"; fail=1; }

# --- 10. mt32.cfg ----------------------------------------------------------
cat > t.cfg <<'CFG'
# a comment, and a blank line follow

block = 256
ring=8
reverb = off
machine = cm32l
desktop_status_ms = 0
nonsense_key = 3
a line with no equals sign
CFG
$BIN --audio null --engine fake --config t.cfg --midi-raw demo.syx \
     --seconds 1 --status-ms 0 > rB.txt 2>&1 || true
expect_grep "config: file read"        "config t.cfg: 9 lines, 6 settings" rB.txt
expect_grep "config: unknown key warns" "unknown key 'nonsense_key'"       rB.txt
expect_grep "config: bad line warns"    "no '=' -- ignored"                rB.txt
expect_grep "config: block applied"     "256 frames/block"                 rB.txt
expect_grep "config: ring applied"      "ring 8"                           rB.txt
expect_grep "config: reverb applied"    "reverb off"                       rB.txt
expect_grep "config: machine applied"   "machine cm32l"                    rB.txt
# ... and the command line beats the file.
$BIN --audio null --engine fake --config t.cfg --block 512 --ring 8 \
     --midi-raw demo.syx --seconds 1 --status-ms 0 > rC.txt 2>&1 || true
expect_grep "config: command line wins" "512 frames/block"                 rC.txt

# --- 11. the real synthesiser, on fabricated ROMs --------------------------
# This is the one case that runs every line of mt32emu -- the C++ runtime, the
# allocations Synth::open() makes, the LA32 -- with no Roland data anywhere.
if $BIN --help | grep -q mt32emu-fakerom; then
    python3 ../../conform/vectors.py . > /dev/null
    $BIN --audio null --engine mt32emu-fakerom --ring 8 --status-ms 0 \
         --midi-raw voice.syx --seconds 1 --counters --tap-wav voice.wav \
         > rD.txt 2>&1 || true
    expect_grep "fakerom: the synth opened" "engine              mt32emu-fakerom" rD.txt
    expect_underruns "fakerom underruns" rD.txt
    expect "fakerom midi bytes" "^midi bytes"        318 rD.txt
    expect "fakerom sysex"      "^sysex messages"      3 rD.txt
    peak=$(grep "^pcm peak" rD.txt | awk '{print $NF}')
    if [ "${peak:-0}" -gt 1000 ]; then
        echo "ok   fakerom rendered audible output (peak $peak)"
    else
        echo "FAIL fakerom rendered peak '$peak'; the voice vector should not"
        echo "     be silent -- see conform/vectors.py"
        fail=1
    fi
    # The same engine on the demo stream *is* silent, and that is not a bug:
    # the fabricated PCM ROM is zeroes and the machine's power-on state has
    # master volume 0. Asserting it keeps the distinction on the record.
    $BIN --audio null --engine mt32emu-fakerom --ring 8 --status-ms 0 \
         --midi-raw demo.syx --seconds 1 --counters > rE.txt 2>&1 || true
    expect "fakerom demo is silent" "^pcm nonzero samples" 0 rE.txt
    expect_grep "and says why" "the fabricated PCM ROM is all zeroes" rE.txt
else
    echo "skip mt32emu-fakerom cases (built without it)"
fi

# --- 12. --counters prints the shared contract, in the shared words --------
# port/host/main.c and emu/src/main.c print these lines; desktop/conform.sh
# asserts across all three. If this drifts, the three-way comparison silently
# stops comparing.
$BIN $COMMON --midi-raw demo.syx --seconds 1 --counters > rF.txt 2>&1 || true
for line in "^engine  " "^output  " "^blocks committed" "^midi bytes" \
            "^short messages" "^sysex messages" "^parser: short" \
            "^parser: orphan data" "^engine back-pressure" "^underruns" \
            "^worst render" "^min ring occupancy"; do
    grep -qE "$line" rF.txt || { echo "FAIL --counters is missing '$line'"; fail=1; }
done
echo "ok   --counters prints the whole shared counter contract"

echo
[ "$fail" = "0" ] && echo "all tests passed" || echo "SOME TESTS FAILED"
exit $fail
