# port/t113/Makefile - the third implementation of the platform seam.
#
#   make              build/mt32-t113.elf (+ .bin, + .map) and libmtp_t113.a
#   make lib          the driver object set only, no main(), no link
#   make size         text/data/bss and the ten largest symbols
#   make uimage       wrap the .bin for U-Boot `bootm` (boot/BRINGUP.md 4.1)
#   make assumptions  list every MTP_T113_UNVERIFIED constant in the image
#   make T113_MMU=0   build the exact bootm handover state and never change it
#   make clean
#
# THERE IS NO `make run` AND NEVER WILL BE. boot/BRINGUP.md 6.4: QEMU has no
# T113, R528 or D1 machine model and is not going to acquire one. Compiling is
# the only verification available here, so the flags below are as strict as
# they can be made: -Wall -Wextra -Werror, freestanding, no libc assumed beyond
# what emu/ already establishes, and a link that fails on any undefined symbol.
#
# The link is the real test. It pulls in port/src/mtp_render.c and
# port/src/mtp_midi_parser.c -- the same objects the host harness and the QEMU
# image build -- against these drivers. If it links with nothing undefined,
# every function port/include declares has an implementation here, which is the
# strongest statement that can be made about a board nobody has.
#
# Toolchain: arm-linux-gnueabihf-gcc used as a bare-metal compiler
# (-nostdlib -ffreestanding), the same choice and for the same reasons as emu/;
# see emu/FINDINGS.md. There is no arm-none-eabi in this environment.
#
# SPDX-License-Identifier: 0BSD

CROSS   ?= arm-linux-gnueabihf-
CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
AR      := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size
NM      := $(CROSS)nm

PORT    := ..
BUILD   := build
TARGET  := $(BUILD)/mt32-t113
LIB     := $(BUILD)/libmtp_t113.a

T113_MMU ?= 1

# Not negotiable: boot/BRINGUP.md 6.1 and port/PORTING.md 4.2 both say
# Cortex-A7, NEON-VFPv4, hard float, ARM (not Thumb). bench/ cross-builds
# mt32emu with exactly these and the objects have to link against ours.
# -ffp-contract=off: neon-vfpv4 fuses float multiply-accumulate into VFMA (one
# rounding) where x86-64's SSE2 baseline does two, which makes this board's
# audio differ from the bench's by 1 LSB on the Analog.cpp FIR that every
# sample passes through. Bit-comparability across implementations is worth
# more than the fused form. PORTING.md 4.2.
ARCHFLAGS := -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -ffp-contract=off

# -Werror is the point of this build. Everything else is emu/'s flag set.
WARN    := -Wall -Wextra -Werror -Wshadow -Wundef -Wpointer-arith \
           -Wcast-align -Wstrict-prototypes -Wmissing-prototypes \
           -Wredundant-decls -Wno-unused-parameter

# -U_FORTIFY_SOURCE matters more than it looks: Ubuntu's GCC turns on
# _FORTIFY_SOURCE=2 at -O2, which rewrites printf/sprintf into glibc's
# __printf_chk family. Those symbols exist only in glibc and a bare-metal link
# then fails on them with no hint why. emu/FINDINGS.md found this the hard way.
COMMON  := $(ARCHFLAGS) -O2 -g -fno-builtin -fno-common \
           -ffunction-sections -fdata-sections -fno-stack-protector \
           -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
           -DT113_MMU=$(T113_MMU)
CFLAGS  := $(COMMON) $(WARN) -ffreestanding -std=gnu99
ASFLAGS := $(ARCHFLAGS) -g -DT113_MMU=$(T113_MMU)
INC     := -Iinclude -I$(PORT)/include

LIBM    := $(shell $(CC) $(ARCHFLAGS) -print-file-name=libm.a)
LIBGCC  := $(shell $(CC) $(ARCHFLAGS) -print-libgcc-file-name)
LDFLAGS := -nostdlib -nostartfiles -static -T link.ld \
           -Wl,--gc-sections -Wl,-Map,$(TARGET).map -Wl,--build-id=none \
           -Wl,--no-warn-rwx-segments -Wl,--no-undefined
LDLIBS  := $(LIBM) $(LIBGCC)

# The drivers: everything below the seam. This set is what `make lib` builds,
# and it is the answer to "what does a T113 port of port/include cost?".
DRV_C   := src/mmu.c src/gic.c src/ccu.c src/pio.c src/uart.c src/dmac.c \
           src/i2s.c src/smhc.c src/fat.c \
           src/t113_log.c src/t113_time.c src/t113_audio.c src/t113_midi.c \
           src/t113_storage.c src/printf.c src/retarget.c
DRV_ASM := src/start.S

# Above the seam: compiled from where they live, never copied.
PORT_C  := $(PORT)/src/mtp_midi_parser.c $(PORT)/src/mtp_render.c \
           $(PORT)/host/engine_fake.c

APP_C   := src/main.c

DRV_OBJ := $(addprefix $(BUILD)/,$(notdir $(DRV_ASM:.S=.o)) $(notdir $(DRV_C:.c=.o)))
PORT_OBJ:= $(addprefix $(BUILD)/,$(notdir $(PORT_C:.c=.o)))
APP_OBJ := $(addprefix $(BUILD)/,$(notdir $(APP_C:.c=.o)))
OBJ     := $(DRV_OBJ) $(PORT_OBJ) $(APP_OBJ)

.PHONY: all lib size dump clean uimage assumptions help

all: $(TARGET).elf $(TARGET).bin $(LIB)

lib: $(LIB)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(ASFLAGS) $(INC) -c -o $@ $<

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(BUILD)/%.o: $(PORT)/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(BUILD)/%.o: $(PORT)/host/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(LIB): $(DRV_OBJ)
	$(AR) rcs $@ $(DRV_OBJ)
	@$(SIZE) -t $(DRV_OBJ) | tail -1

$(TARGET).elf: $(OBJ) link.ld
	$(CC) $(ARCHFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
	@$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "raw image: $$(stat -c%s $@) bytes"

size: $(TARGET).elf
	@$(SIZE) -A $(TARGET).elf
	@echo
	@echo "ten largest symbols:"
	@$(NM) --size-sort -S -r $(TARGET).elf | head -10

dump: $(TARGET).elf
	$(OBJDUMP) -d $< > $(TARGET).lst
	@echo "$(TARGET).lst"

# Every MTP_T113_UNVERIFIED() in the image, read out of the section the linker
# script collects them into. The firmware prints this at boot; this target
# answers the same question without a board.
assumptions: $(TARGET).elf
	@echo "unverified constants compiled into $(TARGET).elf:"
	@$(OBJCOPY) -O binary --only-section=.rodata $(TARGET).elf /dev/null 2>/dev/null || true
	@$(NM) $(TARGET).elf | grep -i 'mtp_t113_assume' || \
	  echo "  (none found by name; they are static -- run 'make dump' and look" \
	       "for the .mtp_assumptions section, or read the boot log)"
	@echo
	@echo "source of truth is the grep:"
	@grep -rn 'MTP_T113_UNVERIFIED(' src/ include/ | grep -v '#define' || true

# boot/BRINGUP.md 4.1 recommendation (1): wrap the payload and use bootm, so
# that cleanup_before_linux() hands over with the MMU and D-cache off, which is
# the state src/start.S assumes.
uimage: $(TARGET).bin
	mkimage -A arm -O linux -T kernel -C none \
	        -a 0x40200000 -e 0x40200000 -n mt32-t113 \
	        -d $(TARGET).bin $(TARGET).uimg

clean:
	rm -rf $(BUILD)

help:
	@sed -n '2,20p' Makefile
