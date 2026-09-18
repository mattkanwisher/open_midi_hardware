#!/usr/bin/env python3
"""
ddrpara.py - read, write and compare Allwinner sun8iw20 (D1/D1s/R528/T113) DRAM
parameter blocks.

Why this exists
---------------
On this SoC family the DRAM controller is configured from a 24-word structure
that Allwinner calls `dram_para_t`.  It lives at **offset 0x38 of every boot0
image** (vendor boot0, awboot, U-Boot SPL, and the DDR-init payload that xfel
uploads).  That single fact is the key to the whole external-DDR3 question:

  * you can *read* a known-good parameter set out of any vendor boot0 blob, and
  * you can *write* your own set into a FEL DDR payload without rebuilding
    anything, because the structure sits at a fixed offset in a fixed-size
    image.

xfel ships DDR payloads for `r528-s3`, `t113-s3` and `t113-s4` only.  There is
no `t113-i` preset, because the T113-i takes *external* DDR3 and its parameters
are a board property.  This tool lets us make one.

See ../BRINGUP.md section 2 for where each preset's numbers come from.

Usage
-----
  # what parameters does this blob use?
  ddrpara.py decode vendor-boot0.bin
  ddrpara.py decode --preset t113i-100ask

  # pull the DDR-init payload out of an xfel checkout
  ddrpara.py extract vendor/xfel/chips/r528_t113.c -o payloads/t113-ddr.bin

  # stamp our parameters into it
  ddrpara.py patch payloads/t113-ddr.bin --preset t113i-100ask \
             -o payloads/t113i-ddr.bin

  # what would this look like in a U-Boot defconfig?
  ddrpara.py kconfig --preset t113i-100ask

  # what changed between two boards?
  ddrpara.py diff --preset t113-s3 --preset t113i-100ask
"""

import argparse
import os
import re
import struct
import sys

# Offset of dram_para_t inside an eGON.BT0 image.  Verified against U-Boot
# (drivers/ram/sunxi/dram_sun20i_d1.h, "offset 0x38 in any boot0 binary") and
# against xfel, which does fel_write(ctx, 0x28038, &ddr3, sizeof(ddr3)) after
# uploading its payload to 0x28000.
PARA_OFFSET = 0x38

# Field order is the ALLWINNER BOOT0 layout, 24 u32 at offset 0x38.
#
# CORRECTED 2026-09-18.  An earlier version of this comment said the order was
# "dram_para_t as defined identically in u-boot drivers/ram/sunxi/dram_sun20i_d1.h".
# It is not.  Mainline U-Boot SPLITS the boot0 structure into two:
#
#   dram_para_t    (dram_sun20i_d1.h:42-67)   21 words, const, from Kconfig:
#                  dram_clk, dram_type, dram_zq, dram_odt_en, mr0..mr3,
#                  tpr0..tpr12   -- NOTE: NO para1, NO para2, NO tpr13
#   dram_config_t  (dram_sun20i_d1.h:69-75)    3 words, MUTABLE, built at
#                  runtime in init_DRAM(): dram_para1 (hardcoded 0x10d2),
#                  dram_para2 (hardcoded 0), dram_tpr13 (= CONFIG_DRAM_SUNXI_TPR13)
#
# So the 24-word flat order below is NOT U-Boot's struct; it is the on-disk
# boot0 order, and U-Boot's header only says it is "kept compatible".  The
# order below is verified against a source that really does lay it out flat:
#   xfel  chips/r528_t113.c:31-57  (struct ddr3_param_t, 24 u32 + reserve[8])
# and cross-checked against SyterKit's 24-value `allwinner,dram-parameters`
# device-tree property, and against the parameters embedded at 0x38 in xfel's
# own DDR payload (see `selftest`).
FIELDS = [
    "dram_clk",     # MHz
    "dram_type",    # 2=DDR2 3=DDR3 6=LPDDR2 7=LPDDR3
    "dram_zq",      # ZQ calibration value
    "dram_odt_en",  # on-die termination enable
    "dram_para1",   # row/bank/page geometry seed (auto-scan overwrites)
    "dram_para2",   # rank/DQ-width seed (auto-scan overwrites)
    "dram_mr0",     # recomputed from dram_clk for DDR3; seed only
    "dram_mr1",     # USED as-is for DDR3: drive strength + Rtt_Nom
    "dram_mr2",     # recomputed from dram_clk for DDR3; seed only
    "dram_mr3",
    "dram_tpr0",    # DRAMTMG0 - recomputed for DDR3
    "dram_tpr1",    # DRAMTMG1 - recomputed for DDR3
    "dram_tpr2",    # DRAMTMG2 - recomputed for DDR3
    "dram_tpr3",    # unused
    "dram_tpr4",
    "dram_tpr5",    # per-lane write leveling / DX drive
    "dram_tpr6",
    "dram_tpr7",    # unused
    "dram_tpr8",
    "dram_tpr9",
    "dram_tpr10",   # AC/DQ delay trim (coarse)
    "dram_tpr11",   # PER-BIT-LANE READ/WRITE DELAY TRIM - board specific
    "dram_tpr12",   # PER-BIT-LANE READ/WRITE DELAY TRIM - board specific
    "dram_tpr13",   # feature bitfield - see TPR13_BITS below
]
NWORDS = len(FIELDS)  # 24
PARA_SIZE = NWORDS * 4  # 96 bytes

# Bits of dram_tpr13, as the mainline U-Boot driver ACTUALLY uses them.
#
# CORRECTED 2026-09-18 by grepping every occurrence of `dram_tpr13` in
# drivers/ram/sunxi/dram_sun20i_d1.c at commit 211de43d0f95 and reading each
# one.  The previous table claimed bits 20, 21 and 22 were "write / read /
# read-gate training".  MAINLINE NEVER READS BITS 21 OR 22.  Bit 20 is only
# reached as the top bit of the 5-bit field at [20:16].  Bits 5, 8, 9, 17 and
# 29 were missing entirely, and bit 8 is set in EVERY published T113 parameter
# set, so the old table decoded none of them.
#
# Line numbers are file:line in dram_sun20i_d1.c at 211de43d0f95.
TPR13_BITS = {
    0:  "skip auto_scan_dram_config(): take geometry from para1/para2 as given (:1242, :1291)",
    1:  "no direct test; the driver SETS it when bit 15 is clear (:1249)",
    2:  "dqs_gating_mode, low bit of the field at [3:2] (:311, :728, :1211)",
    3:  "dqs_gating_mode, high bit of the field at [3:2] (:311, :728)",
    5:  "DDR2/DDR3: force 1T command timing, MCTL 0x3102000 bit 19 (:585)",
    6:  "take the PLL_DDR frequency from dram_tpr9 instead of dram_clk (:494)",
    8:  "after PHY init, OR 0x300 from 0x3103140 into 0x31030b8 (:1348).  SET IN "
        "EVERY PUBLISHED T113 SET, including the T113-S3's 0x34000100.",
    9:  "set 0x3103100[15:12] = 5 instead of clearing it (:1338)",
    13: "no direct test; the driver SETS it when bit 15 is clear (:1249)",
    14: "rank-size bookkeeping for auto_scan_dram_size (:1236); also SET at :1249",
    15: "do not auto-set bits 0, 1, 13, 14 (:1248)",
    16: "internal ZQ only: skip the external ZQ resistor path (:1013, :1272, "
        "1279, :1351)",
    17: "skip mctl_vrefzq_init() entirely (:1007)",
    18: "force AC remapping table 7; tested as `tpr13 & 0xc0000`, so bit 18 OR "
        "bit 19 (:682)",
    19: "same test as bit 18 - `tpr13 & 0xc0000` (:682)",
    26: "NOT READ ANYWHERE IN MAINLINE.  Set in every published T113 set.",
    28: "run dramc_simple_wr_test() after init and fail if it fails (:1361)",
    29: "print the DX0/DX1 gate-training state on failure (:959) - debug only",
    30: "extra master config from dram_tpr8 (:1324)",
}

# Bits [20:16] are also read as a 5-bit FIELD, not as flags: when
# dqs_gating_mode == 2, the driver writes (((tpr13 >> 16) & 0x1f) - 2) into
# 0x31030bc (:769).  Both published T113 sets have dqs_gating_mode == 0
# (bits 3:2 clear), so that path is not taken and bit 16 acts purely as the
# internal-ZQ flag above.
TPR13_FIELDS = [
    (16, 20, "read-gate delay when dqs_gating_mode == 2 (:769); unused when "
             "bits [3:2] are 0, which they are on every published T113 set"),
]

# The AC remapping tables, transcribed from dram_sun20i_d1.c:632-653.
# 22 entries each.  See `acremap` for what they mean and how one is chosen.
AC_REMAPPING_TABLES = [
    [0] * 22,
    [1, 9, 3, 7, 8, 18, 4, 13, 5, 6, 10, 2, 14, 12, 0, 0, 21, 17, 20, 19, 11, 22],
    [4, 9, 3, 7, 8, 18, 1, 13, 2, 6, 10, 5, 14, 12, 0, 0, 21, 17, 20, 19, 11, 22],
    [1, 7, 8, 12, 10, 18, 4, 13, 5, 6, 3, 2, 9, 0, 0, 0, 21, 17, 20, 19, 11, 22],
    [4, 12, 10, 7, 8, 18, 1, 13, 2, 6, 3, 5, 9, 0, 0, 0, 21, 17, 20, 19, 11, 22],
    [13, 2, 7, 9, 12, 19, 5, 1, 6, 3, 4, 8, 10, 0, 0, 0, 21, 22, 18, 17, 11, 20],
    [3, 10, 7, 13, 9, 11, 1, 2, 4, 6, 8, 5, 12, 0, 0, 0, 20, 1, 0, 21, 22, 17],
    [3, 2, 4, 7, 9, 1, 17, 12, 18, 14, 13, 8, 15, 6, 10, 5, 19, 22, 16, 21, 20, 11],
]

# SUNXI_SID_BASE for SUNXI_GEN_NCAT2 parts (dram_sun20i_d1.c:26, and
# arch/arm/include/asm/arch-sunxi/cpu_sunxi_ncat2.h:21).  The AC remapping
# efuse is bits [11:8] of the word at +0x28.
SID_BASE = 0x03006200
SID_AC_REMAP_OFFSET = 0x28


def ac_remap_select(tpr13, fuse, dram_type=3, chipid=None):
    """Reproduce mctl_phy_ac_remapping() (dram_sun20i_d1.c:655-717) exactly.

    Returns (table_index_or_None, note).  None means the driver returns
    WITHOUT WRITING the remap registers at all, which is a different state
    from table 0.
    """
    if dram_type not in (2, 3):
        return None, "LPDDR2/LPDDR3: function returns early, registers untouched (:669-672)"
    if chipid == 0x7200:
        return None, ("SoC chip ID 0x7200 (SUNXI_CHIPID_T113M4020DC0, the T113-S4): "
                      "function returns early, registers untouched (:677-678)")
    if dram_type == 2:
        if fuse == 15:
            return None, "DDR2 with efuse 15: returns early, registers untouched (:681-682)"
        return 6, "DDR2 always uses table 6 (:684)"
    if tpr13 & 0xC0000:
        return 7, "dram_tpr13 & 0xc0000 (bit 18 or bit 19) forces table 7 (:686-687)"
    sel = {8: 2, 9: 3, 10: 5, 11: 4, 12: 1, 13: 0, 14: 0}
    return (sel.get(fuse, 1),
            "efuse SID+0x28[11:8] = %d selects table %d (:689-697; anything not "
            "listed falls through to `default: case 12` = table 1)"
            % (fuse, sel.get(fuse, 1)))


def ac_remap_registers(cfg):
    """The four 32-bit words the driver writes, from dram_sun20i_d1.c:699-717."""
    v0 = ((cfg[4] << 25) | (cfg[3] << 20) | (cfg[2] << 15) |
          (cfg[1] << 10) | (cfg[0] << 5))
    v1 = ((cfg[10] << 25) | (cfg[9] << 20) | (cfg[8] << 15) |
          (cfg[7] << 10) | (cfg[6] << 5) | cfg[5])
    v2 = ((cfg[15] << 20) | (cfg[14] << 15) | (cfg[13] << 10) |
          (cfg[12] << 5) | cfg[11])
    v3 = ((cfg[21] << 25) | (cfg[20] << 20) | (cfg[19] << 15) |
          (cfg[18] << 10) | (cfg[17] << 5) | cfg[16])
    return [(0x3102500, v0), (0x3102504, v1), (0x3102508, v2),
            (0x310250C, v3), (0x3102500, v0 | 1)]


# ---------------------------------------------------------------------------
# Presets.  Every one of these is copied verbatim from a primary source; the
# provenance comment is the whole point, do not edit a number without moving
# its citation with it.
# ---------------------------------------------------------------------------

PRESETS = {
    # Allwinner T113-S3: 128 MB DDR3 CO-PACKAGED inside the SoC.
    # Source: xfel chips/r528_t113.c, `xfel ddr t113-s3` branch.  Identical to
    # awboot arch/arm32/mach-t113s3/dram.c lines 35-41 and to U-Boot
    # configs/mangopi_mq_r_defconfig.  THIS IS NOT OUR PART.
    "t113-s3": dict(
        dram_clk=792, dram_type=3, dram_zq=0x007B7BFB, dram_odt_en=0x00,
        dram_para1=0x000010D2, dram_para2=0x00000000,
        dram_mr0=0x00001C70, dram_mr1=0x00000042,
        dram_mr2=0x00000018, dram_mr3=0x00000000,
        dram_tpr0=0x004A2195, dram_tpr1=0x02423190, dram_tpr2=0x0008B061,
        dram_tpr3=0xB4787896, dram_tpr4=0x00000000, dram_tpr5=0x48484848,
        dram_tpr6=0x00000048, dram_tpr7=0x1620121E, dram_tpr8=0x00000000,
        dram_tpr9=0x00000000, dram_tpr10=0x00000000,
        dram_tpr11=0x00340000, dram_tpr12=0x00000046, dram_tpr13=0x34000100,
    ),

    # (t113-s4 is defined below: identical to t113-s3 but clocked at 936 MHz.
    #  Source: xfel chips/r528_t113.c, `xfel ddr t113-s4` branch.)

    # === THE ONES THAT MATTER ===

    # Allwinner T113-i, EXTERNAL 512 MB DDR3 (one x16 part), on the
    # 100ask "T113i-Industrial DevKit".
    # Source: SyterKit archive/boards/100ask-t113i/board.dts, the
    # `allwinner,dram-parameters` property, decoded through the field order in
    # SyterKit include/drivers/dram/dram.h.
    # Deltas vs t113-s3: odt_en 0->1, tpr11, tpr12, tpr13.
    "t113i-100ask": dict(
        dram_clk=792, dram_type=3, dram_zq=0x007B7BFB, dram_odt_en=0x01,
        dram_para1=0x000010D2, dram_para2=0x00000000,
        dram_mr0=0x00001C70, dram_mr1=0x00000042,
        dram_mr2=0x00000018, dram_mr3=0x00000000,
        dram_tpr0=0x004A2195, dram_tpr1=0x02423190, dram_tpr2=0x0008B061,
        dram_tpr3=0xB4787896, dram_tpr4=0x00000000, dram_tpr5=0x48484848,
        dram_tpr6=0x00000048, dram_tpr7=0x1620121E, dram_tpr8=0x00000000,
        dram_tpr9=0x00000000, dram_tpr10=0x00000000,
        dram_tpr11=0x00770000, dram_tpr12=0x00000002, dram_tpr13=0x34050100,
    ),

    # Allwinner T113-i, EXTERNAL DDR3, on the Tronlong "T113i MiniEVM".
    # Source: nickfox-taterli/t113-uboot configs/t113i_minievm_defconfig
    # (U-Boot 2025.10 base).  Note this board runs the *T113-S3* numbers
    # unchanged on external DDR3 and is reported working.  Two different
    # T113-i boards therefore use two different parameter sets - which is the
    # evidence that these are board-layout trim, not part constants.
    "t113i-tronlong": dict(
        dram_clk=792, dram_type=3, dram_zq=0x007B7BFB, dram_odt_en=0x00,
        dram_para1=0x000010D2, dram_para2=0x00000000,
        dram_mr0=0x00001C70, dram_mr1=0x00000042,
        dram_mr2=0x00000018, dram_mr3=0x00000000,
        dram_tpr0=0x004A2195, dram_tpr1=0x02423190, dram_tpr2=0x0008B061,
        dram_tpr3=0xB4787896, dram_tpr4=0x00000000, dram_tpr5=0x48484848,
        dram_tpr6=0x00000048, dram_tpr7=0x1620121E, dram_tpr8=0x00000000,
        dram_tpr9=0x00000000, dram_tpr10=0x00000000,
        dram_tpr11=0x00340000, dram_tpr12=0x00000046, dram_tpr13=0x34000100,
    ),
}

# t113-s4 = t113-s3 at 936 MHz (xfel chips/r528_t113.c).
PRESETS["t113-s4"] = dict(PRESETS["t113-s3"], dram_clk=936)

# Our own starting point for the mt32-t113 board.  Deliberately identical to
# t113i-100ask: it is the only published external-DDR3 T113-i set whose board
# we can also read the schematic story of, and 512 MB x16 DDR3 is the same
# shape as what we plan to fit.  Expect to re-tune tpr11/tpr12 on our PCB.
PRESETS["mt32-t113"] = dict(PRESETS["t113i-100ask"])


def preset(name):
    if name not in PRESETS:
        sys.exit("unknown preset %r; have: %s" % (name, ", ".join(sorted(PRESETS))))
    return [PRESETS[name][f] for f in FIELDS]


def words_from_blob(data, offset=PARA_OFFSET):
    if len(data) < offset + PARA_SIZE:
        sys.exit("blob is only %d bytes, need at least %d" % (len(data), offset + PARA_SIZE))
    return list(struct.unpack_from("<%dI" % NWORDS, data, offset))


def blob_is_egon(data):
    return len(data) >= 12 and data[4:12] == b"eGON.BT0"


def describe_tpr13(v):
    out = []
    for bit in range(32):
        if v & (1 << bit):
            out.append("  bit %-2d  %s" % (bit, TPR13_BITS.get(
                bit, "NOT READ ANYWHERE IN MAINLINE dram_sun20i_d1.c")))
    for lo, hi, what in TPR13_FIELDS:
        out.append("  [%d:%d] = %d  %s" % (hi, lo, (v >> lo) & ((1 << (hi - lo + 1)) - 1), what))
    out.append("  dqs_gating_mode = (tpr13 >> 2) & 3 = %d" % ((v >> 2) & 3))
    return out


def cmd_acremap(args):
    """Which AC remapping table does a given (tpr13, efuse) pair select?"""
    tpr13 = args.tpr13
    if tpr13 is None:
        tpr13 = PRESETS[args.preset or "mt32-t113"]["dram_tpr13"]
    print("dram_tpr13 = 0x%08x   dram_type = %d   SID+0x28[11:8] = %s"
          % (tpr13, args.type,
             "%d" % args.fuse if args.fuse is not None else "(unknown)"))
    print("read it on hardware with:  xfel read32 0x%08x   # then (v >> 8) & 0xf"
          % (SID_BASE + SID_AC_REMAP_OFFSET))
    print()
    fuses = [args.fuse] if args.fuse is not None else list(range(16))
    seen = {}
    for f in fuses:
        idx, why = ac_remap_select(tpr13, f, args.type, args.chipid)
        seen.setdefault((idx, why), []).append(f)
    for (idx, why), fs in seen.items():
        tag = ("efuse %s" % (fs[0] if len(fs) == 1 else
               ",".join(str(x) for x in fs)))
        if idx is None:
            print("%-28s -> NO REMAPPING WRITTEN AT ALL" % tag)
            print("    %s" % why)
            print("    NOTE: this is NOT the same state as table 0.  The driver")
            print("          expresses 'do not remap' by returning without writing,")
            print("          and table 0 by writing zeros plus the bit-0 enable.")
        else:
            cfg = AC_REMAPPING_TABLES[idx]
            straight = all(v == 0 or v == i + 1 for i, v in enumerate(cfg))
            print("%-28s -> table %d%s" % (tag, idx,
                  "   (STRAIGHT THROUGH)" if straight else ""))
            print("    %s" % why)
            print("    cfg = %s" % cfg)
            for addr, val in ac_remap_registers(cfg):
                print("      writel(0x%08x, 0x%07x)" % (val, addr))
        print()
    return 0


def _zero_means_identity_evidence():
    """The argument that a 0 entry means 'identity at this position'.

    For each table, the set of positions holding 0 is compared with the set of
    values from 1..22 that the table's non-zero entries do not use.  If a 0 at
    index i meant 'signal 0' the two sets would be unrelated; if it means
    'signal i+1, unchanged' they must be equal.  Returns a list of
    (index, positions, missing, ok) tuples.
    """
    out = []
    for i, cfg in enumerate(AC_REMAPPING_TABLES):
        zeros = [j + 1 for j, v in enumerate(cfg) if v == 0]
        missing = sorted(set(range(1, 23)) - set(v for v in cfg if v))
        out.append((i, zeros, missing, zeros == missing))
    return out


def cmd_decode(args):
    if args.preset:
        words = preset(args.preset)
        src = "preset %s" % args.preset
    else:
        data = open(args.blob, "rb").read()
        words = words_from_blob(data, args.offset)
        src = "%s @ 0x%02x" % (args.blob, args.offset)
        if blob_is_egon(data):
            length = struct.unpack_from("<I", data, 16)[0]
            print("eGON.BT0 image, header-declared length %d bytes (file is %d)"
                  % (length, len(data)))
        else:
            print("warning: no eGON.BT0 magic at offset 4 - is this a boot0 image?")
    print("dram_para_t from %s\n" % src)
    for name, v in zip(FIELDS, words):
        if name == "dram_clk":
            print("  %-12s 0x%08x  (%d MHz)" % (name, v, v))
        elif name == "dram_type":
            t = {2: "DDR2", 3: "DDR3", 6: "LPDDR2", 7: "LPDDR3"}.get(v, "?")
            print("  %-12s 0x%08x  (%s)" % (name, v, t))
        else:
            print("  %-12s 0x%08x" % (name, v))
    print("\ndram_tpr13 decoded:")
    for line in describe_tpr13(words[FIELDS.index("dram_tpr13")]) or ["  (no known bits set)"]:
        print(line)
    return 0


def cmd_kconfig(args):
    words = preset(args.preset) if args.preset else words_from_blob(
        open(args.blob, "rb").read(), args.offset)
    d = dict(zip(FIELDS, words))
    print("# mainline U-Boot knobs for this parameter set.")
    print("# Everything not listed here is HARDCODED in")
    print("# drivers/ram/sunxi/dram_sun20i_d1.c and cannot be set from a defconfig.")
    print("CONFIG_MACH_SUN8I_R528=y")
    print("CONFIG_DRAM_CLK=%d" % d["dram_clk"])
    print("CONFIG_SUNXI_DRAM_TYPE_DDR3=y" if d["dram_type"] == 3
          else "# dram_type=%d - pick the matching SUNXI_DRAM_TYPE_* symbol" % d["dram_type"])
    print("CONFIG_DRAM_ZQ=%d" % d["dram_zq"])
    print("CONFIG_DRAM_SUNXI_ODT_EN=0x%x" % d["dram_odt_en"])
    print("# TPR0 is never read by dram_sun20i_d1.c, but drivers/ram/sunxi/Kconfig")
    print("# gives it NO default, so leaving it out makes `make syncconfig` stop")
    print("# and prompt and the build hang.  All five DRAM_SUNXI_* hex symbols")
    print("# are like this.  Verified 2026-09-18 by omitting it and watching a")
    print("# build wedge.  See ../BRINGUP.md section 2.9.")
    print("CONFIG_DRAM_SUNXI_TPR0=0x%08x" % d["dram_tpr0"])
    print("CONFIG_DRAM_SUNXI_TPR11=0x%x" % d["dram_tpr11"])
    print("CONFIG_DRAM_SUNXI_TPR12=0x%x" % d["dram_tpr12"])
    print("CONFIG_DRAM_SUNXI_TPR13=0x%x" % d["dram_tpr13"])
    if d["dram_mr1"] != 0x42:
        print("# WARNING: this set wants dram_mr1=0x%x but mainline hardcodes 0x42."
              % d["dram_mr1"])
        print("#          MR1 is DDR3 output drive + Rtt_Nom - patch the driver.")
    return 0


def cmd_diff(args):
    if len(args.preset) != 2:
        sys.exit("diff needs exactly two --preset arguments")
    a, b = (preset(p) for p in args.preset)
    print("%-12s %-12s %-12s" % ("field", args.preset[0], args.preset[1]))
    n = 0
    for name, x, y in zip(FIELDS, a, b):
        if x != y:
            print("%-12s 0x%08x   0x%08x" % (name, x, y))
            n += 1
    print("\n%d field(s) differ" % n)
    return 0


def cmd_extract(args):
    """Pull xfel's DDR-init payload out of its C source as a flat binary."""
    src = open(args.source, "r", errors="replace").read()
    m = re.search(r"static const uint8_t %s\[\]\s*=\s*\{(.*?)\n\t\};"
                  % re.escape(args.array), src, re.S)
    if not m:
        m = re.search(r"static const uint8_t %s\[\]\s*=\s*\{(.*?)\};"
                      % re.escape(args.array), src, re.S)
    if not m:
        sys.exit("array %s not found in %s" % (args.array, args.source))
    data = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))
    if not blob_is_egon(data):
        sys.exit("extracted %d bytes but they are not an eGON.BT0 image" % len(data))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    open(args.out, "wb").write(data)
    print("wrote %s (%d bytes, eGON.BT0)" % (args.out, len(data)))
    print("its built-in parameters:")
    for name, v in zip(FIELDS, words_from_blob(data)):
        print("  %-12s 0x%08x" % (name, v))
    return 0


def cmd_patch(args):
    data = bytearray(open(args.blob, "rb").read())
    # REFUSE to patch a U-Boot SPL.  Added 2026-09-18 after building one and
    # looking at it.  U-Boot's eGON header is 0x60 bytes (struct boot_file_head,
    # include/sunxi_image.h), and offsets 0x2c..0x5f are `string_pool[13]`,
    # which mkimage fills with the CONFIG_DEFAULT_DEVICE_TREE name.  Writing 96
    # bytes at 0x38 would smash that string - and achieve nothing, because a
    # U-Boot SPL takes its DRAM parameters from Kconfig at COMPILE time
    # (dram_sun20i_d1.c:1370-1392), not from its own header.  Only an Allwinner
    # boot0 - and xfel's DDR payload, which is one - reads them from 0x38.
    if len(data) >= 0x18 and data[0x14:0x17] == b"SPL":
        sys.exit("refusing to patch %s: its header carries U-Boot's \"SPL\" "
                 "signature at 0x14, so offset 0x38 is string_pool, not\n"
                 "dram_para_t.  A U-Boot SPL gets its DRAM parameters from "
                 "Kconfig - edit the defconfig and rebuild instead." % args.blob)
    words = preset(args.preset)
    before = words_from_blob(bytes(data), args.offset)
    struct.pack_into("<%dI" % NWORDS, data, args.offset, *words)
    open(args.out, "wb").write(bytes(data))
    print("wrote %s (%d bytes), dram_para_t at 0x%02x replaced with preset %s"
          % (args.out, len(data), args.offset, args.preset))
    for name, old, new in zip(FIELDS, before, words):
        if old != new:
            print("  %-12s 0x%08x -> 0x%08x" % (name, old, new))
    if blob_is_egon(bytes(data)):
        print("note: the eGON checksum is NOT recomputed. The BROM checks it when "
              "booting from SD/SPI; FEL (xfel write + xfel exec) does not.")
    return 0


# ---------------------------------------------------------------------------
# selftest: everything about this file that can be checked without a T113.
# ---------------------------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
BOOT = os.path.dirname(HERE)
VENDOR = os.path.join(BOOT, "vendor")


def _check(results, name, ok, detail=""):
    results.append((name, ok, detail))
    print("%-4s %s%s" % ("PASS" if ok else "FAIL", name,
                         ("  -- " + detail) if detail else ""))
    return ok


def cmd_selftest(args):
    r = []

    # 1. Structural invariants of this file.
    _check(r, "dram_para_t is 24 words / 96 bytes",
           NWORDS == 24 and PARA_SIZE == 96)
    _check(r, "every preset defines every field",
           all(set(v) == set(FIELDS) for v in PRESETS.values()),
           "presets: " + ", ".join(sorted(PRESETS)))

    # 2. Round trip: pack a preset into a blob and read it back.
    blob = bytearray(b"\x16\x00\x00\xea" + b"eGON.BT0" + b"\x00" * (0x38 + PARA_SIZE))
    struct.pack_into("<%dI" % NWORDS, blob, PARA_OFFSET, *preset("mt32-t113"))
    _check(r, "pack/unpack round trip at 0x38",
           words_from_blob(bytes(blob)) == preset("mt32-t113"))
    _check(r, "eGON magic detector", blob_is_egon(bytes(blob)))

    # 3. The four-field claim: exactly four fields separate the co-packaged
    #    T113-S3 from the 100ask external-DDR3 T113-i.
    diff = [f for f, a, b in zip(FIELDS, preset("t113-s3"), preset("t113i-100ask"))
            if a != b]
    _check(r, "t113-s3 vs t113i-100ask differ in exactly 4 fields",
           diff == ["dram_odt_en", "dram_tpr11", "dram_tpr12", "dram_tpr13"],
           "differ: " + ", ".join(diff))
    _check(r, "all four are mainline Kconfig symbols",
           set(diff) <= {"dram_odt_en", "dram_tpr11", "dram_tpr12",
                         "dram_tpr13", "dram_clk", "dram_zq", "dram_type"})

    # 4. The Tronlong T113-i really is the T113-S3 set unchanged.
    _check(r, "t113i-tronlong == t113-s3 (external DDR3, S3 numbers)",
           preset("t113i-tronlong") == preset("t113-s3"))

    # 5. AC remapping tables.
    _check(r, "8 AC remapping tables of 22 entries",
           len(AC_REMAPPING_TABLES) == 8 and
           all(len(t) == 22 for t in AC_REMAPPING_TABLES))
    ev = _zero_means_identity_evidence()
    good = [i for i, z, m, ok in ev if ok]
    bad = [i for i, z, m, ok in ev if not ok]
    _check(r, "a 0 entry means 'identity here': holds for tables " + str(good),
           set(bad) <= {6},
           "table 6 (the DDR2-only table) is the sole exception: it repeats "
           "the value 1 and drops 18, which looks like a defect in the "
           "decompiled original. Tables that fail: %s" % bad)
    _check(r, "table 0 is therefore straight-through",
           all(v == 0 for v in AC_REMAPPING_TABLES[0]) and ev[0][3])
    _check(r, "table 7 is a full non-identity permutation of 1..22",
           sorted(AC_REMAPPING_TABLES[7]) == list(range(1, 23)) and
           AC_REMAPPING_TABLES[7] != list(range(1, 23)))
    _check(r, "tpr13 bit 18 and bit 19 both force table 7 (mask 0xc0000)",
           ac_remap_select(1 << 18, 12)[0] == 7 and
           ac_remap_select(1 << 19, 12)[0] == 7 and
           ac_remap_select(0, 12)[0] == 1)
    _check(r, "no tpr13 bit can select table 0; only the efuse can",
           all(ac_remap_select(1 << b, 8)[0] != 0 for b in range(32)) and
           ac_remap_select(0, 13)[0] == 0 and ac_remap_select(0, 14)[0] == 0)
    _check(r, "our own preset forces table 7",
           ac_remap_select(PRESETS["mt32-t113"]["dram_tpr13"], 12)[0] == 7)

    # 6. Cross-check the presets against the vendor trees, if they are here.
    xfel_src = os.path.join(VENDOR, "xfel", "chips", "r528_t113.c")
    if os.path.exists(xfel_src):
        txt = open(xfel_src, errors="replace").read()
        m = re.search(r"static const uint8_t t113_ddr_payload\[\]\s*=\s*\{(.*?)\n\t\};",
                      txt, re.S)
        if m:
            data = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))
            _check(r, "xfel's own DDR payload is an eGON.BT0 image", blob_is_egon(data),
                   "%d bytes" % len(data))
            _check(r, "its parameters at 0x38 decode to the t113-s3 preset",
                   words_from_blob(data) == preset("t113-s3"))
    else:
        print("SKIP xfel cross-check (no %s)" % xfel_src)

    syk = os.path.join(VENDOR, "syterkit", "archive", "boards")
    for board, name in (("100ask-t113i", "t113i-100ask"),
                        ("100ask-t113s3", "t113-s3")):
        dts = os.path.join(syk, board, "board.dts")
        if not os.path.exists(dts):
            print("SKIP SyterKit cross-check for %s (no %s)" % (name, dts))
            continue
        txt = open(dts, errors="replace").read()
        m = re.search(r"allwinner,dram-parameters\s*=(.*?);", txt, re.S)
        vals = []
        for tok in re.findall(r"<([^>]+)>", m.group(1)):
            for t in tok.split(","):
                t = t.strip()
                if t == "SUNXI_DRAM_DDR3":
                    vals.append(3)
                elif t:
                    vals.append(int(t, 0))
        _check(r, "SyterKit %s board.dts matches preset %s" % (board, name),
               vals[:NWORDS] == preset(name),
               "read %d values from the DTS" % len(vals))

    # 7. kconfig output emits only symbols that exist in the driver's Kconfig.
    kc = os.path.join(VENDOR, "u-boot", "drivers", "ram", "sunxi", "Kconfig")
    if os.path.exists(kc):
        txt = open(kc).read()
        want = ["DRAM_SUNXI_ODT_EN", "DRAM_SUNXI_TPR0", "DRAM_SUNXI_TPR11",
                "DRAM_SUNXI_TPR12", "DRAM_SUNXI_TPR13", "SUNXI_DRAM_TYPE_DDR3"]
        _check(r, "every symbol kconfig emits exists in u-boot's ram/sunxi/Kconfig",
               all(("config " + w) in txt for w in want))
        nodefault = [w for w in want
                     if re.search(r"config %s\n\thex[^\n]*\n(?!\tdefault)" % w, txt)]
        _check(r, "the hex DRAM_SUNXI_* symbols have NO Kconfig default, so a "
                  "defconfig must set all five", len(nodefault) == 5,
               "no default: " + ", ".join(nodefault))
    else:
        print("SKIP Kconfig cross-check (no %s)" % kc)

    failed = [n for n, ok, _ in r if not ok]
    print()
    print("%d checks, %d failed" % (len(r), len(failed)))
    for n in failed:
        print("  FAILED: %s" % n)
    return 1 if failed else 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("decode", help="print a parameter set from a blob or preset")
    d.add_argument("blob", nargs="?")
    d.add_argument("--preset")
    d.add_argument("--offset", type=lambda s: int(s, 0), default=PARA_OFFSET)
    d.set_defaults(fn=cmd_decode)

    k = sub.add_parser("kconfig", help="emit the U-Boot defconfig lines")
    k.add_argument("blob", nargs="?")
    k.add_argument("--preset")
    k.add_argument("--offset", type=lambda s: int(s, 0), default=PARA_OFFSET)
    k.set_defaults(fn=cmd_kconfig)

    f = sub.add_parser("diff", help="compare two presets")
    f.add_argument("--preset", action="append", default=[])
    f.set_defaults(fn=cmd_diff)

    e = sub.add_parser("extract", help="pull a DDR payload out of xfel's C source")
    e.add_argument("source", help="path to xfel chips/r528_t113.c")
    e.add_argument("--array", default="t113_ddr_payload")
    e.add_argument("-o", "--out", required=True)
    e.set_defaults(fn=cmd_extract)

    ar = sub.add_parser("acremap",
                        help="which AC remapping table a tpr13/efuse pair selects")
    ar.add_argument("--tpr13", type=lambda s: int(s, 0),
                    help="dram_tpr13 value (default: the --preset's)")
    ar.add_argument("--preset")
    ar.add_argument("--fuse", type=lambda s: int(s, 0),
                    help="SID+0x28[11:8]; omit to tabulate all 16 values")
    ar.add_argument("--type", type=int, default=3, help="dram_type (default 3 = DDR3)")
    ar.add_argument("--chipid", type=lambda s: int(s, 0),
                    help="SoC chip ID; 0x7200 is the T113-S4, which skips remapping")
    ar.set_defaults(fn=cmd_acremap)

    st = sub.add_parser("selftest",
                        help="check everything checkable without a T113")
    st.set_defaults(fn=cmd_selftest)

    a = sub.add_parser("patch", help="stamp a preset into a boot0/DDR-payload blob")
    a.add_argument("blob")
    a.add_argument("--preset", required=True)
    a.add_argument("--offset", type=lambda s: int(s, 0), default=PARA_OFFSET)
    a.add_argument("-o", "--out", required=True)
    a.set_defaults(fn=cmd_patch)

    args = p.parse_args()
    if args.cmd in ("decode", "kconfig") and not args.blob and not args.preset:
        sys.exit("give a blob path or --preset")
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
