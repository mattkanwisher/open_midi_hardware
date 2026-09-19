#!/bin/sh
# run.sh - Akumajou Dracula (X68000, Konami 1993), under MAME, into the port.
#
# The second game through the port, and the first with two MIDI modes. The
# game's boot menu offers Roland LA (MT-32 / CM-32L / CM-64) and Roland GS
# (SC-55 family) as separate soundtracks; this script starts mt32-desktop with
# the matching engine, then MAME's X68000 with its CZ-6BM1 MIDI board routed
# to the synth's CoreMIDI destination. No emulator source is touched: MAME has
# had the board (-exp1 x68k_midi) and a MIDI out port (-midiout) for years.
#
#   ./desktop/games/dracula/run.sh            # LA:  mt32emu, ROMs from ~/mt32roms
#   ./desktop/games/dracula/run.sh gs         # GS:  fluidsynth, ~/soundfonts/GeneralUser-GS.sf2
#   ./desktop/games/dracula/run.sh sc55       # GS:  Nuked-SC55 (the reference, not the port)
#   ./desktop/games/dracula/run.sh la --tap-wav x.wav   # extra args go to the synth
#
# "sc55" is the control group: the same MIDI into a cycle-accurate SC-55
# emulator (bench/vendor/Nuked-SC55, our fork with --virtual-port) instead of
# the port. Nothing of ours is in that chain, so it prints no counters; it is
# there so that what the GS SoundFont gets wrong can be heard next to what
# the real unit does.
#
#   DRACULA_DIR   where the two .dim images are      (default old_games/)
#   X68K_BIOS     directory with the X68000 .dat ROMs (default old_games/BIOS/Sharp/X68000)
#   MT32_ROMS     MT-32 ROM directory                  (default ~/mt32roms)
#   SOUNDFONT     SF2 for GS mode                     (default ~/soundfonts/GeneralUser-GS.sf2)
#   SC55_ROMS     SC-55 ROM directory for sc55 mode   (default ~/sc55roms)
#   NUKED_SC55    the nuked-sc55 binary               (default bench/vendor/Nuked-SC55/build-host/nuked-sc55)
#   MAME_SNAP_S   screenshot every N seconds into desktop/build/dracula/snap
#
# MAME shows a "known problems: imperfectly emulated graphics" screen first
# and waits for a key. Press one. After that the machine boots disk 1 to the
# music menu, and you can either press 2 (LA) or 3 (GS) in the window, or
# from another terminal:
#
#   echo "key 3" > desktop/build/dracula/cmd.txt
#
# which ctl.lua types into the emulated keyboard. "reset" gets the menu back.
# When MAME exits, mt32-desktop is sent SIGINT and its summary is printed.
#
# SPDX-License-Identifier: 0BSD

set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
MODE=${1:-la}; [ $# -gt 0 ] && shift
GAME=${DRACULA_DIR:-$REPO/old_games}
BIOS=${X68K_BIOS:-$REPO/old_games/BIOS/Sharp/X68000}
ROMS=${MT32_ROMS:-$HOME/mt32roms}
SF=${SOUNDFONT:-$HOME/soundfonts/GeneralUser-GS.sf2}
SC55=${SC55_ROMS:-$HOME/sc55roms}
NUKED=${NUKED_SC55:-$REPO/bench/vendor/Nuked-SC55/build-host/nuked-sc55}
BIN=$REPO/desktop/build/mt32-desktop
OUT=$REPO/desktop/build/dracula
LOG=$OUT/mt32-desktop.log
DISK1="$GAME/Akumajou Dracula (1993)(Konami)(Disk 1 of 2).dim"
DISK2="$GAME/Akumajou Dracula (1993)(Konami)(Disk 2 of 2).dim"

[ "$MODE" = sc55 ] || [ -x "$BIN" ] || { echo "build first: cmake -S desktop -B desktop/build && cmake --build desktop/build -j" >&2; exit 2; }
[ -f "$DISK1" ] && [ -f "$DISK2" ] || { echo "no Dracula .dim images in $GAME (set DRACULA_DIR)" >&2; exit 2; }
[ -f "$BIOS/iplrom.dat" ] && [ -f "$BIOS/cgrom.dat" ] || {
    echo "no X68000 ROMs in $BIOS (want iplrom.dat and cgrom.dat; set X68K_BIOS)" >&2; exit 2; }
command -v mame >/dev/null || { echo "mame not on PATH (brew install mame)" >&2; exit 2; }
mkdir -p "$OUT/roms" "$OUT/cfg" "$OUT/nvram" "$OUT/snap"

# MAME wants the ROM set as <rompath>/x68000/; a symlink to the BIOS directory
# is enough. -bios ipl11 (IPL 1.1, iplrom.dat) is named explicitly because the
# set also lists the CZ-600CE V1.0 chips, which we do not have and do not need.
ln -sfn "$BIOS" "$OUT/roms/x68000"

case "$MODE" in
la)
    if [ -f "$ROMS/MT32_CONTROL.ROM" ] && [ -f "$ROMS/MT32_PCM.ROM" ]; then
        ENGINE="--engine mt32emu --roms $ROMS --machine mt32"
    elif [ -f "$ROMS/CM32L_CONTROL.ROM" ] && [ -f "$ROMS/CM32L_PCM.ROM" ]; then
        ENGINE="--engine mt32emu --roms $ROMS --machine cm32l"
    else
        echo "no MT-32 ROMs in $ROMS -- using the fake engine (sine stand-in, not an MT-32)." >&2
        ENGINE="--engine fake"
    fi
    echo "LA mode: pick 2 at the music menu" ;;
gs)
    [ -f "$SF" ] || { echo "no SoundFont at $SF (set SOUNDFONT; GeneralUser GS is the one this was tested with)" >&2; exit 2; }
    ENGINE="--engine fluidsynth --soundfont $SF"
    echo "GS mode: pick 3 at the music menu" ;;
sc55)
    [ -x "$NUKED" ] || { echo "no nuked-sc55 at $NUKED (build bench/vendor/Nuked-SC55, or set NUKED_SC55)" >&2; exit 2; }
    [ -f "$SC55/sc55_rom1.bin" ] || { echo "no SC-55 ROMs in $SC55 (set SC55_ROMS)" >&2; exit 2; }
    echo "SC-55 mode (Nuked-SC55, not the port): pick 3 at the music menu" ;;
*)  echo "usage: $0 [la|gs|sc55] [synth args]" >&2; exit 2 ;;
esac

PORT=mt32-t113
if [ "$MODE" = sc55 ]; then
    PORT=sc55
    LOG=$OUT/nuked-sc55.log
    "$NUKED" -d "$SC55" --virtual-port "$PORT" -r gs "$@" > "$LOG" 2>&1 &
    READY='Opened virtual midi port'
else
    # --midi-seq none: publish the destination, connect no hardware sources.
    # --ring 8: a desktop asks for audio in bursts (desktop/README.md, Latency).
    "$BIN" --midi-seq none $ENGINE --ring 8 --status-ms 1000 "$@" > "$LOG" 2>&1 &
    READY='virtual destination'
fi
SYNTH=$!
cleanup() {
    if kill -0 "$SYNTH" 2>/dev/null; then
        kill -INT "$SYNTH" 2>/dev/null || true
        wait "$SYNTH" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

i=0
until grep -q "$READY" "$LOG" 2>/dev/null; do
    if ! kill -0 "$SYNTH" 2>/dev/null; then
        echo "synth exited before opening MIDI:" >&2; cat "$LOG" >&2; exit 1
    fi
    i=$((i+1)); [ "$i" -lt 100 ] || { echo "timed out waiting for the synth" >&2; cat "$LOG" >&2; exit 1; }
    sleep 0.1
done
echo "synth up ($SYNTH) on CoreMIDI port \"$PORT\", log: $LOG"

MAME_CTL=$OUT MAME_SNAP_S=${MAME_SNAP_S:-0} \
mame x68000 -bios ipl11 \
    -rompath "$OUT/roms" -cfg_directory "$OUT/cfg" -nvram_directory "$OUT/nvram" \
    -snapshot_directory "$OUT/snap" \
    -exp1 x68k_midi -midiout "$PORT" \
    -flop1 "$DISK1" -flop2 "$DISK2" \
    -window -nomax -resolution 1024x768 -skip_gameinfo \
    -autoboot_script "$HERE/ctl.lua" \
    > "$OUT/mame.log" 2>&1 || echo "mame exited $?" >&2

cleanup
trap - EXIT
if [ "$MODE" = sc55 ]; then
    echo; echo "=== nuked-sc55: no counters, it is the reference, not the port ==="
else
    echo; echo "=== mt32-desktop summary ($MODE) ==="
    sed -n '/^midi bytes/,$p' "$LOG"
fi
