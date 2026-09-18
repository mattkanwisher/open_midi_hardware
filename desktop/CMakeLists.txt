# mt32-desktop -- the mt32-t113 port, built for a laptop with speakers.
#
#   cmake -S . -B build && cmake --build build -j
#   ./build/mt32-desktop --midi-seq
#
# The real engine is built out of the vendored Munt, out of source, exactly the
# way port/host/CMakeLists.txt does it: nothing under bench/ is written to. It
# is found automatically if bench/vendor/munt/mt32emu is there; point
# MT32EMU_SOURCE_DIR somewhere else, or set it to "" for a build with only the
# fake engine (which needs no ROMs and no C++ at all beyond this file).
#
# SPDX-License-Identifier: 0BSD

cmake_minimum_required(VERSION 3.13)
project(mt32_desktop C CXX)

set(CMAKE_C_STANDARD 11)          # stdatomic in the audio ring
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 98)        # mt32emu is C++98; see port/PORTING.md 2.1
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../port")
set(BENCH_MUNT "${CMAKE_CURRENT_SOURCE_DIR}/../bench/vendor/munt/mt32emu")

if(NOT EXISTS "${PORT_DIR}/include/mtp_audio.h")
  message(FATAL_ERROR "port/ not found next to desktop/ -- this builds the "
                      "platform layer from ../port, it is not standalone")
endif()

add_executable(mt32-desktop
  # Above the seam: the same files the T113 will run.
  ${PORT_DIR}/src/mtp_midi_parser.c
  ${PORT_DIR}/src/mtp_render.c
  # The engine seam, shared with port/host so the two cannot drift.
  ${PORT_DIR}/host/engine_fake.c
  # Below the seam: this directory's whole reason to exist.
  src/desktop_audio_miniaudio.c
  src/desktop_midi.c
  src/desktop_midi_tty.c
  src/desktop_midi_smf.c
  src/desktop_midi_raw.c
  src/desktop_config.c
  src/desktop_storage_posix.c
  src/desktop_time_posix.c
  src/desktop_log.c
  src/desktop_status.c
  src/main.c
  vendor/miniaudio/miniaudio_impl.c
)

target_include_directories(mt32-desktop PRIVATE
  ${PORT_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mt32-desktop PRIVATE -Wall -Wextra -Wshadow)
  # miniaudio is vendored third-party code; do not fail our build on its style.
  set_source_files_properties(vendor/miniaudio/miniaudio_impl.c
    PROPERTIES COMPILE_OPTIONS "-w")
endif()

# ---------------------------------------------------------------- platform ---

find_package(Threads REQUIRED)
target_link_libraries(mt32-desktop PRIVATE Threads::Threads)

find_library(M_LIB m)
if(M_LIB)
  target_link_libraries(mt32-desktop PRIVATE ${M_LIB})
endif()

if(APPLE)
  # miniaudio is built with MA_NO_RUNTIME_LINKING on Apple, so the frameworks
  # are linked rather than dlopened; CoreMIDI is ours.
  target_link_libraries(mt32-desktop PRIVATE
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreMIDI")
  target_sources(mt32-desktop PRIVATE src/desktop_midi_seq_core.c)
  message(STATUS "mt32-desktop: OS MIDI port = CoreMIDI (NEVER COMPILED OR RUN "
                 "-- see FINDINGS.md)")
elseif(UNIX)
  # miniaudio dlopens libasound/libpulse at run time, so playback needs no
  # development packages at all. The ALSA *sequencer* (MIDI in from a keyboard
  # or a DAW) does need headers, and is optional.
  target_link_libraries(mt32-desktop PRIVATE ${CMAKE_DL_LIBS})
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    target_sources(mt32-desktop PRIVATE src/desktop_midi_seq_alsa.c)
    target_link_libraries(mt32-desktop PRIVATE ALSA::ALSA)
    target_compile_definitions(mt32-desktop PRIVATE MTP_WITH_ALSA_SEQ)
    message(STATUS "mt32-desktop: OS MIDI port = ALSA sequencer")
  else()
    target_sources(mt32-desktop PRIVATE src/desktop_midi_seq_none.c)
    message(STATUS "mt32-desktop: no ALSA headers -- --midi-seq disabled. "
                   "Install libasound2-dev for MIDI in from a keyboard or a DAW. "
                   "Audio playback is unaffected.")
  endif()
else()
  target_sources(mt32-desktop PRIVATE src/desktop_midi_seq_none.c)
endif()

# ------------------------------------------------------------------ engine ---

if(NOT DEFINED MT32EMU_SOURCE_DIR AND EXISTS "${BENCH_MUNT}/CMakeLists.txt")
  set(MT32EMU_SOURCE_DIR "${BENCH_MUNT}")
endif()
set(MT32EMU_SOURCE_DIR "${MT32EMU_SOURCE_DIR}" CACHE PATH "Path to munt/mt32emu")

if(MT32EMU_SOURCE_DIR AND EXISTS "${MT32EMU_SOURCE_DIR}/CMakeLists.txt")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(libmt32emu_SHARED OFF CACHE BOOL "" FORCE)
  add_subdirectory("${MT32EMU_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/mt32emu" EXCLUDE_FROM_ALL)
  target_sources(mt32-desktop PRIVATE ${PORT_DIR}/host/engine_mt32emu.cpp)
  target_compile_definitions(mt32-desktop PRIVATE MTP_WITH_MT32EMU)
  target_link_libraries(mt32-desktop PRIVATE MT32Emu::mt32emu)
  message(STATUS "mt32-desktop: linking mt32emu from ${MT32EMU_SOURCE_DIR}")
  message(STATUS "mt32-desktop: mt32emu is LGPL 2.1 -- see README.md, Licences")

  # The real synthesiser on fabricated ROM images, so that this build makes a
  # sound end to end on a machine that has no Roland data -- and so that the
  # audio it renders can be compared, sample for sample, against port/host and
  # against the bare-metal image. The fixture is emu/'s, compiled from where it
  # lives and never modified: one implementation of the trick, not three.
  # It needs MT32Emu::Test::getControlROMMap, which upstream exports only when
  # the library is compiled with MT32EMU_WITH_TESTING (Synth.cpp:2912).
  set(FAKEROM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../emu/src/engine_mt32emu_fake_roms.cpp")
  if(EXISTS "${FAKEROM_SRC}")
    target_compile_definitions(mt32emu PRIVATE MT32EMU_WITH_TESTING)
    target_sources(mt32-desktop PRIVATE ${FAKEROM_SRC})
    target_compile_definitions(mt32-desktop PRIVATE MTP_WITH_FAKEROM)
    # It includes <mt32emu/Structures.h>, which is a *private* header: upstream
    # copies only the public ones into the build tree (CMakeLists.txt:297).
    # Stage the rest beside them, inside our own build directory -- nothing
    # under bench/ is written to, which is the rule this repository keeps.
    file(GLOB MT32EMU_PRIVATE_HEADERS "${MT32EMU_SOURCE_DIR}/src/*.h")
    file(COPY ${MT32EMU_PRIVATE_HEADERS}
         DESTINATION "${CMAKE_BINARY_DIR}/mt32emu/include/mt32emu")
    target_include_directories(mt32-desktop PRIVATE
                               "${CMAKE_BINARY_DIR}/mt32emu/include")
    message(STATUS "mt32-desktop: --engine mt32emu-fakerom available "
                   "(emu/src/engine_mt32emu_fake_roms.cpp)")
  else()
    message(STATUS "mt32-desktop: no emu/src/engine_mt32emu_fake_roms.cpp -- "
                   "--engine mt32emu-fakerom unavailable")
  endif()
else()
  message(STATUS "mt32-desktop: fake engine only "
                 "(-DMT32EMU_SOURCE_DIR=<path to munt/mt32emu> to add the real one)")
endif()

# ------------------------------------------------------------------- tests ---

enable_testing()
# test.sh is the real suite: 27 assertions, none of which need a sound card,
# MIDI hardware or ROMs. ctest just runs it, so there is one set of tests and
# not two that drift.
add_test(NAME suite
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)
