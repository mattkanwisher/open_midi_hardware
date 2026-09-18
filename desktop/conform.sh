#!/bin/sh
# conform.sh - one command that runs the same MIDI through every implementation
# of the platform seam that exists, and compares the audio sample for sample.
#
#   ./desktop/conform.sh
#
# docs/PLAN.md 0.5 rule 2 says every implementation must pass the same
# conformance tests, and docs/HISTORY.md 5 records that one one-second render
# was byte-identical on x86-64 and on bare-metal ARM. This turns that anecdote
# into something reproducible, and widens it from one stream and two
# implementations to four streams and four:
#
#   host-x86      port/host, built here for x86-64
#   host-armv7    port/host, cross-built for Cortex-A7 and run under qemu-arm
#   emu-a7        emu/'s bare-metal image, from reset, under qemu-system-arm
#   desktop       this directory, against miniaudio's null device
#
# and two engines: port/host/engine_fake.c (eight sine voices, and therefore
# the libm of whichever platform it is on) and the real mt32emu opened on the
# fabricated ROMs of emu/src/engine_mt32emu_fake_roms.cpp.
#
# WHAT IS BUILT AND WHAT IS NOT. Nothing outside desktop/ is written to except
# emu/build, and that only if the bare-metal image is not already built with
# mt32emu -- `make -C emu mt32emu` is that directory's own target. port/host is
# not built with its own Makefile: its sources are compiled into
# desktop/build/conform, so a concurrent worker's build tree is never touched,
# and so that the fake-ROM engine can be linked in without editing anything in
# port/. See FINDINGS.md 8 for the one trick that needs.
#
# SPDX-License-Identifier: 0BSD

set -e
cd "$(dirname "$0")"
HERE=$(pwd)
ROOT=$HERE/..
W=$HERE/build/conform
OUT=$W/out
VEC=$W/vec

CROSS=${CROSS:-arm-linux-gnueabihf-}
QEMU_USER=${QEMU_USER:-qemu-arm}
QEMU_SYS=${QEMU_SYS:-qemu-system-arm}
SYSROOT=${SYSROOT:-/usr/arm-linux-gnueabihf/}
SECONDS_PER_RUN=${SECONDS_PER_RUN:-1}
MUNT=$ROOT/bench/vendor/munt/mt32emu

say() { printf '\n== %s\n' "$*"; }

# ---------------------------------------------------------------- 0. tools --

have() { command -v "$1" >/dev/null 2>&1; }
missing=
for t in cc c++ cmake python3; do have $t || missing="$missing $t"; done
[ -n "$missing" ] && { echo "missing tools:$missing"; exit 2; }
HAVE_ARM=1
for t in ${CROSS}gcc ${CROSS}g++ $QEMU_USER; do
    have $t || HAVE_ARM=0
done
HAVE_SYS=1
have $QEMU_SYS || HAVE_SYS=0
[ -d "$MUNT/src" ] || { echo "no $MUNT -- clone munt into bench/vendor first"; exit 2; }

mkdir -p $W $OUT $VEC
rm -f $OUT/*.wav $OUT/*.txt

# ---------------------------------------------------- 1. the MIDI vectors --

say "MIDI vectors"
python3 conform/vectors.py $VEC --verify $ROOT/emu/src/midi_vectors.c || {
    echo "the shared vectors are NOT the bytes the bare-metal image carries."
    echo "The bare-metal leg would be answering a different question, so it is"
    echo "dropped from this run rather than quietly compared."
    HAVE_SYS=0
}

# ------------------------------------------------- 2. desktop's own binary --

say "desktop/ (cmake)"
cmake -S $HERE -B $HERE/build >/dev/null
cmake --build $HERE/build -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
DESKTOP=$HERE/build/mt32-desktop
$DESKTOP --help | grep -q mt32emu-fakerom || {
    echo "this desktop build has no --engine mt32emu-fakerom; cannot compare"
    echo "the real synthesiser. Is emu/src/engine_mt32emu_fake_roms.cpp there?"
    exit 2
}
# The private mt32emu headers and the generated config.h that the cmake build
# staged for us; the host harnesses below compile against exactly those.
MT32INC=$HERE/build/mt32emu/include

# ------------------------------------- 3. port/host, x86-64 and armv7, here --
# Compiled into our own build tree, never into port/host/build.

PORT_C="$ROOT/port/src/mtp_midi_parser.c $ROOT/port/src/mtp_render.c
        $ROOT/port/host/host_audio_wav.c $ROOT/port/host/host_midi_file.c
        $ROOT/port/host/host_storage_posix.c $ROOT/port/host/host_time_posix.c
        $ROOT/port/host/host_log_stderr.c $ROOT/port/host/engine_fake.c
        $ROOT/port/host/main.c"
MT32EMU_CPP="Analog BReverbModel Display File LA32FloatWaveGenerator LA32Ramp
             LA32WaveGenerator MidiStreamParser Part Partial PartialManager
             Poly ROMInfo Synth Tables TVA TVF TVP"

# port/host/main.c knows two engine names, "fake" and "mt32emu", and the second
# resolves to the symbol mtp_engine_mt32emu. emu/'s fake-ROM engine exports
# mtp_engine_mt32emu_fakerom instead. Renaming the symbol at the preprocessor
# rather than editing port/host/main.c is what lets this harness drive the real
# synthesiser through an unmodified port/host: --engine mt32emu gets the
# fabricated ROMs, and the binary says so, because the vtable's *name* string
# is still "mt32emu-fakerom" and every run's output carries it.
FAKEROM=$ROOT/emu/src/engine_mt32emu_fake_roms.cpp
RENAME=-Dmtp_engine_mt32emu_fakerom=mtp_engine_mt32emu

build_host() {   # build_host <tag> <cc> <cxx> <extra-cflags>
    tag=$1; cc=$2; cxx=$3; extra=$4
    d=$W/$tag
    mkdir -p $d/mt32emu
    if [ ! -f $d/mt32emu/libmt32emu.a ]; then
        echo "  building libmt32emu for $tag (18 files)"
        for f in $MT32EMU_CPP; do
            $cxx -O2 -g -std=c++98 -fno-exceptions -fno-rtti $extra \
                 -DMT32EMU_WITH_TESTING -I$MT32INC -I$MT32INC/mt32emu \
                 -c -o $d/mt32emu/$f.o $MUNT/src/$f.cpp &
        done
        wait
        $cxx -O2 -g -std=c++98 -fno-exceptions -fno-rtti $extra \
             -I$MT32INC -I$MT32INC/mt32emu \
             -c -o $d/mt32emu/sha1.o $MUNT/src/sha1/sha1.cpp
        ${CROSS_AR:-ar} rcs $d/mt32emu/libmt32emu.a $d/mt32emu/*.o 2>/dev/null ||
            ${cc%gcc}ar rcs $d/mt32emu/libmt32emu.a $d/mt32emu/*.o
    fi
    echo "  building mtp_host for $tag"
    for f in $PORT_C; do
        $cc -O2 -g -Wall -Wextra -std=c99 $extra -DMTP_WITH_MT32EMU \
            -I$ROOT/port/include -c -o $d/$(basename $f .c).o $f
    done
    $cxx -O2 -g -std=c++98 -fno-exceptions -fno-rtti $extra -DMTP_WITH_MT32EMU \
         $RENAME -I$ROOT/port/include -I$MT32INC \
         -c -o $d/engine_fakerom.o $FAKEROM
    $cxx $extra -o $d/mtp_host $d/*.o $d/mt32emu/libmt32emu.a -lm
}

say "port/host for x86-64"
build_host x86 cc c++ ""

ARMFLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm"
if [ "$HAVE_ARM" = 1 ]; then
    say "port/host cross-built for Cortex-A7"
    # boot/BRINGUP.md 6.1 and port/PORTING.md 4.2: Cortex-A7, NEON-VFPv4, hard
    # float. The same flags emu/ and bench/ use, so this is the same code
    # generation the board will run, minus the operating system.
    CROSS_AR=${CROSS}ar build_host armv7 ${CROSS}gcc ${CROSS}g++ "$ARMFLAGS"

    # ...and the same again with fused multiply-add turned off. This second
    # leg exists because of what the first one found: GCC contracts the
    # multiply-accumulate in mt32emu's AccurateLowPassFilter::process()
    # (Analog.cpp:392) into VFMA on armv7, which rounds once where x86-64's
    # SSE2 rounds twice, and the rendered PCM then differs by 1 LSB on about
    # 0.07% of samples. -ffp-contract=off removes exactly that difference.
    # Keeping both legs in the run means the finding stays visible and the
    # remedy stays proven. FINDINGS.md 8.2.
    CROSS_AR=${CROSS}ar build_host armv7-fpoff ${CROSS}gcc ${CROSS}g++ \
        "$ARMFLAGS -ffp-contract=off"
else
    echo "no ${CROSS}gcc or $QEMU_USER: skipping the armv7 leg"
fi

# ------------------------------------------------ 4. the bare-metal image --

EMU_ELF=$ROOT/emu/build/mt32emu-bare.elf
if [ "$HAVE_SYS" = 1 ]; then
    say "emu/ bare-metal image"
    if [ -f "$EMU_ELF" ] && ${CROSS}nm "$EMU_ELF" 2>/dev/null |
         grep -q mtp_engine_mt32emu_fakerom; then
        echo "  using the image already built: $(basename $EMU_ELF)"
    else
        echo "  building it: make -C ../emu mt32emu"
        ( cd $ROOT/emu && make mt32emu >/dev/null ) || {
            echo "  emu/ did not build (another workstream owns it); skipping"
            HAVE_SYS=0
        }
    fi
    # That directory is owned by another workstream and is being worked on, so
    # snapshot the image and record what was run rather than trusting the path.
    if [ "$HAVE_SYS" = 1 ]; then
        cp $EMU_ELF $W/emu-snapshot.elf
        EMU_ELF=$W/emu-snapshot.elf
        (sha256sum $EMU_ELF 2>/dev/null || shasum -a 256 $EMU_ELF) | sed 's|^|  |'
    fi
else
    echo "no $QEMU_SYS: skipping the bare-metal leg"
fi

# ---------------------------------------------------------------- 5. runs --

# Shell functions share the caller's variables, so every name in here is
# prefixed: an earlier version of this script assigned `eng` inside run_host
# and silently ran the whole matrix with the wrong engine.
run_host() {  # run_host <impl> <bin-prefix> <engine> <vector>
    r_impl=$1; r_pre=$2; r_eng=$3; r_vec=$4
    $r_pre --midi $VEC/$r_vec.syx --engine $r_eng --seconds $SECONDS_PER_RUN \
         --wav $OUT/${r_impl}__${r_eng}__${r_vec}.wav \
         > $OUT/${r_impl}__${r_eng}__${r_vec}.txt 2>&1 || true
}

# The host harnesses were told "mt32emu" and ran the fake-ROM engine; name the
# files for what actually ran so compare.py groups them with everyone else's.
rename_run() {  # rename_run <impl> <ran-as> <real-name> <vector>
    [ "$2" = "$3" ] && return 0
    for ext in txt wav; do
        [ -f $OUT/$1__$2__$4.$ext ] && mv $OUT/$1__$2__$4.$ext $OUT/$1__$3__$4.$ext
    done
    return 0
}

run_emu() {   # run_emu <engine> <vector>
    r_eng=$1; r_vec=$2
    $QEMU_SYS -M virt -cpu cortex-a7 -m 256 -nographic -nodefaults \
      -serial mon:stdio -no-reboot \
      -semihosting-config enable=on,target=native,arg=x,arg=--midi,arg=$r_vec,arg=--engine,arg=$r_eng,arg=--seconds,arg=$SECONDS_PER_RUN,arg=--wav,arg=$OUT/emu-a7__${r_eng}__${r_vec}.wav \
      -kernel $EMU_ELF > $OUT/emu-a7__${r_eng}__${r_vec}.txt 2>&1 || true
}

run_desktop() {  # run_desktop <engine> <vector>
    r_eng=$1; r_vec=$2
    # --ring 8 because a desktop's device wake-up granularity, not the render
    # loop, is what empties a 3-block ring here (FINDINGS.md 5). Ring depth
    # changes how many blocks a pump call produces; it cannot change what is
    # in them, which is what this script compares.
    $DESKTOP --audio null --engine $r_eng --midi-raw $VEC/$r_vec.syx \
             --ring 8 --status-ms 0 --seconds $SECONDS_PER_RUN --counters \
             --tap-wav $OUT/desktop__${r_eng}__${r_vec}.wav \
             > $OUT/desktop__${r_eng}__${r_vec}.txt 2>&1 || true
}

# The engine name the host harnesses answer to is "mt32emu" (see RENAME above);
# everything else calls the same engine "mt32emu-fakerom". The output files are
# named for what actually ran.
say "running the matrix"
for vec in demo bank bad voice; do
    for eng in fake mt32emu-fakerom; do
        hosteng=$eng
        [ "$eng" = "mt32emu-fakerom" ] && hosteng=mt32emu

        printf '  %-16s %-8s :' "$eng" "$vec"

        run_host host-x86 "$W/x86/mtp_host" $hosteng $vec
        rename_run host-x86 $hosteng $eng $vec
        printf ' host-x86'

        if [ "$HAVE_ARM" = 1 ]; then
            run_host host-armv7 "$QEMU_USER -L $SYSROOT $W/armv7/mtp_host" \
                     $hosteng $vec
            rename_run host-armv7 $hosteng $eng $vec
            printf ' host-armv7'
            run_host armv7-fpoff \
                     "$QEMU_USER -L $SYSROOT $W/armv7-fpoff/mtp_host" \
                     $hosteng $vec
            rename_run armv7-fpoff $hosteng $eng $vec
            printf ' armv7-fpoff'
        fi

        run_desktop $eng $vec
        printf ' desktop'

        # The bare-metal image plays streams compiled into it, so it can only
        # take the three shared vectors. `voice` is this directory's and
        # emu/src/midi_vectors.c is not ours to add to -- see FINDINGS.md 8.4.
        if [ "$HAVE_SYS" = 1 ] && [ "$vec" != "voice" ]; then
            run_emu $eng $vec
            printf ' emu-a7'
        fi
        printf '\n'
    done
done

# ------------------------------------------------------------ 6. the answer --

echo
python3 conform/compare.py $OUT
rc=$?
echo
cat <<'NOTE'
EXPECTED STATE, 2026-09-18. This run reports exactly one divergence, and it is
a real one, not a flake: host-armv7 built with the project's current flags
renders the mt32emu `voice` vector 1 LSB differently from every other
implementation, on 69 of 96256 samples, because GCC contracts the
multiply-accumulate in mt32emu's Analog.cpp:392 into VFMA. The armv7-fpoff leg
is the same compiler and the same source with -ffp-contract=off, and it agrees
with x86-64 exactly. If this script ever reports more than that one line, or a
different one, something changed. FINDINGS.md 8.2 has the bisection.
NOTE
echo "runs and their output are in $OUT"
exit $rc
