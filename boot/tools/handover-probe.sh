#!/bin/sh
# Build handover-probe.S, wrap it for `bootm`, drop it into an SD image beside
# a sunxi U-Boot, and boot the whole thing under QEMU's Allwinner H3 model.
#
# What it proves and what it does not: see ../BRINGUP.md section 4.4.  The SoC
# is an H3, not a T113, because QEMU has no T113 model.  The code under test --
# arch/arm/lib/bootm.c, arch/arm/cpu/armv7/cpu.c, arch/arm/cpu/armv7/
# nonsec_virt.S -- is ARMv7 architecture code shared with MACH_SUN8I_R528.
#
# Usage:  tools/handover-probe.sh [go|bootm|bootm-sec]   (default: all three)
#
# SPDX-License-Identifier: 0BSD
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
BOOT=$(cd "$HERE/.." && pwd)
UBOOT=${UBOOT:-$BOOT/vendor/u-boot}
BUILD=${BUILD:-$BOOT/vendor/build/opipc}
OUT=${OUT:-$BOOT/vendor/build/probe}
CROSS=${CROSS:-arm-linux-gnueabihf-}
QEMU=${QEMU:-qemu-system-arm}

[ -f "$BUILD/u-boot-sunxi-with-spl.bin" ] || {
	echo "no $BUILD/u-boot-sunxi-with-spl.bin" >&2
	echo "build it first:  tools/build-uboot.sh orangepi_pc" >&2
	exit 1
}

mkdir -p "$OUT"
echo "==> assembling the probe"
${CROSS}gcc -x assembler-with-cpp -c -o "$OUT/probe.o" "$HERE/handover-probe.S"
${CROSS}ld -Ttext=0x40200000 -e _start -o "$OUT/probe.elf" "$OUT/probe.o"
${CROSS}objcopy -O binary "$OUT/probe.elf" "$OUT/probe.bin"

echo "==> wrapping it for bootm"
"$BUILD/tools/mkimage" -A arm -O linux -T kernel -C none \
	-a 0x40200000 -e 0x40200000 -n handover-probe \
	-d "$OUT/probe.bin" "$OUT/probe.uimg"

# Sector 2048 is past the 8 KB SPL and the ~600 KB of U-Boot that follows it.
PROBE_BLK=2048        # decimal, for dd
PROBE_BLK_HEX=800     # the same number in hex, because U-Boot parses
                      # `mmc read` arguments as hexadecimal
echo "==> building an SD image: SPL+U-Boot at 8 KB, probe at sector $PROBE_BLK"
dd if=/dev/zero of="$OUT/sd.img" bs=1M count=64 status=none
dd if="$BUILD/u-boot-sunxi-with-spl.bin" of="$OUT/sd.img" bs=1024 seek=8 \
	conv=notrunc status=none
dd if="$OUT/probe.uimg" of="$OUT/sd.img" bs=512 seek=$PROBE_BLK \
	conv=notrunc status=none
BLKS=$(printf %x $(( ($(stat -c %s "$OUT/probe.uimg") + 511) / 512 )))

run() {
	name=$1; shift
	echo
	echo "######## $name ########"
	printf '%s\n' "$@" > "$OUT/cmds.txt"
	# The leading newlines interrupt autoboot.
	{ printf '\n\n\n'; sleep 5; cat "$OUT/cmds.txt"; sleep 4; } |
	timeout 40 "$QEMU" -M orangepi-pc -nographic -serial mon:stdio \
		-drive file="$OUT/sd.img",if=sd,format=raw -display none \
		2>&1 | grep -aE 'PROBE|Starting application|Transferring|## |Bad |ERROR' || true
}

want=${1:-all}

if [ "$want" = all ] || [ "$want" = bootm ]; then
	run "bootm  (mainline defaults: ARMV7_NONSEC=y, ARMV7_VIRT=y)" \
		"mmc dev 0" \
		"mmc read 0x42000000 $PROBE_BLK_HEX $BLKS" \
		'bootm 0x42000000 - ${fdtcontroladdr}'
fi

if [ "$want" = all ] || [ "$want" = bootm-sec ]; then
	run "bootm with bootm_boot_mode=sec" \
		"mmc dev 0" \
		"mmc read 0x42000000 $PROBE_BLK_HEX $BLKS" \
		"setenv bootm_boot_mode sec" \
		'bootm 0x42000000 - ${fdtcontroladdr}'
fi

if [ "$want" = all ] || [ "$want" = go ]; then
	run "go (no cleanup_before_linux)" \
		"mmc dev 0" \
		"mmc read 0x42000000 $PROBE_BLK_HEX $BLKS" \
		"cp.b 0x42000040 0x40200000 0x400" \
		"go 0x40200000"
fi

echo
echo "SCTLR bit 0 = MMU, bit 2 = D-cache, bit 12 = I-cache."
echo "CPSR bits [4:0]: 0x13 = SVC, 0x1a = HYP.  Bits 6,7 = FIQ,IRQ masked."
