#!/bin/sh
# run.sh - Space Quest III, under DOSBox-X, into the mt32-t113 port.
#
# Starts mt32-desktop with only its CoreMIDI virtual destination open, waits
# for that destination to exist, then launches DOSBox-X with SQ3 configured for
# a Roland MT-32. When the game exits (or you Ctrl-C), mt32-desktop is sent
# SIGINT and its summary -- bytes, messages, sysex, underruns, the parser's
# error counters -- is printed. Those counters are the test: MT32.DRV uploads
# a full timbre bank and a display message at start-up, and every one of those
# sysex packets must arrive whole.
#
#   ./desktop/games/sq3/run.sh                 # ROMs from $MT32_ROMS or ~/mt32roms
#   ./desktop/games/sq3/run.sh --tap-wav x.wav # anything extra goes to mt32-desktop
#   SQ3_DIR=/somewhere/else ./desktop/games/sq3/run.sh
#
# With no ROMs it runs on the fake engine: you hear a sine-wave stand-in, not
# an MT-32, but the MIDI path is exercised end to end and the counters are real.
#
# SPDX-License-Identifier: 0BSD

set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
GAME=${SQ3_DIR:-$REPO/old_games/SQ3}
ROMS=${MT32_ROMS:-$HOME/mt32roms}
BIN=$REPO/desktop/build/mt32-desktop
LOGDIR=$REPO/desktop/build/sq3
LOG=$LOGDIR/mt32-desktop.log

[ -x "$BIN" ] || { echo "build first: cmake -S desktop -B desktop/build && cmake --build desktop/build -j" >&2; exit 2; }
[ -f "$GAME/SCIV.EXE" ] && [ -f "$GAME/MT32.DRV" ] || {
    echo "no Space Quest III at $GAME (want SCIV.EXE and MT32.DRV; set SQ3_DIR)" >&2; exit 2; }
command -v dosbox-x >/dev/null || { echo "dosbox-x not on PATH (brew install dosbox-x)" >&2; exit 2; }
mkdir -p "$LOGDIR"

# Tell SQ3 to use its MT-32 driver. Sierra's INSTALL.EXE would write the same
# three lines; doing it here means the copy in old_games/ never needs installing.
if [ ! -f "$GAME/RESOURCE.CFG" ]; then
    cp "$HERE/RESOURCE.CFG" "$GAME/RESOURCE.CFG"
    echo "wrote $GAME/RESOURCE.CFG (soundDrv=MT32.DRV)"
elif ! grep -qi 'soundDrv *= *MT32.DRV' "$GAME/RESOURCE.CFG"; then
    echo "warning: $GAME/RESOURCE.CFG does not select MT32.DRV; the game will not send MIDI" >&2
fi

# Engine: the real one if ROMs are there, otherwise say so loudly and go on.
if [ -f "$ROMS/MT32_CONTROL.ROM" ] && [ -f "$ROMS/MT32_PCM.ROM" ]; then
    ENGINE="--engine mt32emu --roms $ROMS --machine mt32"
elif [ -f "$ROMS/CM32L_CONTROL.ROM" ] && [ -f "$ROMS/CM32L_PCM.ROM" ]; then
    ENGINE="--engine mt32emu --roms $ROMS --machine cm32l"
else
    echo "no MT-32 ROMs in $ROMS -- using the fake engine (sine stand-in, not an MT-32)." >&2
    echo "put MT32_CONTROL.ROM + MT32_PCM.ROM there, or set MT32_ROMS, to hear the real thing." >&2
    ENGINE="--engine fake"
fi

# --midi-seq none: publish the virtual destination but do not connect hardware
# sources, so a keyboard on the desk cannot play over the game. --ring 8 for
# the reason desktop/test.sh gives: a desktop asks for audio in bursts.
"$BIN" --midi-seq none $ENGINE --ring 8 --status-ms 1000 "$@" > "$LOG" 2>&1 &
SYNTH=$!
cleanup() {
    if kill -0 "$SYNTH" 2>/dev/null; then
        kill -INT "$SYNTH" 2>/dev/null || true
        wait "$SYNTH" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# Wait for CoreMIDI to have the destination before DOSBox-X enumerates them.
i=0
until grep -q 'virtual destination' "$LOG" 2>/dev/null; do
    if ! kill -0 "$SYNTH" 2>/dev/null; then
        echo "mt32-desktop exited before opening MIDI:" >&2; cat "$LOG" >&2; exit 1
    fi
    i=$((i+1)); [ "$i" -lt 50 ] || { echo "timed out waiting for mt32-desktop" >&2; cat "$LOG" >&2; exit 1; }
    sleep 0.1
done
echo "mt32-desktop up ($SYNTH), log: $LOG"

dosbox-x -conf "$HERE/dosbox-x.conf" -fastlaunch \
    -c "mount c \"$GAME\"" -c "c:" -c "sierra" -c "exit" \
    > "$LOGDIR/dosbox-x.log" 2>&1 || echo "dosbox-x exited $?" >&2

cleanup
trap - EXIT
echo
echo "=== mt32-desktop summary ==="
sed -n '/^midi bytes/,$p' "$LOG"
