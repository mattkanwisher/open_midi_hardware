#!/bin/sh
# build-uboot.sh -- clone mainline U-Boot and build it for this board, from a
# clean tree, in one command.
#
#   tools/build-uboot.sh                  # our board: t113i_mt32
#   tools/build-uboot.sh mangopi_mq_r     # the in-tree T113-S3 board
#   tools/build-uboot.sh orangepi_pc      # the H3, for tools/handover-probe.sh
#   REF=v2026.07 tools/build-uboot.sh     # pin a different U-Boot
#
# Everything lands under boot/vendor/, which .gitignore excludes.  Nothing
# outside boot/ is touched, and the U-Boot checkout is never modified except
# for two files this board needs and mainline does not have:
#   configs/t113i_mt32_defconfig                          <- ../configs/
#   dts/upstream/src/arm/allwinner/sun8i-t113i-mt32.dts   <- ../dts/
# Both are copied in on every run, so editing ours and re-running is the loop.
#
# Verified on 2026-09-18 against U-Boot commit
# 211de43d0f954a00a490220c1aac9db298287c40 (v2026.10-rc4-34-g211de43d0f).
#
# Host packages needed beyond a cross toolchain, learned the hard way by
# hitting each one in turn:
#   gcc-arm-linux-gnueabihf  flex  bison  swig  python3-dev  libssl-dev
#   libgnutls28-dev  uuid-dev
# (flex for the Kconfig lexer, swig + python3-dev for scripts/dtc/pylibfdt,
#  libgnutls28-dev for tools/mkeficapsule, libssl-dev for the FIT signing
#  helpers.  U-Boot builds tools/ unconditionally, so all of them are
#  required even though none of them is needed by the SPL.)
#
# SPDX-License-Identifier: 0BSD
set -eu

BOARD=${1:-t113i_mt32}
HERE=$(cd "$(dirname "$0")" && pwd)
BOOT=$(cd "$HERE/.." && pwd)
VENDOR="$BOOT/vendor"
SRC="$VENDOR/u-boot"
OUT="$VENDOR/build/$BOARD"
CROSS=${CROSS_COMPILE:-arm-linux-gnueabihf-}
REF=${REF:-}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

command -v "${CROSS}gcc" >/dev/null || {
	echo "no ${CROSS}gcc on PATH" >&2; exit 1; }
for t in flex bison swig; do
	command -v $t >/dev/null || {
		echo "missing host tool: $t (see the header of this script)" >&2
		exit 1; }
done

if [ ! -d "$SRC/.git" ]; then
	echo "==> cloning mainline U-Boot into $SRC"
	mkdir -p "$VENDOR"
	git clone --filter=blob:none https://github.com/u-boot/u-boot.git "$SRC"
fi
if [ -n "$REF" ]; then
	echo "==> checking out $REF"
	git -C "$SRC" fetch --tags origin
	git -C "$SRC" checkout --detach "$REF"
fi

COMMIT=$(git -C "$SRC" rev-parse HEAD)
DESC=$(git -C "$SRC" describe --tags 2>/dev/null || echo "?")
echo "==> u-boot $DESC  ($COMMIT)"

# Our two out-of-tree files.
if [ -f "$BOOT/configs/${BOARD}_defconfig" ]; then
	echo "==> installing configs/${BOARD}_defconfig"
	cp "$BOOT/configs/${BOARD}_defconfig" "$SRC/configs/"
fi
for dts in "$BOOT"/dts/*.dts; do
	[ -e "$dts" ] || continue
	cp "$dts" "$SRC/dts/upstream/src/arm/allwinner/"
done

echo "==> configuring ($OUT)"
mkdir -p "$OUT"
make -C "$SRC" O="$OUT" CROSS_COMPILE="$CROSS" "${BOARD}_defconfig"

echo "==> building with -j$JOBS"
# </dev/null matters: drivers/ram/sunxi/Kconfig gives the five DRAM_SUNXI_*
# hex symbols no default, so a defconfig that forgets one makes syncconfig
# PROMPT.  With a tty it hangs; with /dev/null it fails loudly instead.
make -C "$SRC" O="$OUT" CROSS_COMPILE="$CROSS" -j"$JOBS" </dev/null

echo
echo "==> artefacts"
ls -l "$OUT/spl/sunxi-spl.bin" "$OUT/u-boot.bin" "$OUT/u-boot.img" \
      "$OUT/u-boot-sunxi-with-spl.bin" 2>/dev/null || true
echo
echo "    SPL load address : $(grep -E '^CONFIG_SPL_TEXT_BASE=' "$OUT/.config" | cut -d= -f2)"
echo "    SRAM window      : 160 KiB at 0x20000 (sunxi-tools soc_info.c, .soc_id 0x1859)"
echo "    SPL image size   : $(stat -c %s "$OUT/spl/sunxi-spl.bin") bytes"
echo
echo "    dd if=$OUT/u-boot-sunxi-with-spl.bin of=/dev/sdX bs=1k seek=8"
echo "    sunxi-fel uboot $OUT/u-boot-sunxi-with-spl.bin"
echo
echo "    record this commit in BRINGUP.md if it changed: $COMMIT"
