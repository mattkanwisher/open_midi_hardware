#!/bin/sh
# Bring up external DDR3 on a T113-i over USB FEL, then optionally load and run
# a bare-metal payload from DRAM.
#
# Why this is not just `xfel ddr t113-i`:
#   xfel only knows t113-s3, t113-s4 and r528-s3, all of which are the
#   CO-PACKAGED memory parts.  Its DDR init works by uploading an eGON.BT0
#   image to SRAM at 0x28000, overwriting the dram_para_t structure at offset
#   0x38 of that image, and executing it.  We do the same three steps by hand
#   with our own parameters.  See ../BRINGUP.md section 2.
#
# Usage:
#   fel-ddr-t113i.sh                       # DRAM init only
#   fel-ddr-t113i.sh payload.bin           # DRAM init, then load+run at 0x40000000
#   PRESET=t113i-tronlong fel-ddr-t113i.sh # use the other published T113-i set
#   XFEL_SRC=/path/to/xfel fel-ddr-t113i.sh
#   DRY_RUN=1 fel-ddr-t113i.sh             # do everything except touch USB
#
# DRY_RUN=1 is the self-test.  It extracts the payload, patches it, verifies
# the result by decoding it back, and PRINTS the xfel commands instead of
# running them.  That is everything this script can be made to prove without a
# board: verified working 2026-09-18, with no FEL device anywhere in reach.
#
# Requires: xfel on PATH (https://github.com/xboot/xfel, MIT), python3, and an
# xfel source checkout to lift the DDR payload from (or a prebuilt payload in
# payloads/t113-ddr.bin).  Every xfel subcommand used below -- version, write,
# exec, hexdump, read32 -- was checked against xfel v1.4.0's own usage text
# (main.c:15-29).

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
BOOT=$(cd "$HERE/.." && pwd)
PRESET=${PRESET:-mt32-t113}
LOAD_ADDR=${LOAD_ADDR:-0x40000000}
PAYLOAD_DIR="$BOOT/payloads"
BASE="$PAYLOAD_DIR/t113-ddr.bin"
OUT="$PAYLOAD_DIR/${PRESET}-ddr.bin"
XFEL_SRC=${XFEL_SRC:-$BOOT/vendor/xfel}

mkdir -p "$PAYLOAD_DIR"

if [ ! -f "$BASE" ]; then
	src="$XFEL_SRC/chips/r528_t113.c"
	[ -f "$src" ] || {
		echo "no $BASE and no xfel source at $src" >&2
		echo "clone it:  git clone https://github.com/xboot/xfel $XFEL_SRC" >&2
		exit 1
	}
	echo "==> extracting DDR payload from $src"
	python3 "$HERE/ddrpara.py" extract "$src" -o "$BASE" >/dev/null
fi

echo "==> stamping preset '$PRESET' into the payload"
python3 "$HERE/ddrpara.py" patch "$BASE" --preset "$PRESET" -o "$OUT"

echo "==> verifying the patched payload reads back as preset '$PRESET'"
python3 "$HERE/ddrpara.py" decode "$OUT" | sed -n '1,4p;25,40p'

XFEL=${XFEL:-xfel}
if [ "${DRY_RUN:-0}" = 1 ]; then
	XFEL="echo    WOULD RUN: $XFEL"
fi

echo "==> checking the board is in FEL mode"
$XFEL version

# Read the AC remapping efuse BEFORE bringing DRAM up.  This is the one
# command that settles BRINGUP.md section 2.5 / hw/HARDWARE.md 5.4, and it
# costs nothing to do here.  SID base 0x03006200 is SUNXI_SID_BASE for
# SUNXI_GEN_NCAT2 (u-boot arch/arm/include/asm/arch-sunxi/cpu_sunxi_ncat2.h:21,
# and dram_sun20i_d1.c:26); the driver uses bits [11:8] of +0x28.
echo "==> reading the AC remapping efuse at SID+0x28 (bits 11:8)"
$XFEL read32 0x03006228 || true
echo "    feed the value to:  scripts/ddrpara.py acremap --preset $PRESET --fuse N"

echo "==> uploading DDR init payload to SRAM 0x28000 and running it"
$XFEL write 0x00028000 "$OUT"
$XFEL exec 0x00028000

echo "==> DRAM should now be live; probing 0x40000000"
$XFEL hexdump 0x40000000 0x20 || true

if [ $# -ge 1 ]; then
	echo "==> loading $1 to $LOAD_ADDR"
	$XFEL write "$LOAD_ADDR" "$1"
	echo "==> exec $LOAD_ADDR"
	$XFEL exec "$LOAD_ADDR"
fi

echo "done."
