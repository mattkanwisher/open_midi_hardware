# Bring-up: booting an Allwinner T113-i without Linux

Workstream C. Status: **2026-09-18, second pass — the reading has been
compiled.** Still nothing tested on T113 silicon, and no T113 board has been in
front of this work. But mainline U-Boot is now cloned, built for this board
from this repository's own defconfig, and the handover it performs has been
*measured* rather than inferred — on an Allwinner H3 under QEMU, which runs the
same ARMv7 handover code.

The question this document answers: what actually brings a T113-i up from
reset, where the DRAM parameters for an *external* DDR3 part come from, and how
you hand control to a bare-metal or RTOS payload instead of a kernel.

## What the second pass changed

Five claims made on 2026-09-17 turned out to be wrong, and one of them would
have cost a day of confused debugging on the first board.

| Was | Actually | Where |
|---|---|---|
| "`CONFIG_DRAM_SUNXI_TPR0` is a dead Kconfig symbol. **Harmless**" | Dead, yes. Harmless, no. It has **no Kconfig default**, so a defconfig that omits it makes `make syncconfig` stop and prompt and the build never finishes. `configs/t113i_mt32_defconfig` did omit it and **did not build** | § 2.9 |
| "`bootm` hands over in **secure PL1 (SVC)**" | **Measured: HYP mode, non-secure.** `ARMV7_NONSEC` and `ARMV7_VIRT` both default to `y`, and a Cortex-A7 is virtualisation-capable, so the secure monitor ERETs into HYP. `emu/src/start.S` assumes SVC and would silently stay in HYP | § 4.4 |
| "100ask T113-i devkit: console UART0 PB8/PB9" — true, but presented as if `CONS_INDEX=1` would reach it | Mainline's **SPL** muxes the console in C, not from the DT, and for `MACH_SUN8I_R528` it knows exactly two pin pairs: **PE2/PE3** (`CONS_INDEX=1`) and **PB6/PB7** (`CONS_INDEX=4`). A board on PB8/PB9 gets a silent SPL and a talkative U-Boot | § 6.2 |
| The eGON header table listed `dram_para_t` at **0x38** as a row of the header | True of an *Allwinner boot0* and of xfel's DDR payload. **Not true of a U-Boot SPL**, whose header is 0x60 bytes and whose 0x2c–0x5f is `string_pool`, holding the devicetree name. Patching a U-Boot SPL at 0x38 corrupts it and changes nothing | § 1.2 |
| "QEMU: no" | Narrower than that. QEMU has no T113, but `-M orangepi-pc` runs the **entire sunxi boot flow** — eGON image, SPL, DRAM init, SD sector 16, SPL→u-boot.img, the U-Boot shell, `bootm` — from a U-Boot built out of this same tree. That is where § 4.4's numbers came from | § 6.4 |

Two claims were *confirmed and sharpened*: the four-Kconfig external-DDR3 delta
(§ 2.4, now cross-checked by script against three independent sources), and
"`go` leaves the MMU and caches on" (§ 4.1, now a measurement).

And the AC-remapping conclusion that gates the PCB (§ 2.5) is **unchanged but
now derived rather than asserted**: table 0 really is straight-through, and
there is a proof of it that does not depend on a datasheet.

## The part, and why the distinction matters

| | T113-S3 | T113-S4 | **T113-i** |
|---|---|---|---|
| Package | TQFP-128 | TQFP-128 | **LFBGA** |
| DRAM | 128 MB DDR3 **co-packaged** | 256 MB DDR3 co-packaged | **external, up to 2 GB** |
| Who targets it | every hobbyist board, every bootloader port | a few | industrial SoMs (Forlinx FET113i-S), 100ask T113i devkit, Tronlong MiniEVM |

Everything in the sunxi hobbyist ecosystem — awboot, xboot, xfel's `ddr`
command, the FreeRTOS port, every blog post — is written against the **S3**.
Where a piece of evidence below applies only to the S3, it says so.

The good news, established in section 2: the silicon is the same die family
(`sun8iw20`, mainline U-Boot symbol `MACH_SUN8I_R528`), the DRAM driver is the
same code, and the difference between co-packaged and external DDR3 comes down
to **four numbers in a defconfig**.

Convention used throughout:

| | |
|---|---|
| **[V]** | read from source or vendor documentation, cited by file and line |
| **[I]** | inference, and the reasoning is given |
| **[B]** | **built or run in this container on 2026-09-18**, with the command and its output quoted. New in the second pass, and the strongest class here |
| **[X]** | a claim from the first pass that the second pass **corrected** |

Nothing anywhere in this document has touched T113 silicon. **[B]** means a
compiler or an emulator agreed, not that a board did.

All source citations are against mainline U-Boot commit
`211de43d0f954a00a490220c1aac9db298287c40` (`v2026.10-rc4-34-g211de43d0f`,
2026-09-17), cloned to `vendor/u-boot`. See "Reference tree".

---

## 1. Boot ROM flow, image format, FEL

### 1.1 What the BROM does at reset [V]

The A7 comes out of reset executing the mask-ROM BROM. It walks its boot media
list looking for an image with the `eGON.BT0` signature, copies the first stage
into SRAM, verifies a checksum, and branches to it. If nothing valid is found
anywhere, it drops into **FEL** — a USB device mode on the OTG port that speaks
a proprietary read-memory / write-memory / execute protocol.

Media order, in the order the BROM tries them:

| Order | Device | Where it looks |
|---:|---|---|
| 1 | MMC0 (microSD) | sector 16 (8 KB), then sector 256 (128 KB) |
| 2 | SPI0 NOR / SPI NAND on PortC | offset 0 |
| 3 | MMC2 (eMMC) | sector 16, sector 256, or a boot partition |
| — | none valid | **FEL over USB-OTG** |

The two SD offsets are verified from U-Boot
`doc/board/allwinner/sunxi.rst`: *"All Allwinner SoCs will try to find a boot
image at sector 16 (8KB) of an SD card"*, and newer SoCs *"also look at sector
256 (128KB) for the signature"*. **[V]**

The relative priority of SPI versus MMC is stated less precisely: sunxi.rst
says *"Typically the SPI flash has the lowest boot priority, so SD card and
eMMC devices will be considered first."* **[V for SD-before-SPI, I for the
exact eMMC/SPI ordering on this specific part.]** Confirm against the T113-i
datasheet's boot-mode table before designing a board that relies on it.

U-Boot's SPL enumerates `SUNXI_BOOTED_FROM_MMC0`, `..._MMC0_HIGH` (the 128 KB
location), `..._SPI`, `..._NAND`, `..._MMC2`, `..._MMC2_HIGH`
(`arch/arm/mach-sunxi/board.c`), which is the authoritative list of what the
BROM can hand over from. **[V]**

A detail worth knowing: **the BROM writes the media it booted from back into
the image header in SRAM**, at the `boot_media` field. The SPL reads it to
decide where to look for the next stage:

```c
/* arch/arm/mach-sunxi/board.c */
if (sunxi_egon_valid(egon_head))
    return readb(&egon_head->boot_media);
...
/* Not a valid image, so we must have been booted via FEL. */
return SUNXI_INVALID_BOOT_SOURCE;
```
**[V]**

### 1.2 The eGON.BT0 header [V + X]

From U-Boot `include/sunxi_image.h` — this is the structure the BROM parses:

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0x00 | 4 | `b_instruction` | An ARM `b` (`0xeaxxxxxx`) or RISC-V `j` past the header |
| 0x04 | 8 | `magic` | `"eGON.BT0"` — not NUL-terminated |
| 0x0c | 4 | `check_sum` | Simple 32-bit word sum, seeded with `0x5f0a6c39` |
| 0x10 | 4 | `length` | Image length, **must be a multiple of 512** |
| 0x14 | 4 | `pub_head_size` / `spl_signature` | U-Boot stamps `"SPL"` + version here |
| 0x18 | 4 | `fel_script_address` | set by `sunxi-fel` |
| 0x1c | 4 | `fel_uEnv_length` | set by `sunxi-fel` |
| 0x20 | 4 | `dt_name_offset` | |
| 0x24 | 4 | `dram_size` | MiB, written by the SPL after DRAM init |
| 0x28 | 4 | `boot_media` | **written here by the boot ROM** |
| 0x2c | 52 | `string_pool[13]` | |
| **0x38** | **96** | **`dram_para_t`** | **24 u32 — see section 2** |

**[X] That last row is true of an Allwinner boot0 and FALSE of a U-Boot SPL,
and the first pass did not say so.** `struct boot_file_head`
(`include/sunxi_image.h:37-80`) is **0x60 bytes**, not 0x38: offsets
0x2c–0x5f are `string_pool[13]`, which mkimage fills with
`CONFIG_DEFAULT_DEVICE_TREE`. Here is the front of the SPL built in § 2.9,
and the devicetree name is sitting exactly where `dram_para1` would be: **[B]**

```
$ od -A x -t x1z -v spl/sunxi-spl.bin | head -6
000000 16 00 00 ea 65 47 4f 4e 2e 42 54 30 2e 9a 67 c9  >....eGON.BT0..g.<
000010 00 60 00 00 53 50 4c 02 00 00 00 00 00 00 00 00  >.`..SPL.........<
000020 2c 00 00 00 00 00 00 00 00 00 00 00 61 6c 6c 77  >,...........allw<
000030 69 6e 6e 65 72 2f 73 75 6e 38 69 2d 74 31 31 33  >inner/sun8i-t113<
000040 73 2d 6d 61 6e 67 6f 70 69 2d 6d 71 2d 72 2d 74  >s-mangopi-mq-r-t<
000050 31 31 33 00 00 00 00 00 00 00 00 00 00 00 00 00  >113.............<
000060 0f 00 00 ea 14 f0 9f e5 14 f0 9f e5 14 f0 9f e5  >................<
```

Reading it off: `b .+0x60` at 0x00; `length` at 0x10 = `0x6000` = 24576, which
is the file size; `"SPL"` + version 0x02 at 0x14 (`SPL_HEADER_VERSION =
SPL_VERSION(0,2)`); `dt_name_offset` at 0x20 = 0x2c; the ASCIIZ devicetree name
at 0x2c; and the first instruction at 0x60. A U-Boot SPL takes its DRAM
parameters from **Kconfig at compile time**
(`dram_sun20i_d1.c:1370-1392`, `static const dram_para_t para = { .dram_clk =
CONFIG_DRAM_CLK, ... }`), so there is nothing at 0x38 to patch and nothing that
would be read if you did. `scripts/ddrpara.py patch` now refuses a blob that
carries the `"SPL"` signature at 0x14. **[B]**

Where the 0x38 row *is* true: Allwinner's own boot0 puts its DRAM parameter
block immediately after a 0x38 header, and U-Boot's D1 driver header says so
explicitly — *"This is copied from Allwinner's boot0 data structure, which can
be found at offset 0x38 in any boot0 binary"*
(`drivers/ram/sunxi/dram_sun20i_d1.h`). **[V]** Independently confirmed by
xfel, which uploads its DDR payload to SRAM `0x28000` and then writes its
parameter struct to `0x28038` before executing
(`chips/r528_t113.c`). **[V]** And confirmed empirically: `scripts/ddrpara.py`
decodes xfel's payload at 0x38 and gets `dram_clk = 0x318 = 792`,
`dram_type = 3`. **[V]**

One more wrinkle, found by extracting it: xfel's `t113_ddr_payload` array is
24064 bytes but its header `length` field says **19392 (0x4bc0)**, which is
not even a multiple of 512. Its sibling `r528_ddr_payload` is self-consistent
at 24064. This does not matter on the FEL path — xfel writes the whole array
and `exec`s it, and the BROM never parses the header — but it means **that
blob cannot be `dd`ed to an SD card and booted.** **[B]**

Build one with U-Boot's mkimage: `mkimage -T sunxi_egon -A arm -d in.bin
out.bin`. Padding is 8192 bytes by default (`PAD_SIZE` in `tools/sunxi_egon.c`)
so that one image works for NAND as well as SD; the minimum is 512. **[V]**
awboot ships its own 200-line `tools/mksunxi.c` doing the same job, invoked as
`mksunxi <file> <pad>`. **[V]**

### 1.3 First-stage size limit [V + B]

The first stage runs entirely in SRAM, before DRAM exists. On T113:

- Load address **0x00020000** when booted by the BROM from SD/SPI/eMMC
  (U-Boot `CONFIG_SUNXI_SRAM_ADDRESS`, `default 0x20000 if ... SUNXI_GEN_NCAT2`,
  which `MACH_SUN8I_R528` selects). **[V]**
- Load address **0x00028000** in FEL mode, because the BROM's own FEL code is
  using the low part of SRAM A1. **[V]** — awboot's Makefile literally builds
  two link scripts, `-D__RAM_BASE=0x00020000` for boot and
  `-D__RAM_BASE=0x00028000` for FEL.

Available size — three independent sources, and they agree once you see what
each is measuring:

| Source | Says |
|---|---|
| sunxi-tools `soc_info.c`, entry `0x1859 /* D1/D1s/R528/T113-S3 */` | `.spl_addr = 0x20000`, **`.sram_size = 160 * 1024`**, `.sid_base = 0x03006000`, `.sid_offset = 0x200` |
| awboot `arch/arm32/mach-t113s3/link.ld` comment | "A1 + DSP0 IRAM + DSP0 DRAM0. **160K in boot mode, 128K in FEL mode**" |
| SyterKit `archive/configs/100ask-t113i_defconfig` | `SPL_BIN_TEXT_BASE=0x20000`, `SPL_BIN_MAX_SIZE=0x20000` (128 KB); `SPL_FEL_TEXT_BASE=0x28000`, `SPL_FEL_MAX_SIZE=0x19000` (100 KB) |

All three **[V]**. The physical window is **160 KB at 0x20000** — SRAM A1 plus
the DSP0 IRAM and DRAM0 aliases, which are contiguous. FEL costs you the first
32 KB. SyterKit's figures are its own conservative budget, not a hardware
limit.

**[I]** Design to **128 KB boot / 100 KB FEL** and you are safe under every
reading.

**[B] "Well inside this" is now a number.** The SPL built from
`configs/t113i_mt32_defconfig` in § 2.9:

```
$ ls -l spl/sunxi-spl.bin spl/u-boot-spl.bin
-rw-r--r-- 1 root root 24576 Sep 18 15:33 spl/sunxi-spl.bin
-rwxr-xr-x 1 root root 18520 Sep 18 15:33 spl/u-boot-spl.bin
$ arm-linux-gnueabihf-size spl/u-boot-spl
   text	   data	    bss	    dec	    hex	filename
  18118	    400	    272	  18790	   4966	spl/u-boot-spl
```

**24576 bytes with the eGON header and padding**, loaded at 0x20060
(`CONFIG_SPL_TEXT_BASE`, which is `SUNXI_SRAM_ADDRESS` + the 0x60 header). It
occupies 0x20000–0x26000 of a 160 KB window ending at 0x48000: **15 % of it**.
In FEL at 0x28000 it ends at 0x2e000, inside the 100 KB budget too. There is
no size problem here and there is not going to be one.

Note also that **mainline sets no SPL size limit at all for sunxi**:
`CONFIG_SPL_SIZE_LIMIT=0x0` in the generated `.config`, and
`common/spl/Kconfig:30-36` has no sunxi default. So nothing will warn you if a
future SPL does overflow SRAM — it will simply not boot. **[B]**

Note also `sid_base 0x03006000 + sid_offset 0x200` = **0x03006200**, which is
exactly U-Boot's `SUNXI_SID_BASE` in the DRAM driver — the same SID block whose
`+0x28` efuse selects the AC remapping table in section 2.5. **[V]**

### 1.4 FEL: how it works and what drives it [V]

FEL is entered when no boot medium holds a valid image, or via a board's FEL
button, or by putting the `fel-sdboot.sunxi` magic binary on an SD card (ships
in sunxi-tools' `bin/`). USB VID:PID is **1f3a:efe8**. There is normally no
on-board indication other than the device appearing on the host. **[V]**

Two tools, and for this SoC the answer is not the obvious one:

| Tool | Licence | T113 support | Verdict |
|---|---|---|---|
| **sunxi-tools** (`sunxi-fel`) | GPL-2.0 | Knows the part: `soc_info.c` has `.soc_id = 0x1859, /* Allwinner D1/D1s/R528/T113-S3 */, .name = "R528"` with the SRAM map and SID base filled in. **[V]** Does generic `read`/`write`/`exec`/`spiflash-write`/`uboot`. **No DRAM init for this family** — it has no per-SoC DDR payloads at all. | Fine for `sunxi-fel uboot u-boot-sunxi-with-spl.bin`, which works because the SPL does its own DRAM init |
| **xfel** (xboot/xfel) | **MIT** | Dedicated `chips/r528_t113.c`, 4600 lines. `xfel ddr {r528-s3,t113-s3,t113-s4}` brings DRAM up *from the host*, plus `spinor`/`spinand` programming, `sid`, `jtag`, `efuse`. | **The one to use.** DRAM-up-then-load is the fast dev loop |

xfel's DDR init is the interesting capability: it uploads a 24064-byte
eGON.BT0 DDR-init image to SRAM 0x28000, overwrites the 96-byte `dram_para_t`
at 0x28038, and executes it. Afterwards you can `xfel write 0x40000000
payload.bin; xfel exec 0x40000000` and you are running from DRAM with no
bootloader on the board at all. **[V — read from `chips/r528_t113.c`.]**

**xfel has no `t113-i` preset.** Its three presets are all co-packaged-DRAM
parts. `scripts/fel-ddr-t113i.sh` in this directory reproduces the three steps
by hand with external-DDR3 parameters; see section 2.6.

---

## 2. DRAM init for an external-DDR3 T113-i

This is the question the plan calls the single biggest unknown. It is
substantially **resolved**, and the answer is better than expected.

### 2.1 Where the code lives [V]

One driver, shared across the whole die family:

```
u-boot/drivers/ram/sunxi/dram_sun20i_d1.c      1451 lines, GPL-2.0+
u-boot/drivers/ram/sunxi/dram_sun20i_d1.h        84 lines
u-boot/drivers/ram/sunxi/Kconfig                 60 lines
```

Its own header: *"Allwinner D1/D1s/R528/T113-sx DRAM initialisation … As usual
there is no documentation for the memory controller or PHY IP used here. The
baseline of this code was lifted from awboot[1], which seems to be based on
some form of de-compilation of some original Allwinner code bits."*

Kconfig help for `DRAM_SUN20I_D1`: *"This enables support for the DRAM
controller driver covering the Allwinner D1/R528/T113s SoCs."* **[V]**

So the lineage is: Allwinner boot0 blob → decompiled into awboot → cleaned up
into mainline U-Boot (v2024.01) → re-adopted by awboot, which now carries the
U-Boot version with a `GPL-2.0+` SPDX tag and the U-Boot file header. It is
**GPL source, not a blob.** That is the answer to "is this an Allwinner blob or
the Tina SDK?" — **it is neither, any more.**

SyterKit carries its own copy at `drivers/dram/dram-sun8iw20.c`; xfel carries
a precompiled eGON image of it. Same code, four packagings. **[V]**

### 2.2 What is auto-detected, and what is not [V]

This is why density is a non-problem. `init_DRAM()` calls
`auto_scan_dram_config()`, which calls:

- `auto_scan_dram_rank_width()` — runs a DQS training pass and reads the error
  bits to determine **1 or 2 ranks** and **half or full DQ width**
  (`dram_para2` bits 12 and 0).
- `auto_scan_dram_size()` — walks the address lines per rank to find **row
  count, bank count (4 or 8), and page size**, writing the result into
  `dram_para1`.
- `DRAMC_get_dram_size()` — computes the total in MB from the controller
  registers afterwards.

```c
/* dram_sun20i_d1.c */
debug("rank %d row = %d\n", rank, i);
debug("rank %d bank = %d\n", rank, (j + 1) << 2); /* 4 or 8 */
debug("rank %d page size = %d KB\n", rank, pgsize);
```

**Consequence: you do not configure the DDR3 density, organisation, rank count
or bus width anywhere.** A 2 Gbit x16 part and a 4 Gbit x16 part use the same
defconfig. **[V]**

### 2.3 What *is* configurable, and what is hardcoded [V + X]

Mainline exposes seven knobs, and only seven:

| Kconfig | `dram_para_t` field | What it is |
|---|---|---|
| `DRAM_CLK` | `dram_clk` | MHz. 792 on every published T113 board |
| `SUNXI_DRAM_TYPE_DDR3` | `dram_type` | 2/3/6/7 = DDR2/DDR3/LPDDR2/LPDDR3 |
| `DRAM_ZQ` | `dram_zq` | ZQ calibration, `0x7b7bfb` everywhere |
| `DRAM_SUNXI_ODT_EN` | `dram_odt_en` | on-die termination enable |
| `DRAM_SUNXI_TPR11` | `dram_tpr11` | **per-byte-lane read/write delay trim** |
| `DRAM_SUNXI_TPR12` | `dram_tpr12` | **per-byte-lane read/write delay trim** |
| `DRAM_SUNXI_TPR13` | `dram_tpr13` | feature/behaviour bitfield |

**[X] A structural correction, because `scripts/ddrpara.py` asserted the
opposite.** Mainline does **not** carry the flat 24-word Allwinner structure.
It splits it in two (`dram_sun20i_d1.h:42-75`):

- `dram_para_t` — **21 words**, `const`, every field from Kconfig or a literal:
  `dram_clk`, `dram_type`, `dram_zq`, `dram_odt_en`, `mr0..mr3`, `tpr0..tpr12`.
  **No `dram_para1`, no `dram_para2`, no `dram_tpr13`.**
- `dram_config_t` — 3 words, **mutable**, built at run time in `init_DRAM()`
  (`:1254-1259`): `.dram_para1 = 0x000010d2`, `.dram_para2 = 0`,
  `.dram_tpr13 = CONFIG_DRAM_SUNXI_TPR13`. The auto-scan writes back into it.

So `para1` and `para2` are **hardcoded seeds in U-Boot**, not configurable at
all, and `tpr13` lives in a different struct from the one its name suggests.
The flat 24-word order is the *on-disk boot0* order, and the place it is
really laid out flat is xfel (`chips/r528_t113.c:31-57`, `struct
ddr3_param_t`, 24 u32 followed by `reserve[8]`). `ddrpara.py` says this
correctly now. **[V]**

Everything else in the 24-word structure is a **compile-time constant in the
driver** (`static const dram_para_t para = { ... }` at `:1370`):

```c
.dram_mr0  = 0x1c70,    .dram_mr1  = 0x42,    .dram_mr2 = 0x18,
.dram_tpr0 = 0x004a2195, .dram_tpr1 = 0x02423190, .dram_tpr2 = 0x0008b061,
.dram_tpr5 = 0x48484848, .dram_tpr6 = 0x00000048,
```

Three notes on that, each verified by reading the code:

1. **The DDR3 JEDEC timings are computed, not taken from `tpr0..tpr2`.**
   `mctl_set_timing_params()` derives `tfaw`, `trrd`, `trcd`, `trc`, `txp`,
   `twtr`, `twr`, `trp`, `tras`, `trfc`, `trefi`, `tcl`, `tcwl`, MR0 and MR2
   from nanosecond constants and `CONFIG_DRAM_CLK`, via
   `ns_to_t(ns) = DIV_ROUND_UP((CONFIG_DRAM_CLK/2) * ns, 1000)`. The `tpr0`
   comments (`//DRAMTMG0`) are vestigial. **[V]**
2. **[X] `CONFIG_DRAM_SUNXI_TPR0` is a dead Kconfig symbol — and it is NOT
   harmless to leave out.** Dead it is: `grep -n TPR0 dram_sun20i_d1.c` finds
   only comments. But `drivers/ram/sunxi/Kconfig` declares it — and
   `DRAM_SUNXI_ODT_EN`, `_TPR11`, `_TPR12`, `_TPR13` — as `hex` with **no
   `default` line** (Kconfig lines 10-34). A Kconfig `hex` symbol with no
   default and no value in the defconfig is *unset*, and `make syncconfig`
   stops and asks:

   ```
   DRAM TPR0 parameter (DRAM_SUNXI_TPR0) [] (NEW)
   Error in reading or end of file.
   ```

   On a terminal that hangs forever; under `</dev/null` it fails. The first
   pass's `configs/t113i_mt32_defconfig` omitted TPR0 on exactly the reasoning
   above, and **did not build.** Corrected in § 2.9. Note that both published
   T113 defconfigs — `mangopi_mq_r_defconfig` and taterli's
   `t113i_minievm_defconfig` — do set it, which is why nobody noticed. **[B]**
3. **`dram_mr1 = 0x42` is used as-is for DDR3** (`mr1 = para->dram_mr1;` in the
   DDR3 branch). MR1 bit 1 = output drive RZQ/7 (34 Ω), bit 6 = Rtt_Nom RZQ/2
   (120 Ω). **[V for the code path; I for the decode — that is the standard
   JEDEC MR1 encoding, not something the source states.]** If our DDR3 part or
   topology wants different termination, **this is a driver patch, not a
   defconfig line**. It is the one genuinely missing knob.

The generic `DRAM_TIMINGS_DDR3_1066F_1333H` / `DRAM_TIMINGS_DDR3_800E_...`
speed-bin choice in `arch/arm/mach-sunxi/Kconfig` is gated
`if MACH_SUN4I || MACH_SUN5I || MACH_SUN7I` and **does not apply to this
family**. **[V]**

### 2.4 The actual external-DDR3 numbers [V]

This is the find that closes the question. SyterKit carries a board file for
the **100ask T113i-Industrial DevKit**, which its own README describes as
*"Main control: Allwinner T113i … DRAM: DDR3 512MB"* — i.e. external DDR3 —
alongside a 100ask T113-S3 board with the co-packaged 128 MB.

`vendor/syterkit/archive/boards/100ask-t113i/board.dts` carries the full
24-word `allwinner,dram-parameters` array, and the field order is pinned by
`include/drivers/dram/dram.h`. Decoding it and diffing against the S3 board in
the same repo:

```
$ scripts/ddrpara.py diff --preset t113-s3 --preset t113i-100ask
field        t113-s3      t113i-100ask
dram_odt_en  0x00000000   0x00000001
dram_tpr11   0x00340000   0x00770000
dram_tpr12   0x00000046   0x00000002
dram_tpr13   0x34000100   0x34050100

4 field(s) differ
```

**Four fields. All four are mainline Kconfig symbols.** Everything else —
clock, type, ZQ, MRs, all the TMG words — is byte-identical between the
co-packaged S3 and the external-DDR3 T113-i. **[V]**

**[B] Re-checked mechanically in the second pass**, because this is the claim
the whole workstream rests on. `scripts/ddrpara.py selftest` re-reads
SyterKit's two `board.dts` files and xfel's embedded payload from `vendor/`
every time it runs and compares them word for word against the presets in
`ddrpara.py`:

```
PASS t113-s3 vs t113i-100ask differ in exactly 4 fields
PASS all four are mainline Kconfig symbols
PASS t113i-tronlong == t113-s3 (external DDR3, S3 numbers)
PASS SyterKit 100ask-t113i board.dts matches preset t113i-100ask
PASS SyterKit 100ask-t113s3 board.dts matches preset t113-s3
PASS its parameters at 0x38 decode to the t113-s3 preset
```

Three independent sources, one number each, no drift. The claim stands.

`scripts/ddrpara.py kconfig --preset mt32-t113` emits exactly the defconfig
block, and `configs/t113i_mt32_defconfig` is that block in context.

### 2.5 The catch: AC remapping, and why it is a *layout* question [V + B + I]

Rewritten 2026-09-18 with the driver in front of me instead of remembered. The
conclusion is the same — **route straight through** — but the first pass
asserted the key step and this pass derives it, which matters, because this is
the one thing in this document that gets etched into copper.

`dram_tpr13` differs between the co-packaged and external-DDR3 sets by
`0x00050000`: bits 16 and 18.

#### What the code actually does

`mctl_phy_ac_remapping()`, `dram_sun20i_d1.c:655-717`, in full shape: **[V]**

```c
if (para->dram_type != DDR2 && para->dram_type != DDR3)
        return;                                     /* :669-672 no write at all */

fuse = (readl(SUNXI_SID_BASE + 0x28) & 0xf00) >> 8; /* :669  bits [11:8]      */

if (sid_read_soc_chipid() == SUNXI_CHIPID_T113M4020DC0)
        return;                                     /* :677-678 the T113-S4    */

if (para->dram_type == DDR2) {
        if (fuse == 15) return;                     /* :681-682 no write at all */
        cfg = ac_remapping_tables[6];
} else {
        if (config->dram_tpr13 & 0xc0000) {         /* :686                     */
                cfg = ac_remapping_tables[7];
        } else {
                switch (fuse) {                     /* :689-697                 */
                case 8: cfg = tables[2]; break;   case 9:  cfg = tables[3]; break;
                case 10: cfg = tables[5]; break;  case 11: cfg = tables[4]; break;
                default: case 12: cfg = tables[1]; break;
                case 13: case 14: cfg = tables[0]; break;
                }
        }
}
/* :699-717 four 5-bit-packed words, then the first one again with bit 0 set */
writel(..., 0x3102500); writel(..., 0x3102504);
writel(..., 0x3102508); writel(..., 0x310250c);
writel(... | 1, 0x3102500);
```

Three corrections to the first pass's reading, all small and all worth having:

1. **[X] The test is `& 0xc0000` — bits 18 OR 19, not bit 18.** Either one
   forces table 7. The first pass said "bit 18". `ddrpara.py acremap` now
   models both.
2. **[X] The "table 0 is no remap" gloss conflated two different states.** The
   driver has *three* outcomes, not two: write table N; write table 0; or
   **`return` without writing the registers at all**. The third is what LPDDR,
   DDR2-with-fuse-15 and the T113-S4 get. Table 0 is a table that gets written,
   plus the bit-0 enable.
3. There is **no way to select table 0 from a defconfig.** `tpr13` can force
   table 7 and nothing else. Table 0 is reachable only if the efuse reads 13 or
   14 — or by a one-line driver patch. **[B]**, proved by exhaustion:
   `ddrpara.py selftest` checks all 32 tpr13 bits against all 16 fuse values.

#### Is table 0 really the identity? — the part that was asserted, now derived

Mainline's own comment above the function says *"It is unclear which lines are
being remapped"*, so nobody upstream knows either. Table 0 is `[0] = { 0 }`,
i.e. 22 zeros. The first pass wrote "table 0 is all zeros (identity / no
remap)". **All-zeros is not obviously an identity** — if entry *i* means "pin
*i* is driven by internal signal *cfg[i]*", then all-zeros would mean every
address pin carries signal 0, which is nonsense.

Here is the argument that it does mean identity, and it comes out of the other
seven tables. Tables 1–5 contain zeros in a few positions. Compare, for each
table, **the set of positions holding zero** with **the set of values from
1..22 that the table's non-zero entries never use**:

```
$ scripts/ddrpara.py selftest
PASS a 0 entry means 'identity here': holds for tables [0, 1, 2, 3, 4, 5, 7]
```

| table | zero at positions (1-based) | values missing from 1..22 |
|---|---|---|
| 1 | 15, 16 | 15, 16 |
| 2 | 15, 16 | 15, 16 |
| 3 | 14, 15, 16 | 14, 15, 16 |
| 4 | 14, 15, 16 | 14, 15, 16 |
| 5 | 14, 15, 16 | 14, 15, 16 |
| 7 | — (a full permutation of 1..22) | — |
| **0** | **all 22** | **all 22** |

They match exactly, in every DDR3 table. If `0` meant "signal 0" the two
columns would be unrelated; they are equal in five independent tables. So
**`0` at position *i* means "leave position *i* alone", and table 0, being all
zeros, is the identity permutation — straight through.** **[B]**

(The sole exception is table 6, the DDR2-only one, which uses the value 1
twice and drops 18. That looks like a defect in the de-compiled original
rather than a counter-example; it is not on our path, and `selftest` calls it
out by name.)

#### What that means for the PCB [I]

The 22 lines are almost certainly A0–A15 (16) + BA0–BA2 (3) + RAS, CAS, WE (3)
= exactly 22. **That arithmetic is the inference; the driver does not say so**,
and it is the weakest link in this section. If it holds, a wrong table is not
subtly wrong — it would put a *command* strobe on an *address* pin and DRAM
init would fail outright at training, loudly, on the first boot.

The practical consequence is better than the first pass made it sound:

- **Route A0–A15, BA0–BA2, RAS, CAS, WE straight through to the datasheet's
  pin names.** That is what every vendor reference design does, and it is the
  routing exactly one of the eight tables corresponds to.
- **Selecting which one is software.** All eight tables are in GPL source, in
  one 22-entry array, and switching between them is a one-line patch. A wrong
  choice costs a rebuild; wrong copper costs a respin. That asymmetry is the
  whole argument, and it survives.
- Do **not** ship `TPR13` bit 18 by default on our own board unless we have
  reason to believe the 100ask routing is the datasheet routing. Our preset
  inherits `0x34050100` from 100ask and therefore *does* force table 7 — that
  is the first thing to try and the first thing to change.

#### The one command that settles it

```sh
xfel read32 0x03006228        # SUNXI_SID_BASE 0x03006200 + 0x28
                              # the AC-remap efuse is bits [11:8]
scripts/ddrpara.py acremap --preset mt32-t113 --fuse <that nibble>
```

0x03006200 is `SUNXI_SID_BASE` for `SUNXI_GEN_NCAT2`
(`arch/arm/include/asm/arch-sunxi/cpu_sunxi_ncat2.h:21`, and the driver's own
fallback `#define` at `dram_sun20i_d1.c:26`), and it agrees with sunxi-tools'
`sid_base 0x03006000 + sid_offset 0x200` and with xfel, which reads the chip ID
from 0x03006200. **[V]** `scripts/fel-ddr-t113i.sh` now issues that `read32`
as its first step, before it brings DRAM up, so the number lands in the log of
the very first FEL session on the very first board.

`ddrpara.py acremap` prints the exact register writes for any (tpr13, fuse)
pair, so the value can be checked against a live board with `xfel read32
0x3102500` too: **[B]**

```
$ scripts/ddrpara.py acremap --preset mt32-t113 --fuse 12
efuse 12                     -> table 7
    dram_tpr13 & 0xc0000 (bit 18 or bit 19) forces table 7 (:686-687)
    cfg = [3, 2, 4, 7, 9, 1, 17, 12, 18, 14, 13, 8, 15, 6, 10, 5, 19, 22, 16, 21, 20, 11]
      writel(0x12720860, 0x3102500)
      writel(0x1ae93221, 0x3102504)
      writel(0x005519e8, 0x3102508)
      writel(0x174ac2d3, 0x310250c)
      writel(0x12720861, 0x3102500)
```

One more unknown the driver hands us: it skips remapping entirely when the SoC
chip ID is `0x7200` (`SUNXI_CHIPID_T113M4020DC0`, the T113-S4). **Nobody knows
the T113-i's chip ID.** If it happens to be 0x7200, mainline will never remap
on our part at all, and the whole question dissolves. `xfel sid` on the first
board answers that in the same breath as the efuse. **[I]**

### 2.6 A second, contradictory data point — and why it is reassuring [V]

`nickfox-taterli/t113-uboot` is a U-Boot 2025.10 fork titled *"Mainline UBoot
for T113i MiniEVM (Tronlong)"* — another **external-DDR3 T113-i** board. Its
`configs/t113i_minievm_defconfig`:

```
CONFIG_DRAM_SUNXI_ODT_EN=0
CONFIG_DRAM_SUNXI_TPR11=0x340000
CONFIG_DRAM_SUNXI_TPR12=0x46
CONFIG_DRAM_SUNXI_TPR13=0x34000100
CONFIG_DRAM_CLK=792
CONFIG_DRAM_ZQ=8092667
CONFIG_MACH_SUN8I_R528=y
```

Those are the **T113-S3's values, unchanged, on external DDR3**. And the whole
delta of that fork against mainline U-Boot is a board DTS plus a UART0 PG
pinmux commit — the DRAM driver itself is byte-identical to mainline except for
a T113-S4 chip-ID check that postdates its base. **[V — diffed locally, 26
lines, all of it the S4 chipid hunk.]**

So: two published external-DDR3 T113-i boards, two different parameter sets,
both reported working. **[V for the files; the "working" claim is the repos'
own, unverified by us.]**

**What that means.** These four fields are **board trim, not part constants**.
There is no secret T113-i parameter set to obtain from Allwinner. Either
published set is a legitimate starting point; the ones that will need sweeping
on our PCB are `tpr11` and `tpr12` (byte-lane delay compensation, i.e. trace
length), and `tpr13` bit 18 has to agree with our routing.

### 2.7 So: blob, SDK, or source? [V]

**Source, GPL-2.0+, in mainline U-Boot since v2024.01.** Verified by tag:

```
v2023.10: [configs/mangopi_mq_r_defconfig no] [drivers/ram/sunxi/dram_sun20i_d1.c no]
v2024.01: [configs/mangopi_mq_r_defconfig YES] [drivers/ram/sunxi/dram_sun20i_d1.c YES]
```

You do not need a vendor boot0, the Tina SDK, or a blob. The plan's stated
risk — *"the DRAM parameter set is the specific unknown"* — can be downgraded.

**But keep one escape hatch.** If a board we buy does not come up, the vendor
boot0 on its SPI NAND or SD card contains a known-good parameter set at offset
0x38, and `scripts/ddrpara.py decode <boot0.bin>` will print it in named form.
That is the fallback, and it is cheap.

### 2.8 The tooling in this directory

- **`scripts/ddrpara.py`** — decode / diff / patch `dram_para_t` blocks.
  Presets for `t113-s3`, `t113-s4`, `t113i-100ask`, `t113i-tronlong` and
  `mt32-t113`, each with its provenance in a comment. Subcommands:
  `decode`, `kconfig`, `diff`, `extract` (pull the DDR payload out of xfel's C
  source), `patch` (stamp a preset into a payload or boot0 image).
- **`scripts/fel-ddr-t113i.sh`** — the thing xfel does not have: bring up
  *external* DDR3 over FEL, then optionally load and run a payload. Extracts
  xfel's payload, patches our parameters in at 0x38, `xfel write` + `xfel exec`.
  New in the second pass: **`acremap`** (which remapping table a
  `tpr13`/efuse pair selects, and the exact register writes) and
  **`selftest`** (20 checks, no hardware, no network).
- **`scripts/fel-ddr-t113i.sh`** — as before, plus `DRY_RUN=1`, which does
  everything except touch USB and prints the `xfel` commands it would run.
  That is its self-test, and it passes. It now also reads the AC-remapping
  efuse at SID+0x28 as its *first* step, before DRAM.
- **`configs/t113i_mt32_defconfig`** — a U-Boot defconfig for our board with
  every non-obvious line's provenance recorded. **It builds** (§ 2.9), against
  commit `211de43d0f95`.
- **`dts/sun8i-t113i-mt32.dts`** — new. The board devicetree the defconfig
  names. Compiles to a DTB; every pin choice is a proposal, not a schematic.
- **`tools/build-uboot.sh`** — new. Clone-to-artefacts in one command.
- **`tools/handover-probe.S`, `tools/handover-probe.sh`** — new. The
  `mrs`/`mrc` dump § 4.4 asked for, run today under QEMU against a real sunxi
  U-Boot. This is where § 4.4's measured numbers come from.

`scripts/ddrpara.py selftest` is the regression test for all of the above:

```
$ scripts/ddrpara.py selftest
PASS dram_para_t is 24 words / 96 bytes
PASS every preset defines every field
PASS pack/unpack round trip at 0x38
PASS eGON magic detector
PASS t113-s3 vs t113i-100ask differ in exactly 4 fields
PASS all four are mainline Kconfig symbols
PASS t113i-tronlong == t113-s3 (external DDR3, S3 numbers)
PASS 8 AC remapping tables of 22 entries
PASS a 0 entry means 'identity here': holds for tables [0, 1, 2, 3, 4, 5, 7]
PASS table 0 is therefore straight-through
PASS table 7 is a full non-identity permutation of 1..22
PASS tpr13 bit 18 and bit 19 both force table 7 (mask 0xc0000)
PASS no tpr13 bit can select table 0; only the efuse can
PASS our own preset forces table 7
PASS xfel's own DDR payload is an eGON.BT0 image  -- 24064 bytes
PASS its parameters at 0x38 decode to the t113-s3 preset
PASS SyterKit 100ask-t113i board.dts matches preset t113i-100ask
PASS SyterKit 100ask-t113s3 board.dts matches preset t113-s3
PASS every symbol kconfig emits exists in u-boot's ram/sunxi/Kconfig
PASS the hex DRAM_SUNXI_* symbols have NO Kconfig default, so a defconfig must set all five

20 checks, 0 failed
```

The last six are the interesting ones: they re-derive this document's
parameter claims from the vendor trees under `vendor/` every time they run,
so a preset cannot drift away from its citation without the test noticing.
**[B]**

### 2.9 The build. This is the part that was reading and is now a binary. [B]

Everything in this section is output from commands run in this container on
2026-09-18 against U-Boot `211de43d0f95`. One command reproduces all of it:

```sh
tools/build-uboot.sh                # our board
tools/build-uboot.sh mangopi_mq_r   # the in-tree T113-S3, as a control
```

It clones U-Boot into `vendor/` if it is not there, copies
`configs/t113i_mt32_defconfig` and `dts/sun8i-t113i-mt32.dts` into the
checkout, configures and builds. Host packages beyond the cross compiler,
learned by hitting each one in turn: `flex` (the Kconfig lexer), `bison`,
`swig` + `python3-dev` (`scripts/dtc/pylibfdt`), `libssl-dev`,
`libgnutls28-dev` (`tools/mkeficapsule`), `uuid-dev`. U-Boot builds `tools/`
unconditionally, so all of them are needed even though the SPL needs none.

#### Does the defconfig apply cleanly to mainline?

Every symbol in it exists:

```
OK    CONFIG_ARM            OK    CONFIG_DRAM_CLK
OK    CONFIG_ARCH_SUNXI     OK    CONFIG_SUNXI_DRAM_TYPE_DDR3
OK    CONFIG_MACH_SUN8I_R528 OK   CONFIG_DRAM_ZQ
OK    CONFIG_DEFAULT_DEVICE_TREE  OK CONFIG_DRAM_SUNXI_ODT_EN
OK    CONFIG_SPL            OK    CONFIG_DRAM_SUNXI_TPR11
OK    CONFIG_SUNXI_MINIMUM_DRAM_MB OK CONFIG_DRAM_SUNXI_TPR12
OK    CONFIG_CONS_INDEX     OK    CONFIG_DRAM_SUNXI_TPR13
```

and the resulting `.config` differs from `mangopi_mq_r_defconfig`'s in exactly
the lines it is meant to. But it did **not build**, for two reasons, both now
fixed in the file:

1. **`CONFIG_DRAM_SUNXI_TPR0` was missing** and has no Kconfig default, so
   `syncconfig` prompted forever. See § 2.3 note 2. This is the correction
   that would have cost real time on a first board, because the failure mode
   is a hang with no error.
2. **`CONFIG_DEFAULT_DEVICE_TREE="allwinner/sun8i-t113i-mt32"` named a
   devicetree that did not exist.** It does now:
   `dts/sun8i-t113i-mt32.dts` in this directory, which `tools/build-uboot.sh`
   copies into `dts/upstream/src/arm/allwinner/`. It is a proposal to
   workstream B, not a reading of a schematic, and every pin in it says so.

#### The artefacts, and their real sizes

```
$ ls -l spl/sunxi-spl.bin spl/u-boot-spl.bin u-boot.bin u-boot.img \
        u-boot-sunxi-with-spl.bin
-rw-r--r-- 1 root root  24576 spl/sunxi-spl.bin
-rwxr-xr-x 1 root root  18520 spl/u-boot-spl.bin
-rw-r--r-- 1 root root 457992 u-boot.bin
-rw-r--r-- 1 root root 458056 u-boot.img
-rw-r--r-- 1 root root 490824 u-boot-sunxi-with-spl.bin
```

The first pass's § 3 table guessed "SPL ~32 KB + U-Boot proper ~600 KB" and
tagged it **[I]**. Real answer: **SPL 24 KiB, U-Boot proper 447 KiB, combined
image 479 KiB.** The guess was 30 % high on both, which for an order-of-
magnitude estimate is fine, but there is no reason to estimate any more.

The combined image is exactly `sunxi-spl.bin` padded to 32768 and then
`u-boot.img` concatenated — 32768 + 458056 = 490824, which is the byte count
above. That layout is `arch/arm/dts/sunxi-u-boot.dtsi:29-41`, a binman image
with `min-size = <0x8000>`, matching `CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR
= 0x40` (sector 64 = 32 KiB past the SPL, which itself sits at sector 16).
**[B]**

#### Do the DRAM numbers actually reach the binary?

The driver is fully inlined by GCC, so the `dram_para_t` never appears as data
and most of the parameters are folded into immediates. `dram_tpr13` survives
as a literal, and it is a clean discriminator:

```
mangopi size 18600
   tpr13 s3 0x34000100      count=1
   tpr13 ours 0x34050100    count=0
mt32 size 18520
   tpr13 s3 0x34000100      count=0
   tpr13 ours 0x34050100    count=1
bytes differing in the common prefix: 6030 of 18520
```

Same driver, different numbers, different code. The defconfig is doing what it
claims. **[B]**

#### What this does NOT prove

Nothing about DRAM coming up. Nothing about the PHY, the training, the
remapping table, the clocks or the pinmux. The SPL is a 24 KiB binary that
exists; whether it brings an external DDR3 part up on our copper is the thing
only silicon can say. Everything below the seam in `docs/PLAN.md` § 0.5 is
still below the seam.

---

## 3. Bootloader options

| | Mainline U-Boot | awboot | xboot | SyterKit | Vendor (Tina/boot0) |
|---|---|---|---|---|---|
| Repo | u-boot/u-boot | szemzoa/awboot | xboot/xboot | YuzukiHD/SyterKit | Allwinner |
| Licence | GPL-2.0+ | GPL-2.0+ (DRAM code) | GPL | GPL-2.0 | proprietary |
| Last commit seen | 2026-09-17 (master) | 2025-11-21 | active | 2026-09-12 | — |
| DRAM init | **yes**, `dram_sun20i_d1.c` | **yes**, same code | yes | yes, `dram-sun8iw20.c` | yes |
| T113-S3 | `mangopi_mq_r_defconfig` | `mach-t113s3` | `PLATFORM=arm32-t113s3` | archived board | yes |
| T113-S4 | chip-ID special case in the DRAM driver | `mach-t113s4` | — | — | yes |
| **T113-i (external DDR3)** | **no in-tree board**, but the driver covers it; taterli's fork adds one | no mach dir; change 7 `#define`s in `dram.c` | no | **`archive/boards/100ask-t113i`, with real parameters** | yes |
| Size | **SPL 24 KiB + U-Boot proper 447 KiB, measured** | single ~100 KB binary | ~300 KB+ | small, configurable | — |
| Boot time | ~1 s to prompt | sub-second, its stated purpose | fast | fast | — |
| Payload handoff | `go` / `bootelf` / `booti` / `bootm`, env scripting, FAT/ext4/TFTP | loads a fixed kernel+dtb off FAT or SPI | its own app model | `syter_boot` app, or write your own | — |

The **Boot time** row is **[I]** — order-of-magnitude estimates from the shape
of each project, not measurements. Nobody has timed any of these on a T113.
The **Size** row's U-Boot entry is now **[B]**: see § 2.9. The first pass
guessed 32 KB + 600 KB and the real numbers are 24 KiB + 447 KiB, so the
estimate was about 30 % high; the other three columns are still guesses. Everything else in the table is **[V]** from the checkouts under
`vendor/`.

Detail per option:

**Mainline U-Boot.** T113 support landed in **v2024.01** (verified by tag in
3.–2.7 above): the DRAM driver, `sun8i-t113s.dtsi`, and
`mangopi_mq_r_defconfig`. `MACH_SUN8I_R528` selects `CPU_V7A`,
`SPL_ARMV7_SET_CORTEX_SMPEN`, `SUNXI_GEN_NCAT2`, `SUNXI_NEW_PINCTRL`,
`MMC_SUNXI_HAS_NEW_MODE`, `SUPPORT_SPL`, `DRAM_SUN20I_D1`, and implies
`OF_UPSTREAM`. **[V]** What you get: DRAM, clocks, MMC, SPI flash, UART
console, USB, a devicetree, FAT/ext4, TFTP, an environment, and a scriptable
boot. What you don't get in-tree: a T113-i board, which is a DTS plus the four
DRAM lines. **The most capable option and the one with a future.**

**awboot.** Purpose-built "small linux bootloader for Allwinner T113-s3,
T113-s4, V851s", alive as of Nov 2025, with a recent PSCI merge. Builds four
variants (fel / spi / sdmmc / emmc) with valid eGON headers. Reads kernel+DTB
off FAT32 on SD or off SPI NOR/NAND. It now carries the *U-Boot* DRAM code with
the U-Boot header and an SPDX tag, so it and U-Boot are the same driver. For a
T113-i you would add a `mach-t113i` or edit seven `#define`s at the top of
`arch/arm32/mach-t113s3/dram.c` (lines 35-41). **[V]** It also has the cleanest
existing **PSCI / second-core** implementation for this SoC — see section 4.3.
**Attractive if boot time matters; weak if you want a shell.**

**xboot.** Has `src/arch/arm32/mach-t113s3/` with a driver set including G2D,
and a documented build (`make PLATFORM=arm32-t113s3`) and flash flow via xfel.
**[V]** It is really an application framework with a Lua runtime, not a
bootloader you hand off from; it would be a *replacement* for our payload
rather than a stage under it. **Not the shape we want.**

**SyterKit.** A bare-metal firmware framework and bootloader toolkit, GPL-2.0,
very active (Sept 2026), Kconfig + Kbuild + device trees compiled to C. Its
current board list is A523/T527/H618/T536-class parts; **the T113 boards are in
`archive/`** — `100ask-t113s3` and `100ask-t113i`. The archived T113-i board is
nonetheless the single most valuable artefact this research turned up, because
it carries the external-DDR3 parameters. **[V]** As a *bootloader* it is a
risk: our target is archived, so we would be maintaining it. **Use it as a
parameter source and a driver reference, not as the bootloader.**

**Vendor route (Tina Linux / boot0).** Not needed, per 2.7. Keep the vendor
boot0 of whatever dev board we buy as a DRAM-parameter fallback.

### Recommendation

**Mainline U-Boot**, with a board DTS and the four DRAM lines added. It is the
only option that is simultaneously (a) covering our exact part family in
upstream-maintained GPL source, (b) able to load an arbitrary payload off FAT
or TFTP and start it, and (c) still going to exist in three years. Boot time is
a second, which for a synth module that is switched on and left on is not worth
trading away the tooling.

Keep **xfel** beside it as the development loop (section 6) and as the
recovery path.

---

## 4. Loading a non-Linux payload

### 4.1 The four commands, and the CPU state each leaves [V + B]

This matters more than it looks, because an RTOS's reset code usually assumes
caches and MMU are off.

**This table is now measured, not inferred.** `tools/handover-probe.sh` builds
a 424-byte payload that prints CPSR and SCTLR over the UART and then spins,
wraps it with `mkimage`, and boots it three ways from a real sunxi U-Boot under
QEMU. § 6.4 explains why an Allwinner H3 is a legitimate stand-in for this
particular question. Raw output: **[B]**

```
######## bootm  (mainline defaults: ARMV7_NONSEC=y, ARMV7_VIRT=y) ########
PROBE cpsr  = 0x600001da      PROBE r0 = 0x00000000
PROBE sctlr = 0x00c50078      PROBE r1 = 0x00000000
                              PROBE r2 = 0x49ff5000

######## bootm with bootm_boot_mode=sec ########
PROBE cpsr  = 0x600001d3      PROBE r0 = 0x00000000
PROBE sctlr = 0x00c50878      PROBE r1 = 0x00000000
                              PROBE r2 = 0x49ff5000

######## go (no cleanup_before_linux) ########
PROBE cpsr  = 0x600001d3      PROBE r0 = 0x00000001
PROBE sctlr = 0x00c5187d      PROBE r1 = 0x79f6bcdc
                              PROBE r2 = 0x79f6bcdc
```

Decoded:

| Command | mode | MMU (SCTLR.0) | D-cache (.2) | I-cache (.12) | IRQ/FIQ/A | r0, r1, r2 |
|---|---|---|---|---|---|---|
| `bootm`, mainline defaults | **HYP (0x1a), non-secure** | **off** | **off** | **off** | all masked | 0, machid, FDT |
| `bootm`, `bootm_boot_mode=sec` | **SVC (0x13), secure** | **off** | **off** | **off** | all masked | 0, machid, FDT |
| `go <addr>` | SVC (0x13), secure | **ON** | **ON** | **ON** | as U-Boot left them | argc, argv, argv |
| `bootelf [-p] <addr>` | not measured; same call path as `go` **[V by source]** | on | on | on | as U-Boot left them | argc, argv |

Three things fall out that the first pass did not have:

- **`go` really does hand over a live MMU and live caches.** SCTLR = 0x00c5187d,
  bits 0, 2 and 12 all set. That claim is now a measurement rather than a
  reading, and it is the whole reason this project uses `bootm`.
- **`bootm` also turns the I-cache off**, which the first pass's table did not
  mention. `cleanup_before_linux_select(CBL_ALL)` does `dcache_disable()`,
  `v7_outer_cache_disable()`, `invalidate_dcache_all()`, `icache_disable()`,
  `invalidate_icache_all()` — `arch/arm/cpu/armv7/cpu.c:38-58`. **[V + B]**
- **`bootm` lands in HYP, not SVC.** See § 4.4; this is the correction that
  matters most.

One caveat, stated because it is real: in HYP mode `MRC p15,0,Rt,c1,c0,0`
reads **HSCTLR**, not SCTLR, so the first row's SCTLR is the Hyp banked copy.
Bits 0, 2 and 12 carry the same meanings in both, so the MMU/cache reading
holds; the difference in bit 11 between rows 1 and 2 is an artefact of that
banking and means nothing.

Verified in source:

```c
/* cmd/boot.c:41, the entire body of do_go() that matters */
rc = do_go_exec((void *)addr, argc - 1, argv + 1);   /* no cleanup at all */

/* arch/arm/lib/cmd_boot.c:33-40, the ARM override of do_go_exec() */
unsigned long do_go_exec(ulong (*entry)(int, char * const []),
                         int argc, char *const argv[])
{
        ulong addr = (ulong)entry | 1;    /* only to keep ARMv7-M in Thumb */
        entry = (void *)addr;
        return entry(argc, argv);         /* still just a call */
}

/* arch/arm/lib/bootm.c - cleanup_before_linux() has exactly three call sites
   in this file, :269 (arm64 boot_jump_linux), :331 (arm32 boot_jump_linux),
   :414 (boot_prep_vxworks), and NONE of them is reachable from `go`.        */
bootm_final(flag);
cleanup_before_linux();                                        /* :331       */

/* arch/arm/cpu/armv7/cpu.c:81-84 */
int cleanup_before_linux(void) { return cleanup_before_linux_select(CBL_ALL); }
/* include/cpu_func.h:106-109:  CBL_DISABLE_CACHES = 1<<0, CBL_ALL = 3        */

/* arch/arm/cpu/armv7/cpu.c:27-58 */
int cleanup_before_linux_select(int flags) {
        disable_interrupts();
        if (flags & CBL_DISABLE_CACHES) {
                dcache_disable();      /* flushes d-cache AND disables the MMU */
                v7_outer_cache_disable();
                invalidate_dcache_all();
                icache_disable();      /* <- the first pass missed this pair   */
                invalidate_icache_all();
        }
        ...
}
```
**[V]** `grep -rn cleanup_before_linux` across the whole tree finds no call
from `cmd/boot.c`, `lib/elf.c`, `cmd/elf.c` or `arch/arm/lib/cmd_boot.c`. The
only ARM paths that call it are `arch/arm/lib/bootm.c` (the `boot*` family),
`arch/arm/lib/spl.c` (the SPL jumping to U-Boot proper) and
`lib/efi_loader/efi_boottime.c`. **[B]**

**Which `mkimage` type matters, and why.** `bootm` dispatches on the image's
`ih_os` field through `boot_os[]` in `boot/bootm_os.c:529-533`:

```c
static boot_os_fn *boot_os[] = {
        [IH_OS_U_BOOT] = do_bootm_standalone,   /* :26-38: appl(argc, argv); */
        [IH_OS_LINUX]  = do_bootm_linux,        /* -> cleanup_before_linux() */
        ...
};
```

`do_bootm_standalone()` is a plain call — **as bad as `go`**. So the payload
must be `-O linux`, and `mkimage -A arm -O u-boot ...` would quietly undo the
entire point of this section. **[V]**

**[I] Consequence for us:** if the payload is an RTOS or bare-metal image whose
`start.S` sets up its own page tables and enables caches, `go` and `bootelf`
will hand it a live MMU with U-Boot's identity mapping and dirty cache lines,
and it will fault or corrupt memory. Two clean ways out:

1. **Wrap the payload as a Linux-format image and use `bootm`/`booti`.**
   `mkimage -A arm -O linux -T kernel -C none -a 0x40200000 -e 0x40200000`.
   You then inherit `cleanup_before_linux()` for free, and the payload starts
   with MMU off, caches off, interrupts off — exactly the state a bare-metal
   `start.S` wants. This is what the Circle framework relies on under mt32-pi,
   and it is the path of least surprise. (It is **not** enough on its own: see
   § 4.4 for the mode you land in.)

   **[B] Done, end to end.** `emu/`'s payload wrapped with the `mkimage` built
   in § 2.9:

   ```
   $ tools/mkimage -A arm -O linux -T kernel -C none \
       -a 0x40200000 -e 0x40200000 -n mt32-t113 \
       -d emu/build/mt32emu-bare.bin mt32emu-bare.uimg
   Image Name:   mt32-t113
   Image Type:   ARM Linux Kernel Image (uncompressed)
   Data Size:    210280 Bytes = 205.35 KiB = 0.20 MiB
   Load Address: 40200000
   Entry Point:  40200000
   ```

   and the 64-byte header it produced, decoded and both CRCs independently
   recomputed:

   ```
   ih_magic  0x27051956          ih_os     0x05  IH_OS_LINUX   (image.h:78)
   ih_hcrc   0x141b5f7c  ok      ih_arch   0x02  IH_ARCH_ARM   (image.h:117)
   ih_size   0x00033568          ih_type   0x02  IH_TYPE_KERNEL(image.h:191)
   ih_load   0x40200000          ih_comp   0x00  IH_COMP_NONE  (image.h:250)
   ih_ep     0x40200000          ih_name   "mt32-t113"
   ih_dcrc   0xcaa06f61  ok      payload   210280 bytes, first word `b .+0x20`
   ```

   (`emu/` is another workstream's and is being built concurrently, so the byte
   count is of whatever `emu/build/mt32emu-bare.bin` happened to be at
   15:34 on 2026-09-18 — a later run in the same session wrapped a 114064-byte
   build of it with identical results. The header is the point, not the size.)

   Headroom: `CONFIG_SYS_BOOTM_LEN` is `0x800000` in our build, so the payload
   has 8 MiB to grow into before `bootm` starts refusing it. And **`bootm` on
   ARM32 refuses to start anything without a devicetree or ATAGS** — a plain
   `bootm <addr>` on this build prints `FDT and ATAGS support not compiled in`
   and resets. `bootm <addr> - ${fdtcontroladdr}` passes U-Boot's own control
   FDT and works; that is how `tools/handover-probe.sh` does it, and it is
   also why r2 in § 4.1 is a real pointer. Found by hitting it. **[B]**
2. **Use `go` and make the payload's first instructions disable the MMU and
   caches itself**, which is what SyterKit's `clean_syterkit_data()` does before
   chaining:
   ```c
   arm32_mmu_disable(); arm32_dcache_disable();
   arm32_icache_disable(); arm32_interrupt_disable();
   ```
   **[V — `vendor/syterkit/archive/boards/100ask-t113i/board.c`]**

Prefer (1). It costs one `mkimage` invocation and removes a whole class of
bring-up bug.

### 4.2 Addresses [V + I]

- DRAM base **0x40000000**. **[V]** — `SDRAM_BASE` in awboot, `memory@40000000`
  in every T113 DTS, `CFG_SYS_SDRAM_BASE` in U-Boot.
- U-Boot proper relocates itself to the **top** of DRAM, so load the payload low
  and leave the top ~16 MB alone. **[I]** On a 512 MB part, loading at
  0x40200000 (2 MB in) or 0x41000000 (16 MB in) is conventional and safe.
- FEL-mode SRAM staging is at **0x28000**, with ~100 KB usable. **[V]**

### 4.3 Which core, and starting the second one [V]

Reset releases **CPU0 only**; CPU1 is held in reset with its power domain off.
The A7 pair is a Cortex-A7 MPCore, GIC-400 at **0x03020000**
(`CONFIG_ARM_GIC_BASE_ADDRESS` in awboot; `compatible = "arm,gic-400"`,
`allwinner,irq-count = <223>` in SyterKit's T113 DTS). Architected timer runs at
**24 MHz**. **[V]**

To start CPU1, awboot has a complete PSCI implementation for this SoC
(`arch/arm32/mach-t113s3/psci.c`, `psci_board.c`, `psci-common.c`,
`CONFIG_ARMV7_PSCI_NR_CPUS 2`). The `CPU_ON` sequence is:

```c
int __secure psci_cpu_on(u32 unused, u32 mpidr, u32 pc, u32 context_id)
{
        u32 cpu = (mpidr & 0x3);
        psci_save(cpu, pc, context_id);
        sunxi_cpu_set_entry(cpu, &psci_cpu_entry);  /* R_CPUCFG + 0x1c8 */
        sunxi_cpu_set_reset(cpu, true);
        sunxi_cpu_invalidate_cache(cpu);
        sunxi_cpu_set_locking(cpu, true);           /* disable ext debug */
        sunxi_cpu_set_power(cpu, true);
        sunxi_cpu_set_reset(cpu, false);
        sunxi_cpu_set_locking(cpu, false);
        return ARM_PSCI_RET_SUCCESS;
}
```
plus `enable_smp_via_cp15()` setting ACTLR.SMP (bit 6) on the boot core.
Soft-entry register is `R_CPUCFG_BASE (0x07000400) + 0x1c8`; CPUCFG is at
`0x09010000`. **[V]**

**[I] For us:** that is ~200 lines to lift into the payload if we ever want
CPU1, and it is GPL-2.0+ code we can read. The plan's render loop is
single-threaded on one A7 for now; the honest read is that a second core is a
*later* optimisation, not day-one work. Note also that U-Boot's sunxi SPL sets
SMPEN itself (`SPL_ARMV7_SET_CORTEX_SMPEN` is selected by `MACH_SUN8I_R528`),
so if U-Boot is the stage below us, half the job is already done. **[V for the
Kconfig select.]**

### 4.4 Exception level and security state — MEASURED, and the first pass was wrong [X + B]

This is ARMv7-A, so there are no EL0-3; the question is **secure vs non-secure,
and SVC vs Hyp**.

**What the first pass said:** *"[I] By default, unless U-Boot is configured to
drop to non-secure/Hyp for a kernel, `go`/`bootelf` hands over in secure PL1
(SVC). ... If we later use `bootm` with `CONFIG_ARMV7_NONSEC`, we would land in
non-secure Hyp or SVC instead, which would break naive GIC setup."*

**What is true:** the "if we later use" is not hypothetical. It is the default,
and it is what a build from this repository's own defconfig does.

```
$ grep -nE "ARMV7_NONSEC|ARMV7_VIRT|ARMV7_BOOT_SEC_DEFAULT|ARMV7_PSCI=" .config
283:CONFIG_ARMV7_NONSEC=y
287:CONFIG_ARMV7_VIRT=y
288:CONFIG_ARMV7_PSCI=y
    # CONFIG_ARMV7_BOOT_SEC_DEFAULT is not set
```

`ARMV7_NONSEC` and `ARMV7_VIRT` are `default y` in
`arch/arm/cpu/armv7/Kconfig:15` and `:76`, gated only on
`CPU_V7_HAS_NONSEC`/`CPU_V7_HAS_VIRT`, which `MACH_SUN8I_R528` selects.
`ARMV7_BOOT_SEC_DEFAULT` is `default y if ARCH_TEGRA` — i.e. **n for us**. And
`armv7_boot_nonsec()` (`arch/arm/lib/bootm.c:213-225`) returns
`armv7_boot_nonsec_default()`, which is `true` unless `ARMV7_BOOT_SEC_DEFAULT`,
overridable at run time by `setenv bootm_boot_mode sec|nonsec`. **[V]**

So `boot_jump_linux()` takes this branch (`arch/arm/lib/bootm.c:351-357`):

```c
if (armv7_boot_nonsec()) {
        secure_ram_addr(_do_nonsec_entry)(kernel_entry, 0, machid, r2);
} else {
        kernel_entry(0, machid, r2);
}
```

`_do_nonsec_entry` is `smc #0` (`arch/arm/cpu/armv7/nonsec_virt.S:106-112`),
and `_secure_monitor` (`:44-105`) sets SCR.NS, and then:

```asm
        mov     r6, #SVC_MODE               @ default mode is SVC
        is_cpu_virt_capable r4
#ifdef CONFIG_ARMV7_VIRT
        orreq   r5, r5, #0x100              @ allow HVC instruction
        moveq   r6, #HYP_MODE               @ Enter the kernel as HYP
        mrseq   r3, sp_svc
        msreq   sp_hyp, r3                  @ migrate SP
#endif
        ...
        movs    pc, lr                      @ ERET to non-secure
```

A Cortex-A7 **is** virtualisation-capable, so `r6 = HYP_MODE`. And the probe
agrees: `cpsr = 0x600001da`, mode field `0x1a` = **Hyp**. **[B]**

**Why this is not academic.** Both existing implementations of the reset path
in this repository assume a PL1 mode and try to reach SVC with
`msr cpsr_c` — and the ARM ARM is explicit that a `CPS` or `MSR` that attempts
to change *out of* Hyp mode is **ignored**, so on the real handover neither of
them leaves Hyp and neither of them notices.

`port/t113/src/start.S` is the one that matters most, because it is the T113
implementation: its header says *"ENTRY CONTRACT. U-Boot `bootm` ... Secure or
non-secure is NOT part of that contract and is the one thing"* it cannot know,
and then line 87 comments *"3. Known CPSR: SVC, interrupts masked"* and lines
89-92 do the `mrs`/`bic`/`orr #0x13`/`msr cpsr_c` dance. It is right that
secure-vs-non-secure is not the contract; the mode is, and it is not SVC.
**This is workstream C's hand-off to whoever owns `port/` and `emu/`, and it
is the single most actionable thing in this document.**

`emu/src/start.S` has the same shape. It opens with
*"MMU off, D-cache off and flushed, I-cache state unspecified, IRQ and FIQ
masked, ARM state, PL1, SVC mode, (inferred) secure"*. The consequence in
either file is the same: the mode-field write is ignored, the core stays in
Hyp, the following `mcr p15,0,r0,c12,c0,0` writes VBAR while exceptions vector
through HVBAR, and the five per-mode stacks that get set up are all the same
Hyp stack. Nothing faults; nothing works either. That is the worst shape of
bug, and it would have been the first thing seen on a real board.

Neither file is wrong to *assume* something — both say what they assume, in
their headers, which is exactly why this was findable. What was wrong is the
thing they were told to assume, and that came from this document.

**Three ways out, in order of preference.**

1. **`CONFIG_ARMV7_BOOT_SEC_DEFAULT=y` in the defconfig.** Done — it is in
   `configs/t113i_mt32_defconfig` now, with the reasoning inline. Measured
   result: `cpsr = 0x600001d3` (SVC), `sctlr = 0x00c50878` (MMU, D-cache and
   I-cache all off). That is exactly the state `start.S` documents. **[B]**
2. **`setenv bootm_boot_mode sec`** in the boot environment, which reaches the
   same code path without a rebuild. Also measured, same numbers — the two
   rows in § 4.1 labelled `bootm_boot_mode=sec` are this.
3. **Teach the payload to leave Hyp**, the way Linux's `head.S` and Circle do:
   detect Hyp, set `SPSR_hyp` to SVC and `ERET`. About fifteen instructions.
   This belongs to whoever owns `emu/` and `port/`; workstream C's job is to
   flag it, which this is.

**The cost of option 1, stated honestly.** Going secure gives up something
real: with `ARMV7_NONSEC` + `ARMV7_PSCI`, U-Boot leaves its **PSCI secure
monitor installed** at MVBAR, so a non-secure payload can start CPU1 with a
plain `smc` PSCI `CPU_ON` and never write the R_CPUCFG sequence in § 4.3 at
all. Taking option 1 hands that job back to us.

That is not hypothetical either: `emu/src/smp.c` already starts CPU1 with PSCI
`CPU_ON` (function ID `0x84000003`) over an `hvc` or `smc` conduit, and it
calls `PSCI_VERSION` first specifically so it can report NOT IMPLEMENTED
instead of hanging. Under QEMU `-M virt` the conduit is `hvc` and QEMU itself
is the implementation. On the T113 there is no firmware underneath, so the
**only** PSCI implementation available would be the one U-Boot leaves behind
— and option 1 removes it, at which point `emu/`'s `smc` conduit has nothing
to answer it and `PSCI_VERSION` returns NOT IMPLEMENTED. The render loop is
single-threaded, so this costs nothing today. If CPU1 is ever wanted on
silicon, the trade is: **option 3 (payload leaves Hyp itself) keeps both the
SVC entry state and the PSCI monitor**, and is the right answer at that point.
**[V for the mechanism, I for what happens on the T113 — untested.]**

`CONFIG_HAS_ARMV7_SECURE_BASE` is **not** set for `MACH_SUN8I_R528`
(`arch/arm/cpu/armv7/Kconfig:32-33` lists SUN6I/SUN7I/SUN8I but the R528 is
its own symbol), so U-Boot's secure section is not relocated into SRAM on this
part — it stays in DRAM where U-Boot put it. Anything we load over the top of
U-Boot's relocation area therefore destroys the PSCI monitor. Another reason
option 1 is the tidy choice for now. **[V]**

## 5. RTOS options: what actually exists for this silicon

The plan asks for driver paths, not ecosystems. Here they are.

### 5.1 RT-Thread + Allwinner's `sunxi-hal` — the clear winner on coverage [V]

RT-Thread's tree carries `bsp/allwinner/libraries/sunxi-hal`, which is
Allwinner's own Melis/Tina HAL vendored in. It has **per-SoC files for
`sun8iw20`, which *is* the T113/D1 die**:

| Function | Path under `bsp/allwinner/libraries/sunxi-hal/hal/source/` |
|---|---|
| **I²S / TDM ("daudio")** | `sound/platform/sunxi-daudio.c` + `sound/platform/platforms/daudio-sun8iw20.h` |
| I²S register base | `daudio-sun8iw20.h`: `#define SUNXI_DAUDIO_BASE (0x02032000)`, `DAUDIO_NUM_MAX 3` |
| **PCM/DMA ring** | `sound/platform/sunxi-pcm.c`, `sound/core/snd_dma.c`, `snd_pcm.c` |
| On-chip audio codec | `sound/codecs/sun8iw20-codec.c` |
| ALSA-shaped API | `sound/component/aw-alsa-lib/` (pcm.c, pcm_rate.c, pcm_softvol.c, control.c, a NEON resampler) |
| S/PDIF, DMIC | `sound/platform/sunxi-spdif.c`, `sunxi-dmic.c`, `platforms/spdif-sun8iw20.h`, `dmic-sun8iw20.h` |
| Clocks | `ccmu/sunxi-ng/ccu-sun8iw20.c`, `ccu-sun8iw20-r.c`, `ccu-sun8iw20-rtc.c` |
| GPIO/pinctrl | `gpio/sun8iw20/gpio-sun8iw20.c` |
| Interrupt controller | `intc/platform/intc-sun8iw20.h` + `intc.c`, `hal_intc.c` |
| DMA | `dma/` + `platform-dma.h` (has sun8iw20) |
| SD/MMC | `sdmmc/` — `hal_sdhost.c`, `sd.c`, `mmc.c`, `core.c`, `sdio.c`, with **RT-Thread OSAL bindings already written** (`sdmmc/osal/os/RT-Thread/*`) |
| UART, TWI, SPI, PWM, RTC, watchdog, thermal, efuse, USB | all present with `sun8iw20` platform headers |

**[V — all read from the checkout at `vendor/rt-thread`.]**

The `sunxi-daudio.c` driver has real DMA plumbing
(`playback_dma_param.src_maxburst`, `dma_addr`, `hal_dma.h`), which is exactly
the I²S-DMA-ring piece workstream D needs.

FAT comes from RT-Thread proper (DFS + elm-chan FatFs), not from the HAL. **[V
by absence — there is no filesystem in `sunxi-hal`.]**

**Two real caveats, and they are not small.**

1. **There is no ARM Cortex-A7 T113 BSP.** The in-tree boards are
   `bsp/allwinner/d1` and `bsp/allwinner/d1s`, both **RISC-V** RT-Smart. The HAL
   is SoC-specific but largely architecture-neutral C; what is *not* neutral is
   the interrupt controller (GIC-400 on the A7 side, PLIC on the C906 side), the
   startup code, the MMU setup and the context switch. **[V for the BSP list; I
   for the size of the port — call it "an A7 BSP shell around an existing HAL",
   which is real work but bounded, and RT-Thread already has ARMv7-A support and
   GIC drivers from other BSPs.]**
2. **Licence.** Every `sunxi-hal` file carries
   `Copyright (c) 2019-2025 Allwinner Technology Co., Ltd. ALL rights reserved.`
   followed by a warranty disclaimer and **no licence grant**, sitting inside an
   Apache-2.0 repository. **[V — quoted from `sunxi-daudio.c`.]** For a project
   that already has to be careful about LGPL-2.1 boundaries around `mt32emu`,
   this needs a decision before any of it is copied, not after.

**Melis** is Allwinner's own RTOS built on RT-Thread with this same HAL;
`DongshanPI/D1s-Melis` is the public D1s port. It is a *source* for drivers and
a documentation crib (mostly Chinese), not a platform to adopt.

### 5.2 FreeRTOS: `robots/allwinner_t113` [V]

Cloned and read. What it has, on real T113 silicon:

- FreeRTOS, **single core, no SMP** (author's own words).
- GICv2 interrupt controller — `common/arm/irq.c`.
- ARM architected timer — `common/arm/arm_timer.c`.
- MMU setup — `common/arm/mmu.c`.
- CCU clocks — `common/aw/ccu.c`. CPU at 1008 MHz.
- DMA controller — `common/aw/dmac.c`.
- GPIO — `common/aw/gpio.c`.
- **SD/MMC** — `common/aw/smhc.c`, plus FatFs (`diskio.c`, `ffconf.h`, `sdfs.c`).
- Display engine + LCD — `common/aw/de.c`, `tcon_lcd.c`, 800×480 working.
- USB OHCI via TinyUSB, described by the author as "a bit hackish".
- UART — `common/aw/uart.c`, **blocking, no DMA or interrupts**.
- TWI — "not working/finished".
- It runs Doom.

**What it does not have: any I²S or audio driver at all.** Grepping the tree for
`i2s|audio|codec|dac|ahub` hits only Doom's sound stubs, liblzg and CMSIS
headers. **[V]**

Two more things that matter:

- *"DDR3 is initialized by the proprietary code in xfel. There is no bootloader
  yet. Get uboot or something."* **[V — its README.]** So it gives us nothing on
  the external-DDR3 question, and it targets `AW_T113S2.ld` — a co-packaged-DRAM
  part.
- Last commit **May 2024**. Licence is generous: the author's own files are
  public domain; `start.S` and parts of `irq.c` are from xboot; TinyUSB MIT,
  FreeRTOS MIT, TLSF BSD, CMSIS Apache-2.0. **[V]**

**Verdict: an excellent reference for GIC, timer, MMU and SMHC on this exact
SoC, under a licence we can actually use. Not a BSP. You would write the I²S
driver.**

### 5.3 Zephyr [V]

`soc/allwinner/` in Zephyr main contains exactly two entries: **`sun8i_h3`** and
**`sun50i_h618`**. No `sun8iw20`, no `sun20i`, no T113, no D1, no R528. **[V]**

Zephyr does support ARMv7-A / Cortex-A (the `cortex_a_r` architecture), and the
H3 is itself a Cortex-A7 Allwinner part, so the precedent exists. But there is
no T113 SoC definition, no board, and no driver. **[V for the tree contents; I
for the effort — this would be a from-scratch SoC port including clocks,
pinctrl, GIC wiring and I²S, with the added cost of doing it Zephyr's way.]**

**Verdict: do not plan on it**, as the parent document already concluded.

### 5.4 Summary

| | I²S + DMA | SD/MMC | FAT | GIC | Timer | UART | Ported to T113 A7? | Licence |
|---|---|---|---|---|---|---|---|---|
| RT-Thread + `sunxi-hal` | **yes, `sunxi-daudio.c` for sun8iw20** | yes, with RT-Thread OSAL | via RT-Thread DFS | yes (HAL + other BSPs) | yes | yes | **no ARM BSP — RISC-V only** | Apache-2.0 repo, **Allwinner "all rights reserved" files** |
| FreeRTOS `robots/allwinner_t113` | **no** | yes | yes (FatFs) | yes | yes | yes, blocking | **yes, running** | public domain + MIT/BSD |
| Zephyr | no | no | yes (generic) | yes (generic) | yes | yes | **no SoC at all** | Apache-2.0 |
| Bare metal on awboot/U-Boot | write it | write it | write it (FatFs) | write it | write it | write it | n/a | ours |

**The honest read.** Neither RTOS is a drop-in. RT-Thread has *by far* the most
real driver code for this exact SoC — and critically, the only I²S-with-DMA
driver that exists for it — but it is behind an ARM BSP that does not exist and
a licence that has not been cleared. FreeRTOS has a *working* T113 A7 port with
the awkward parts (GIC, MMU, timer, SD) already done, under a clean licence, and
is missing precisely the one driver we most need.

**[I] The pragmatic shape:** take the FreeRTOS port's GIC/timer/MMU/SMHC
scaffolding (or write the equivalent bare-metal), and write one I²S+DMA driver
against the T113 user manual, using RT-Thread's `sunxi-daudio.c` as
documentation for a register set Allwinner does not publish well — reading it
for understanding rather than copying it, which sidesteps the licence question.
One I²S driver is days, not weeks; the parent document's "weekend-scale driver
list" is optimistic but the right order of magnitude, and this is the
concrete reason it holds: everything *except* I²S already exists somewhere we
can read.

---

## 6. Toolchain and development loop

### 6.1 Compiler [V]

Cortex-A7, NEON-VFPv4, hard float. awboot's flags:

```
-mcpu=cortex-a7 -mthumb-interwork -mthumb -mno-unaligned-access \
-mfpu=neon-vfpv4 -mfloat-abi=hard
```
**[V — awboot Makefile.]**

- Bootloader / bare metal: `arm-none-eabi-` (awboot's default).
- U-Boot and anything wanting a libc: `arm-linux-gnueabihf-`.
- **Match `bench/`.** Workstream A is already cross-building `mt32emu` for
  `armv7-a` NEON hard-float; the payload must use the same ABI and the same
  `-mfpu`, or the C++ objects will not link cleanly against the platform layer.
  Drop `-mthumb` for the synth — Munt is FP-heavy and ARM mode is the safer
  default until measured.

### 6.2 Serial console [V + X]

The T113 has multiple UARTs and **every board puts the console somewhere
different**, which is a genuine trap:

| Board | Console | Pins | Source |
|---|---|---|---|
| MangoPi MQ-R (T113-S3) | UART3 (`CONS_INDEX=4`) | — | `configs/mangopi_mq_r_defconfig` **[V]** |
| 100ask T113-i devkit | UART0 @ 0x02500000 | PB8/PB9, mux 6 | SyterKit `100ask-t113i/board.dts` **[V]** |
| 100ask T113-S3 | UART3 @ 0x02500c00 | PB6/PB7, mux 7 | SyterKit `100ask-t113s3/board.dts` **[V]** |
| Tronlong T113i MiniEVM | UART0 | PG17/PG18 | taterli fork, `uart0_pg_pins` + a `CONFIG_UART0_PORT_PG` symbol that **only exists in that fork** **[V]** |

3.3 V TTL, 115200 8N1. **[I]** Get a CH340 or FT232 adapter and check the
board's schematic before assuming.

**[X] And the table above is not the whole story, in a way that will bite.**
Mainline's **SPL does not take its console pinmux from the devicetree.** It is
a C preprocessor ladder in `arch/arm/mach-sunxi/board.c`, and for
`MACH_SUN8I_R528` it has exactly two arms:

```c
#elif CONFIG_CONS_INDEX == 1 && defined(CONFIG_MACH_SUN8I_R528)     /* :160 */
        sunxi_gpio_set_cfgpin(SUNXI_GPE(2), 6);       /* PE2 */
        sunxi_gpio_set_cfgpin(SUNXI_GPE(3), 6);       /* PE3 */
        sunxi_gpio_set_pull(SUNXI_GPE(3), SUNXI_GPIO_PULL_UP);
...
#elif CONFIG_CONS_INDEX == 4 && defined(CONFIG_MACH_SUN8I_R528)     /* :181 */
        sunxi_gpio_set_cfgpin(SUNXI_GPB(6), 7);       /* PB6 */
        sunxi_gpio_set_cfgpin(SUNXI_GPB(7), 7);       /* PB7 */
        sunxi_gpio_set_pull(SUNXI_GPB(7), SUNXI_GPIO_PULL_UP);
...
#else
#error Unsupported console port number. Please fix pin mux settings in board.c
```
**[V]**

So the only two console pinouts mainline supports without a patch are
**UART0 on PE2/PE3** and **UART3 on PB6/PB7**. U-Boot *proper* is different —
it is DM-based and takes the pins from the DT, and the pinctrl driver's
function table allows `uart0` (mux 6) on PB0-PB1, PB8-PB9 **and** PE2-PE3
(`drivers/pinctrl/sunxi/pinctrl-sunxi.c:601-619`). The failure mode on a board
like the 100ask T113-i, which puts UART0 on **PB8/PB9**, is therefore:
**the SPL is silent and U-Boot proper talks.** The first output you ever see
from that board is the second-stage banner, and every DRAM debug print the SPL
made is lost — which is precisely the output you want when DRAM does not come
up.

Independent corroboration that this is a real trap, not a theory: taterli's
fork carries a commit whose entire subject is *"sunxi: add UART0 PG17/PG18
pinmux support for T113-i"*, and whose message says DM pinctrl reconfiguring
the pins made the debug UART *"失声"* — go silent — on boards that route UART0
to PG17/PG18. **[V]**

**Hand-off to workstream B:** put the console on **UART0 PE2/PE3**
(`CONS_INDEX=1`) or **UART3 PB6/PB7** (`CONS_INDEX=4`). Anything else costs a
patch to mainline `board.c` that we then carry forever.
`configs/t113i_mt32_defconfig` and `dts/sun8i-t113i-mt32.dts` both pick
PE2/PE3, and both say why.

Note that the MIDI-in UART for the synth is a *separate* problem — 31 250 baud,
which is `24 MHz / 768`; the sunxi UART divisor arithmetic works out exactly, so
no fractional-baud gymnastics needed. **[I — arithmetic, not read from source.]**

### 6.3 Iteration loop, fastest first [V]

**1. xfel — no SD swapping, seconds per cycle. The one to use.**
```sh
# board in FEL mode (no SD inserted, or FEL button held)
scripts/fel-ddr-t113i.sh build/payload.bin
```
That brings up external DDR3 with our parameters, loads the payload to
0x40000000 and runs it. Nothing is written to any flash. This is how the
FreeRTOS T113 port is developed (its README says so outright), and it is how
xboot documents its own T113 flow. **[V]**

**2. U-Boot + TFTP.** Once U-Boot is on an SD card, leave it there and iterate
over Ethernet or serial:
```
=> setenv serverip 192.168.1.10; tftpboot 0x41000000 payload.uimg; bootm 0x41000000
```
Slower to set up, but a useful second loop once there's a board with a PHY.

**3. SD swap.** The fallback. `dd if=u-boot-sunxi-with-spl.bin of=/dev/sdX bs=1k
seek=8`, then copy the payload onto a FAT32 partition. Slow, and SD card
sockets wear out.

**4. JTAG.** The T113 has JTAG and both SyterKit
(`scripts/openocd/allwinner_t113.cfg`) and the FreeRTOS port (`openocd.cfg` in
each firmware dir) ship OpenOCD configs. **[V]** `xfel jtag` also exists.
Worth wiring the pads on our board even if we never use them.

### 6.4 QEMU: no T113 — but more than "no" [X + B]

The first pass said "QEMU: no", full stop. That is right about the T113 and
wrong about the sunxi boot path, and the difference produced this document's
best evidence.

**There is no T113, D1, R528 or sun8iw20 machine, and there never will be.**
Three Allwinner models exist, confirmed from the binary rather than from the
source tree: **[B]**

```
$ qemu-system-arm -M help | grep -iE 'allwinner|cubie|orangepi|bananapi'
bpim2u               Bananapi M2U (Cortex-A7)
cubieboard           cubietech cubieboard (Cortex-A8)
orangepi-pc          Orange Pi PC (Cortex-A7)
$ qemu-system-arm --version | head -1
QEMU emulator version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)
```

A10, H3 and R40. No sun20i family member, and nothing about DRAM init, the
DDR PHY, I2S, the DMAC, SMHC or the T113's GIC wiring is emulated anywhere.

**What does run, and it is a lot more than nothing.** `orangepi-pc` models an
Allwinner **H3** — a Cortex-A7 sunxi part that mainline U-Boot supports with
the *same* `arch/arm/mach-sunxi` code, the *same* eGON image format, the
*same* SD sector-16 boot offset and the *same* SPL→`u-boot.img` handoff as
`MACH_SUN8I_R528`. Build it and `dd` it into a disk image and the whole flow
runs: **[B]**

```
$ tools/build-uboot.sh orangepi_pc
$ dd if=/dev/zero of=sd.img bs=1M count=64
$ dd if=u-boot-sunxi-with-spl.bin of=sd.img bs=1024 seek=8 conv=notrunc
$ qemu-system-arm -M orangepi-pc -nographic -drive file=sd.img,if=sd,format=raw

U-Boot SPL 2026.10-rc4-00034-g211de43d0f95 (Sep 18 2026 - 15:37:13 +0000)
DRAM: 1024 MiB
Failed to set core voltage! Can't set CPU frequency
Trying to boot from MMC1

U-Boot 2026.10-rc4-00034-g211de43d0f95 Allwinner Technology
CPU:   Allwinner H3 (SUN8I 0000)
Model: Xunlong Orange Pi PC
DRAM:  1 GiB
=>
```

So the parts of § 1 that are *format and flow* rather than *silicon* are
exercisable today: the eGON header the BROM parses, the 8 KiB SD offset, the
SPL loading `u-boot.img` from raw sectors, the environment, `mmc read`,
`mkimage`, and — crucially — **`bootm` and `go`**.

That last one is why § 4.1 and § 4.4 have measurements in them. The handover
code under test is `arch/arm/lib/bootm.c`, `arch/arm/cpu/armv7/cpu.c` and
`arch/arm/cpu/armv7/nonsec_virt.S` — **ARMv7 architecture code with no SoC
dependency at all** — and `orangepi_pc_defconfig` reaches it with the same
switches our defconfig does (`ARMV7_NONSEC=y`, `ARMV7_VIRT=y`,
`ARMV7_BOOT_SEC_DEFAULT` unset, `CPU_V7A=y`). `tools/handover-probe.sh` is
the harness; its SoC-specific content is four lines of UART address.

**What it does not tell you, and the list has not got shorter.** Nothing about
DRAM init on external DDR3, the AC remapping table, the PHY, clocks, pinmux,
I2S, the DMAC, SMHC, the GIC-400's Allwinner wiring, or any timing whatsoever
— QEMU's TCG models neither the A7 pipeline nor its caches. A green QEMU run
must not create confidence about the hard parts. It did, however, prove that
the single most load-bearing inference in the first pass was wrong, which is
exactly what emulation above the seam is for (`docs/PLAN.md` § 0.5).

### 6.5 xfel without a board: no [B]

Asked and answered honestly. xfel builds and runs here:

```
$ cd vendor/xfel && make && ./xfel version
ERROR: No FEL device found!
$ echo $?
255
```

It needs a USB device at VID:PID `1f3a:efe8`. This container has no
`/dev/bus/usb` at all, so there is nothing to attach even a gadget or `usbip`
endpoint to, and xfel's FEL protocol is a request/response conversation with
real BROM code — there is no software implementation of the other end
anywhere, in xfel or elsewhere. **The FEL path cannot be exercised without
silicon.** What *can* be checked is everything that happens before the USB
write, which is what `DRY_RUN=1 scripts/fel-ddr-t113i.sh` does, and what
`scripts/ddrpara.py selftest` does to the payload it uploads.

## 7. Recommended path

Let **mainline U-Boot v2024.01 or later** do silicon bring-up: it has the
D1/R528/T113 DRAM driver in GPL source, and the external-DDR3 delta is four
Kconfig lines (`ODT_EN`, `TPR11`, `TPR12`, `TPR13`) that two published T113-i
boards already give us working values for, because the driver auto-detects
rank, DQ width and density and computes the DDR3 JEDEC timings from
`DRAM_CLK`. Build `u-boot-sunxi-with-spl.bin` with `tools/build-uboot.sh` — it does the
clone, the defconfig, our board DTS and the build in one command, and it has
been run — dd the 490824-byte result to an SD card at 8 KB, and have U-Boot
load our payload from FAT as a `mkimage`-wrapped `-O linux -T kernel` `bootm`
image so that `cleanup_before_linux()` hands it a clean CPU: MMU off, D-cache
off, I-cache off, interrupts masked, CPU1 still parked. **Set
`CONFIG_ARMV7_BOOT_SEC_DEFAULT=y`, or `bootm` arrives in non-secure Hyp
instead of secure SVC and the payload's `start.S` silently does nothing**
(§ 4.4 — measured, not guessed). **Put the console on UART0 PE2/PE3 or UART3
PB6/PB7**, because those are the only two pin pairs mainline's SPL knows how
to mux on this part (§ 6.2). Above that, write bare metal rather than
adopting an RTOS on day one: read the FreeRTOS `robots/allwinner_t113` port for
GIC-400, architected-timer, MMU and SMHC on this exact silicon (public domain,
already running), read RT-Thread's `sunxi-daudio.c` for the I²S register set
(the only I²S driver that exists for sun8iw20, but under an Allwinner "all
rights reserved" header, so read it, do not paste it), and write the one driver
nobody has: an I²S DMA ring. Develop the whole thing over **xfel**, never
touching an SD card — `scripts/fel-ddr-t113i.sh` brings up external DDR3 with
our parameters and runs a payload from DRAM in seconds. Before the PCB is laid
out, settle the AC-remapping question (section 2.5): the DDR3 address/command
routing on our board must match the remapping table we program in `TPR13`, and
that is a schematic decision, not a software tune.

### First three commands on a T113 dev board

```sh
# 1. Is it alive, is it the part we think it is, and WHICH AC REMAPPING TABLE
#    does its efuse ask for?  Board in FEL mode (no SD card inserted),
#    USB-OTG to the host.  The read32 is the one command that closes the
#    question in section 2.5, and it costs nothing.
xfel version && xfel sid
xfel read32 0x03006228        # AC remapping efuse, bits [11:8]
./scripts/ddrpara.py acremap --preset mt32-t113 --fuse <that nibble>

# 2. Bring up DRAM and prove it is really there.
#    On a T113-S3 dev board this is enough:
xfel ddr t113-s3 && xfel hexdump 0x40000000 0x40
#    On a T113-i board with external DDR3, use ours instead:
#    ./scripts/fel-ddr-t113i.sh

# 3. Build mainline U-Boot and run it from FEL - no SD card, nothing written
#    to the board.  Both of these builds have been done here; only the last
#    line has not.
./tools/build-uboot.sh mangopi_mq_r       # the in-tree T113-S3 board
./tools/build-uboot.sh                    # ours, external DDR3
sunxi-fel uboot vendor/build/mangopi_mq_r/u-boot-sunxi-with-spl.bin
```

If step 3 prints a U-Boot banner, every hard part of section 1 and section 2 is
proven on that board, and the remaining work is our own code.

---

## What is still unknown

Renumbered and re-scoped after the second pass. Items 3 and 5 are **closed**.

1. **Which AC remapping table our own DDR3 routing needs** (§ 2.5). Still the
   item that gates the PCB, but it is better understood than it was: table 0
   is now *derived* to be straight-through rather than assumed, all eight
   tables are a one-line patch apart, and a wrong choice fails loudly at
   training rather than subtly. The remaining unknowns are (a) which of the 22
   lines is which — mainline's own comment says *"it is unclear"* — and (b)
   what the T113-i's efuse and chip ID actually read. Both are one `xfel`
   session away, and `scripts/fel-ddr-t113i.sh` now asks for them first.
   **Still the item to send to workstream B today.**
2. **`dram_mr1` is not configurable** (§ 2.3). Unchanged. If our DDR3 part or
   topology wants termination other than Rtt_Nom = RZQ/2 with a 34 Ω output
   driver, that is a one-line driver patch that nobody has needed and
   therefore nobody has tested. Note the same is true of `dram_para1` and
   `dram_para2`, which the second pass found are **hardcoded in U-Boot**
   (0x10d2 and 0) and not Kconfig at all.
3. ~~**Exception level and security state on handoff.** Inferred as secure
   PL1; a 10-line payload on real hardware settles it.~~ **CLOSED, and the
   inference was wrong.** `bootm` hands over in **non-secure Hyp** with
   mainline's defaults; measured, not inferred (§ 4.4). The defconfig now sets
   `CONFIG_ARMV7_BOOT_SEC_DEFAULT=y` to get secure SVC instead. What remains
   is only to re-run `tools/handover-probe`'s equivalent on the real board,
   which `emu/src/start.S` already does for free — it records CPSR, SCTLR,
   ACTLR, VBAR, MIDR, ID_PFR1, CNTFRQ and r0/r1/r2 at entry and prints them.
4. **The RT-Thread `sunxi-hal` licence** (§ 5.1). Unchanged. Allwinner
   copyright with no grant, inside an Apache-2.0 repo. Needs a decision.
5. ~~**Whether the taterli T113-i U-Boot fork actually boots.**~~ **Partly
   closed.** Its `# CONFIG_SPL is not set` is confirmed by reading the file,
   and so is the reason it needs a fork at all: its single commit,
   *"sunxi: add UART0 PG17/PG18 pinmux support for T113-i"*, exists because
   mainline's SPL pinmux ladder has no arm for those pins (§ 6.2). That fork
   is evidence about **DRAM parameters and about the console trap**, and still
   not evidence about a complete SD-boot flow. Note its defconfig *does* set
   `CONFIG_DRAM_SUNXI_TPR0`, which is what ours was missing.
6. **New: whether our defconfig's numbers are right for our copper.** The
   build is real; the parameters are the 100ask T113-i's, on the 100ask
   T113-i's layout. `tpr11` and `tpr12` are byte-lane delay trim, i.e. trace
   length, and they will need sweeping on our PCB. That is a bring-up task,
   not a research one.
7. **New: the T113-i's SoC chip ID.** `mctl_phy_ac_remapping()` skips
   remapping entirely for chip ID `0x7200` (the T113-S4). Mainline knows four
   chip IDs and the T113-i is not among them (`dram_sun20i_d1.h:26-31`).
   `xfel sid` answers it.

## Network, and what did not work

**Second pass, 2026-09-18.** github.com and general HTTPS worked, which is why
there is a U-Boot checkout at all. What did not work, recorded per the house
rule:

- **`xfel version` — "No FEL device found!"** There is no USB anywhere in this
  container (`/dev/bus/usb` does not exist), and no software implementation of
  the BROM's end of the FEL protocol exists to talk to. § 6.5.
- **The first attempt to build `configs/t113i_mt32_defconfig` hung**, twice,
  because `make syncconfig` was prompting for `DRAM_SUNXI_TPR0` on a pipe. The
  second attempt failed instead, because `</dev/null`. Both are in § 2.3.
- **`bootm <addr>` with no devicetree** prints `FDT and ATAGS support not
  compiled in` and resets. The probe only ran once `${fdtcontroladdr}` was
  passed as the third argument. § 4.1.
- **`mmc read 0x42000000 2048 2` read block 8264**, because U-Boot parses that
  argument as hexadecimal. Ten minutes lost; `tools/handover-probe.sh` now
  carries both spellings of the number and a comment saying why.
- **Seven host packages** were missing and each one stopped the build in turn:
  `flex`, `bison`, `swig`, `python3-dev`, `libssl-dev`, `libgnutls28-dev`,
  `uuid-dev`. `tools/build-uboot.sh` checks for three of them up front and
  documents all seven.
- **LCSC, JLCPCB, Forlinx, 100ask, whycan and the Allwinner vendor sites are
  blocked** and were not attempted this pass; nothing in this document needed
  them, because everything came out of source.

### Sites blocked by the egress proxy, first pass

Named per the brief:

- **linux-sunxi.org** — blocked (`EGRESS_BLOCKED`) by both WebFetch and curl.
  This is the community wiki and the canonical reference for the eGON header,
  FEL, the T113-S3 page and the boot-media order. Everything that would have
  come from there was instead read from U-Boot, awboot, SyterKit and xfel
  source, which is a better source anyway.
- **lists.denx.de** — blocked. The U-Boot patch series that added the
  D1/R528/T113 DRAM code could not be read; the code itself was read instead.
- **www.mail-archive.com** — blocked. Same patch series.

## Reference tree

Everything under `vendor/` is a read-only clone, gitignored, never committed.

**The exact commits this document's second pass was written against**, in the
style `bench/README.md` records Munt's:

| Tree | Commit | Date | Licence |
|---|---|---|---|
| **u-boot/u-boot** | `211de43d0f954a00a490220c1aac9db298287c40` (`v2026.10-rc4-34-g211de43d0f`) | 2026-09-17 | GPL-2.0+ |
| xboot/xfel | `445e8aefe6914c85817cc9bd1d201629364b0ec6` | 2026-09-14 | MIT |
| YuzukiHD/SyterKit | `f95d29d611ce439a2510957b0f312b5b439f53b8` | 2026-09-12 | GPL-2.0 |
| szemzoa/awboot | `5380c00fc67c975433f25c57fb481aa2b91aebf8` | 2025-11-21 | GPL-2.0+ |
| nickfox-taterli/t113-uboot | `9e607b81385c7228b12f4abcea26e464fbcd8155` | 2025-11-27 | GPL-2.0+ |

Every file:line citation in this document is against that U-Boot commit. If
`tools/build-uboot.sh` reports a different hash, the line numbers have moved
and the claims need re-checking — that is what the hash is for.

To recreate:

```sh
# U-Boot: tools/build-uboot.sh does this for you, and pins with REF=
git clone --filter=blob:none https://github.com/u-boot/u-boot.git vendor/u-boot
git -C vendor/u-boot checkout --detach 211de43d0f954a00a490220c1aac9db298287c40

cd vendor
git clone --depth 1 https://github.com/xboot/xfel.git xfel
git clone --depth 1 https://github.com/YuzukiHD/SyterKit.git syterkit
git clone --depth 1 https://github.com/szemzoa/awboot.git awboot
git clone --depth 1 --filter=blob:none https://github.com/nickfox-taterli/t113-uboot.git
git clone --depth 1 https://github.com/robots/allwinner_t113.git freertos-t113
git clone --depth 1 --filter=blob:none https://github.com/RT-Thread/rt-thread.git
git clone --depth 1 https://github.com/linux-sunxi/sunxi-tools.git
```

A full U-Boot clone with `--filter=blob:none` is **548 MB** on disk — a shallow
clone or `git sparse-checkout set drivers/ram arch/arm/mach-sunxi configs
arch/arm/dts doc/board/allwinner dts` is much smaller, but note that a sparse
checkout **will not build**; `tools/build-uboot.sh` needs the whole tree.
`scripts/ddrpara.py selftest` cross-checks against `vendor/xfel` and
`vendor/syterkit` if they are present and skips those checks cleanly if they
are not.

Host packages the U-Boot build needs beyond the cross toolchain, each one
found by hitting its error: `flex`, `bison`, `swig`, `python3-dev`,
`libssl-dev`, `libgnutls28-dev`, `uuid-dev`. **[B]**

The files that carry the load, in rough order of importance:

| File | Why |
|---|---|
| `u-boot/drivers/ram/sunxi/dram_sun20i_d1.c` | the DRAM driver, all of section 2 |
| `u-boot/drivers/ram/sunxi/Kconfig` | the seven knobs |
| `syterkit/archive/boards/100ask-t113i/board.dts` | **external-DDR3 T113-i parameters** |
| `syterkit/archive/boards/100ask-t113s3/board.dts` | the co-packaged set, for the diff |
| `t113-uboot/configs/t113i_minievm_defconfig` | the second T113-i data point |
| `xfel/chips/r528_t113.c` | FEL DDR payload + protocol |
| `u-boot/include/sunxi_image.h` | eGON header |
| `u-boot/doc/board/allwinner/sunxi.rst` | boot media, offsets, FEL |
| `awboot/arch/arm32/mach-t113s3/psci*.c` | second-core start |
| `freertos-t113/common/arm/*`, `common/aw/*` | GIC, timer, MMU, SMHC on this SoC |
| `sunxi-tools/soc_info.c` | the SRAM map and SID base, straight from the FEL tool |
| `rt-thread/bsp/allwinner/libraries/sunxi-hal/hal/source/sound/` | the only I²S driver for sun8iw20 |

## Links

- U-Boot: https://github.com/u-boot/u-boot — `drivers/ram/sunxi/dram_sun20i_d1.c`, `doc/board/allwinner/sunxi.rst`
- awboot: https://github.com/szemzoa/awboot
- SyterKit: https://github.com/YuzukiHD/SyterKit
- xboot: https://github.com/xboot/xboot — `docs/guide-allwinner-t113s3.md`
- xfel: https://github.com/xboot/xfel — MIT
- sunxi-tools: https://github.com/linux-sunxi/sunxi-tools
- T113-i U-Boot (Tronlong MiniEVM): https://github.com/nickfox-taterli/t113-uboot
- FreeRTOS on T113: https://github.com/robots/allwinner_t113
- RT-Thread: https://github.com/RT-Thread/rt-thread — `bsp/allwinner/`
- D1s-Melis: https://github.com/DongshanPI/D1s-Melis
- Forlinx OK113i-S / FET113i-S: https://www.forlinx.net/single-board-computer/t113i-s-sbc-144.html
- Zephyr Allwinner SoCs: https://github.com/zephyrproject-rtos/zephyr/tree/main/soc/allwinner
- QEMU ARM machines: https://www.qemu.org/docs/master/system/target-arm.html
- linux-sunxi wiki (blocked from here): https://linux-sunxi.org/T113-s3
