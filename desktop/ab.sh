#!/bin/sh
# ab.sh - render one MIDI stream through this project's synthesiser and through
# somebody else's, and say numerically how far apart they are.
#
#   ./desktop/ab.sh                          # the `voice` vector, no ROMs
#   ./desktop/ab.sh capture.mid --roms ~/mt32roms
#   ./desktop/ab.sh capture.syx --roms ~/mt32roms --out /tmp/ab
#
# WHAT THIS IS FOR. The question behind it is "how do we sound against another
# MT-32 implementation?", and the way to ask it that survives being repeated is
# NOT to play a game through us and listen. A live stream from an emulator
# arrives with the jitter of whatever scheduled it, so two runs of the same
# game are two different MIDI streams and any difference you hear could be
# either machine. So: capture the stream ONCE (README.md, "From an emulator to
# an A/B"), then render that one file offline through every engine you have.
# That is what this does.
#
# WHAT IT COMPARES. Up to four legs, and they are not equal evidence:
#
#   ours           desktop/build/mt32-desktop -- this project's whole pipeline:
#                  mtp_midi_parser, mtp_render, the ring, the block scheduler,
#                  the engine. What we ship.
#   ref-seam       desktop/build/ab/ab_ref -- the same engine with none of the
#                  above: events straight into the synthesiser at sample-exact
#                  times. Isolates OUR code from the library's.
#   munt-smf2wav   Munt's own mt32emu-smf2wav, built out of bench/vendor/munt.
#                  The same library driven by its author's front end. This is
#                  the leg that actually answers the question -- and it needs
#                  real ROMs, so on a machine without them it does not run.
#   munt-orig      the same, on the ORIGINAL file rather than the normalised
#                  one, so that a disagreement between our SMF reader and
#                  libsmf's shows up as itself.
#
# WITHOUT ROMs THIS PROVES NOTHING ABOUT THE PRODUCT'S SOUND, and says so at
# the end of every run. The fabricated ROMs of emu/src/engine_mt32emu_fake_roms
# .cpp make the emulator run for real on meaningless data; no Roland data has
# ever been in this repository and none ever will be.
#
# Everything is written under desktop/build/ab. Nothing outside desktop/ is
# written to, exactly as conform.sh promises -- bench/vendor/munt is configured
# out of source into our own build tree.
#
# SPDX-License-Identifier: 0BSD

set -e
cd "$(dirname "$0")"
HERE=$(pwd)
ROOT=$HERE/..
W=$HERE/build/ab
MUNT=$ROOT/bench/vendor/munt

INPUT=
ROMS=${ROMS:-}
OUT=
MACHINE=mt32
ENGINE=
RATE=${RATE:-48000}
# Seconds rendered after the last event. 2, not a rounder number, because the
# `ours` leg stops two seconds after its MIDI source runs out whatever
# --seconds says (desktop/src/main.c:425-429, "enough silence for release tails
# and reverb to finish"). Asking the other legs for the same length means the
# comparison covers the whole of both rather than a prefix of one.
TAIL=${TAIL:-2}
PARTIALS=${PARTIALS:-32}

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

options:
  --roms DIR        real MT-32 ROMs; without them everything runs on the
                    fabricated ones and the audio is not MT-32 audio
  --machine NAME    mt32 (default) or cm32l
  --engine NAME     force the engine: mt32emu, mt32emu-fakerom, fake
  --out DIR         where the WAVs go (default: desktop/build/ab/out)
  --tail S          seconds rendered after the last event (default 2)
  --rate HZ         48000 (default) or 32000
EOF
}

while [ $# -gt 0 ]; do
    case $1 in
        --roms)    ROMS=$2; shift 2;;
        --machine) MACHINE=$2; shift 2;;
        --engine)  ENGINE=$2; shift 2;;
        --out)     OUT=$2; shift 2;;
        --tail)    TAIL=$2; shift 2;;
        --rate)    RATE=$2; shift 2;;
        -h|--help) usage; exit 0;;
        -*)        echo "ab.sh: unknown option $1"; usage; exit 2;;
        *)         INPUT=$1; shift;;
    esac
done

OUT=${OUT:-$W/out}
PREP=$W/prep
say() { printf '\n== %s\n' "$*"; }

# ---------------------------------------------------------------- 0. tools --

have() { command -v "$1" >/dev/null 2>&1; }
missing=
for t in cc c++ cmake python3; do have $t || missing="$missing $t"; done
[ -n "$missing" ] && { echo "missing tools:$missing"; exit 2; }
[ -d "$MUNT/mt32emu/src" ] || {
    echo "no $MUNT -- clone munt into bench/vendor first"; exit 2; }

mkdir -p $W $OUT $PREP
rm -f $OUT/*.wav $OUT/*.txt

# ------------------------------------------------------------- 1. the input --

if [ -z "$INPUT" ]; then
    say "no input given: using conform/vectors.py's \`voice\`"
    # The one vector that makes the fabricated ROMs sound at all: it programs
    # the System, Patch Temporary and Timbre Temporary areas over sysex before
    # it plays a note. A plain note-on renders exact silence on fake ROMs, and
    # comparing silence proves nothing (FINDINGS.md 8.3).
    python3 conform/vectors.py $PREP/vec >/dev/null
    INPUT=$PREP/vec/voice.syx
fi
[ -f "$INPUT" ] || { echo "ab.sh: no such file: $INPUT"; exit 2; }

say "preparing the stream"
NAME=stream
python3 ab/midiprep.py "$INPUT" $PREP --name $NAME
EVT=$PREP/$NAME.evt
MID=$PREP/$NAME.mid
RAW=$PREP/$NAME.raw

SPAN_US=$(sed -n 's/^# events [0-9]* *span_us \([0-9]*\)/\1/p' $EVT)
[ -n "$SPAN_US" ] || SPAN_US=0
FRAMES=$(python3 -c "print(int($SPAN_US * $RATE // 1000000) + $TAIL * $RATE)")
SECONDS_RUN=$(python3 -c "print('%.3f' % ($FRAMES / $RATE))")

# A stream with no time in it is delivered whole before the first frame, so
# every leg is deterministic and they must agree exactly. A stream with time in
# it is paced by a wall clock on the `ours` leg and by arithmetic everywhere
# else, so they must not be asserted equal. That distinction is the difference
# between a test and a measurement, and it decides --strict below.
if [ "$SPAN_US" = "0" ] && [ -f "$RAW" ]; then
    MODE=raw
else
    MODE=timed
fi
echo "  mode $MODE, $FRAMES frames ($SECONDS_RUN s at $RATE Hz)"

# ------------------------------------------------------------ 2. the engine --

if [ -z "$ENGINE" ]; then
    if [ -n "$ROMS" ]; then ENGINE=mt32emu; else ENGINE=mt32emu-fakerom; fi
fi
ROMARG=
[ -n "$ROMS" ] && ROMARG="--roms $ROMS --machine $MACHINE"

# --------------------------------------------------------------- 3. builds --

say "desktop/ (cmake)"
cmake -S $HERE -B $HERE/build >/dev/null
cmake --build $HERE/build -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
DESKTOP=$HERE/build/mt32-desktop
MT32INC=$HERE/build/mt32emu/include
[ -f "$HERE/build/mt32emu/libmt32emu.a" ] || {
    echo "the desktop build has no mt32emu -- there is nothing to compare"; exit 2; }

say "ab_ref (the reference front end)"
# Compiled into our own build tree against the library the cmake build already
# produced, so port/host/build and bench/ are untouched. The same trick
# conform.sh uses, and the same reason.
mkdir -p $W/obj
REF=$W/ab_ref
if [ ! -x "$REF" ] || [ ab/ab_ref.cpp -nt "$REF" ] ||
   [ $HERE/build/mt32emu/libmt32emu.a -nt "$REF" ]; then
    for f in $ROOT/port/host/host_storage_posix.c \
             $ROOT/port/host/host_log_stderr.c \
             $ROOT/port/host/engine_fake.c; do
        cc -O2 -g -Wall -Wextra -std=c99 -I$ROOT/port/include \
           -c -o $W/obj/$(basename $f .c).o $f
    done
    c++ -O2 -g -std=c++98 -fno-exceptions -fno-rtti \
        -DMTP_WITH_MT32EMU -DMTP_WITH_FAKEROM \
        -I$ROOT/port/include -c -o $W/obj/ab_ref.o ab/ab_ref.cpp
    c++ -O2 -g -std=c++98 -fno-exceptions -fno-rtti -DMTP_WITH_MT32EMU \
        -I$ROOT/port/include -I$MT32INC \
        -c -o $W/obj/engine_mt32emu.o $ROOT/port/host/engine_mt32emu.cpp
    c++ -O2 -g -std=c++98 -fno-exceptions -fno-rtti \
        -I$ROOT/port/include -I$MT32INC -I$MT32INC/mt32emu \
        -c -o $W/obj/engine_fakerom.o $ROOT/emu/src/engine_mt32emu_fake_roms.cpp
    c++ -o $REF $W/obj/*.o $HERE/build/mt32emu/libmt32emu.a -lm
    echo "  built $REF"
else
    echo "  up to date: $REF"
fi

# Munt's own renderer. It reads ROMs from files and hashes them, so there is no
# fabricated-ROM path into it: with no --roms this leg cannot exist, and the
# rig says so rather than substituting something and calling it Munt.
SMF2WAV=$W/munt/mt32emu_smf2wav/mt32emu-smf2wav
HAVE_MUNT=0
if [ -n "$ROMS" ]; then
    say "mt32emu-smf2wav (Munt's own renderer)"
    if [ ! -x "$SMF2WAV" ]; then
        if cmake -S $MUNT -B $W/munt -DCMAKE_BUILD_TYPE=Release \
                 -Dmunt_WITH_MT32EMU_QT=FALSE -Dmunt_WITH_MT32EMU_SMF2WAV=TRUE \
                 > $W/munt-configure.log 2>&1 &&
           cmake --build $W/munt -j"$(nproc 2>/dev/null || echo 4)" \
                 > $W/munt-build.log 2>&1; then
            echo "  built $SMF2WAV"
        else
            echo "  it did not build -- see $W/munt-build.log."
            echo "  It needs glib2 >= 2.32 (mt32emu_smf2wav/CMakeLists.txt:22):"
            echo "      apt install libglib2.0-dev"
        fi
    else
        echo "  up to date: $SMF2WAV"
    fi
    [ -x "$SMF2WAV" ] && HAVE_MUNT=1
fi

# ----------------------------------------------------------------- 4. runs --

say "rendering"

# ours: the whole pipeline, through the null device, which paces in real time.
# A timed stream therefore takes as long to render as it does to play.
printf '  %-14s' "ours"
if [ "$MODE" = raw ]; then
    $DESKTOP --audio null --engine $ENGINE $ROMARG --midi-raw "$INPUT" \
             --rate $RATE --ring 8 --status-ms 0 --partials $PARTIALS \
             --seconds $SECONDS_RUN --counters --tap-wav $OUT/ours.wav \
             > $OUT/ours.txt 2>&1 || true
else
    $DESKTOP --audio null --engine $ENGINE $ROMARG --midi-smf "$INPUT" \
             --rate $RATE --ring 8 --status-ms 0 --partials $PARTIALS \
             --seconds $SECONDS_RUN --counters --tap-wav $OUT/ours.wav \
             > $OUT/ours.txt 2>&1 || true
fi
[ -s $OUT/ours.wav ] && echo "ok" || { echo "FAILED -- see $OUT/ours.txt"; }

printf '  %-14s' "ref-seam"
$REF --events $EVT --wav $OUT/ref-seam.wav --engine $ENGINE $ROMARG \
     --rate $RATE --frames $FRAMES --partials $PARTIALS \
     > $OUT/ref-seam.txt 2>&1 || true
[ -s $OUT/ref-seam.wav ] && echo "ok" || { echo "FAILED -- see $OUT/ref-seam.txt"; }

if [ "$HAVE_MUNT" = 1 ]; then
    # -a 2 is AnalogOutputMode_ACCURATE, which is what both of our engines open
    # the synth with (port/host/engine_mt32emu.cpp:133) and what makes the
    # output rate exactly 48000. The three --record-max-*-silence -1 matter more
    # than they look: the default is 0, which TRIMS leading silence and would
    # shift the whole file against ours.
    printf '  %-14s' "munt-smf2wav"
    $SMF2WAV -m "$ROMS" -i $MACHINE -f -o $OUT/munt-smf2wav.wav \
             -a 2 -x $PARTIALS \
             --record-max-start-silence -1 --record-max-end-silence -1 \
             --record-max-la32-end-silence -1 -e $FRAMES \
             $MID > $OUT/munt-smf2wav.txt 2>&1 || true
    [ -s $OUT/munt-smf2wav.wav ] && echo "ok" ||
        echo "FAILED -- see $OUT/munt-smf2wav.txt"

    case "$INPUT" in
        *.mid|*.MID|*.smf)
            printf '  %-14s' "munt-orig"
            $SMF2WAV -m "$ROMS" -i $MACHINE -f -o $OUT/munt-orig.wav \
                     -a 2 -x $PARTIALS \
                     --record-max-start-silence -1 --record-max-end-silence -1 \
                     --record-max-la32-end-silence -1 -e $FRAMES \
                     "$INPUT" > $OUT/munt-orig.txt 2>&1 || true
            [ -s $OUT/munt-orig.wav ] && echo "ok" ||
                echo "FAILED -- see $OUT/munt-orig.txt"
            ;;
    esac
fi

# ------------------------------------------------------------- 5. the answer --

echo
STRICT=
[ "$MODE" = raw ] && STRICT=--strict
rc=0
python3 ab/abdiff.py $OUT --ref ours $STRICT || rc=$?

echo
if [ -n "$ROMS" ] && [ "$ENGINE" = mt32emu ]; then
cat <<NOTE
ROMs were supplied and the real engine ran. The WAVs in
$OUT are MT-32 audio and can be listened to.
Nobody in this repository has ever heard one: no ROM has been loaded in any
session that wrote this code, and no claim about how it sounds appears in any
document here. Listen, and write down what you hear -- that is evidence this
project does not have.
NOTE
else
cat <<NOTE
WHAT THIS RUN DID NOT SHOW. There were no ROMs, so every leg ran on the
fabricated images of emu/src/engine_mt32emu_fake_roms.cpp: a control ROM that
is mostly zeroes and a PCM ROM that is entirely zeroes, handed to the library
with the SHA-1 it expects. Every line of mt32emu executed; the sound is
meaningless. THIS RUN SAYS NOTHING ABOUT HOW THE PRODUCT SOUNDS and cannot be
compared with a recording of a real MT-32. What it does show is that the legs
above agree (or do not) about what the emulator computes, which is the part
this project can be wrong about on its own.

Munt's own renderer could not run at all: it loads ROMs from files and hashes
them, and there is no fabricated-ROM path into it. Supply --roms to get the
leg that answers the original question.
NOTE
fi
echo
echo "WAVs and logs: $OUT"
exit $rc
