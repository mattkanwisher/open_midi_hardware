# CMake toolchain for the Allwinner T113-i application cores:
# ARM Cortex-A7, armv7-a, NEON-VFPv4, hard float ABI.
#
# The T113-i's A7s are armv7-a with the VFPv4-D32 FPU and Advanced SIMD (NEON).
# -mfpu=neon-vfpv4 is the right choice: it enables both NEON and the fused
# multiply-add instructions (VFMA), which vfpv3 lacks.
#
#   cmake -S bench -B bench/build-armv7 -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/bench/cmake/toolchain-armv7a-neon.cmake
#
# Override the compiler prefix if your toolchain is named differently:
#   -DCROSS_PREFIX=arm-none-linux-gnueabihf-
#
# Debian/Ubuntu:  apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
# Arch:           pacman -S arm-linux-gnueabihf-gcc
# Or the Arm GNU toolchain:
#   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#   (arm-none-linux-gnueabihf for a Linux board, arm-none-eabi for bare metal)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

if(NOT DEFINED CROSS_PREFIX)
  set(CROSS_PREFIX arm-linux-gnueabihf-)
endif()

set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)
set(CMAKE_AR           ${CROSS_PREFIX}ar           CACHE FILEPATH "")
set(CMAKE_RANLIB       ${CROSS_PREFIX}ranlib       CACHE FILEPATH "")
set(CMAKE_STRIP        ${CROSS_PREFIX}strip        CACHE FILEPATH "")
set(CMAKE_OBJDUMP      ${CROSS_PREFIX}objdump      CACHE FILEPATH "")
set(CMAKE_SIZE         ${CROSS_PREFIX}size         CACHE FILEPATH "")

# -mcpu=cortex-a7 implies -march=armv7-a and the A7 scheduling model.
# -mthumb: the A7 runs Thumb-2 at full speed and it is markedly smaller, which
# matters if we ever want this resident in SRAM. Flip to -marm to compare.
#
# Three knobs, so that the claims in ANALYSIS.md about NEON, Thumb-2 and -O
# level can be re-measured rather than asserted. bench/estimate.sh uses them:
#
#   -DT113_FPU=vfpv3-d16   a build with NO NEON at all, for the NEON ablation
#   -DT113_ISA=-marm       ARM rather than Thumb-2
#   -DT113_OPT=-O3         -O3 rather than -O2
#
if(NOT DEFINED T113_FPU)
  set(T113_FPU neon-vfpv4)
endif()
if(NOT DEFINED T113_ISA)
  set(T113_ISA "-mthumb")
endif()
if(NOT DEFINED T113_OPT)
  set(T113_OPT "-O2")
endif()

# -ffp-contract=off matches what the product builds use (port/PORTING.md 4.2),
# so the cost measured here is the cost of the binary that ships. It also
# slightly RAISES the instruction count, by un-fusing the multiply-accumulates
# in Analog.cpp's FIR -- so leaving it off would have flattered the estimate.
set(T113_ARCH_FLAGS "-mcpu=cortex-a7 -mfpu=${T113_FPU} -mfloat-abi=hard ${T113_ISA} -ffp-contract=off")

set(CMAKE_C_FLAGS_INIT   "${T113_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${T113_ARCH_FLAGS}")

# -O2 rather than -O3: mt32emu's hot loops are branchy per-sample state
# machines, and -O3's extra unrolling and vectorisation attempts mostly cost
# I-cache here. Measure both before believing either.
set(CMAKE_C_FLAGS_RELEASE_INIT   "${T113_OPT} -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "${T113_OPT} -DNDEBUG")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
