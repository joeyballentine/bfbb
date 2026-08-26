# The PC port's build. Nothing else in this repository reads this file.
#
# The GameCube build is configure.py -> build.ninja -> decomp-toolkit, and it
# is scored by whether build/GQPE78/main.dol comes out byte-identical to
# retail. That build lists every object it compiles explicitly, so a file this
# one adds can never enter it; and this build never invokes ninja, configure.py
# or CodeWarrior. The two share source, and nothing else.
#
# What keeps them from interfering is not this comment. It is:
#
#   * the include path. Game code says #include "iTime.h" and gets
#     src/SB/Core/gc/iTime.h there, src/SB/Core/pc/iTime.h here. Neither
#     directory is ever on the other build's path, so neither build can see
#     the other's platform layer even by accident.
#   * -DGAMECUBE, which only the CodeWarrior build defines. Shared headers
#     that must differ branch on it.
#   * the DOL's SHA-1. Any edit to shared source is checked by rebuilding the
#     GameCube target and confirming it still hashes to
#     306526d90b48e99894c3138f5fc8f2716d9fecf6. That is the gate that actually
#     proves this build has not changed that one.

cmake_minimum_required(VERSION 3.16)
project(bfbb-pc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/pc)

# Not a style preference. The source is full of *(U32*)&someFloat, written that
# way because it made CodeWarrior emit the right instruction; GCC and Clang at
# -O2 are entitled to miscompile it. See "Other things that will bite" in
# PCPORT.md.
add_compile_options(-fno-strict-aliasing)

# 32-bit is a project-wide constraint, not a preference. gMemInfo.DRAM.addr is
# a U32 and xMemInitHeap does pointer arithmetic on it as an integer; every
# asset-overlaid struct addresses memory in 32 bits; and sizeof(xSndVoiceInfo)
# is 100, which iSndPlay divides a byte offset by. On LP64 all of that is
# wrong. See PCPORT-HANDOFF.md 5(a): 162/198 units compile 64-bit against these
# headers and 195/198 under -m32.
#
# tools/pcprogress.py measures with the same flag. If you change it here, change
# it there -- the two disagreeing is how the metric lied once before.
option(BFBB_BUILD_32BIT "Build 32-bit, which the asset layouts require" ON)
if(BFBB_BUILD_32BIT)
  add_compile_options(-m32)
  add_link_options(-m32)
endif()

# --- input backend -----------------------------------------------------------
# "null" reports no controllers and is always available. It is a real
# configuration, not a missing implementation: a build with no input library
# genuinely has no controllers, and the game handles that -- it shows its
# "please reconnect the controller" screen.
set(BFBB_INPUT_BACKEND "null" CACHE STRING "Host input backend: null")
set_property(CACHE BFBB_INPUT_BACKEND PROPERTY STRINGS null)

if(BFBB_INPUT_BACKEND STREQUAL "null")
  set(INPUT_SOURCES ${PC_DIR}/iPadHostNull.cpp)
else()
  message(FATAL_ERROR "unknown BFBB_INPUT_BACKEND '${BFBB_INPUT_BACKEND}'")
endif()

# --- audio backend ---------------------------------------------------------
# Same arrangement as the input backend. "null" is silent but keeps time: it
# knows each sample's length and reports a voice as playing for exactly that
# long, because a great deal of the game waits on a sound -- dialogue lines,
# cutscene beats, NPC barks. A backend that finished everything instantly would
# desynchronise all of them and look like a bug in the game code.
set(BFBB_AUDIO_BACKEND "null" CACHE STRING "Host audio backend: null")
set_property(CACHE BFBB_AUDIO_BACKEND PROPERTY STRINGS null)

if(BFBB_AUDIO_BACKEND STREQUAL "null")
  set(AUDIO_SOURCES ${PC_DIR}/iSndHostNull.cpp)
else()
  message(FATAL_ERROR "unknown BFBB_AUDIO_BACKEND '${BFBB_AUDIO_BACKEND}'")
endif()

# --- host backend ---------------------------------------------------------
# The OS half of the platform layer, behind src/SB/Core/pc/iHost.h. The layer
# above it -- iTime, iMemMgr, iFile, iSystem, isavegame -- holds the mapping
# onto the game's semantics and is the same on every host; only this file
# changes. An #ifdef for the host OS belongs in one of these, never above them.
if(WIN32)
  set(HOST_SOURCES ${PC_DIR}/iHostWin32.cpp)
else()
  set(HOST_SOURCES ${PC_DIR}/iHostPosix.cpp)
endif()

# --- the platform layer ------------------------------------------------------
# Only the interfaces that are implemented. The rest of the i* set is listed in
# src/SB/Core/pc/README.md with what each one is waiting on; adding a file here
# before it exists would make this build fail rather than under-report, which
# is the right way round.
add_library(bfbb_platform STATIC
  ${PC_DIR}/iFile.cpp
  ${PC_DIR}/intrin.cpp
  ${PC_DIR}/iMath.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iPad.cpp
  ${PC_DIR}/iSystem.cpp
  ${PC_DIR}/iTRC.cpp
  ${PC_DIR}/iTime.cpp
  ${PC_DIR}/isavegame.cpp
  ${PC_DIR}/iSnd.cpp

  # Copied unchanged from gc/ -- pure geometry and collision, nothing
  # GameCube-specific in them. See VERBATIM.txt; --drift reports divergence.
  ${PC_DIR}/iMath3.cpp
  ${PC_DIR}/iCollideFast.cpp

  # Also a verbatim copy, but three of its calls are RenderWare collision-world
  # functions (RpAtomicForAllIntersections, RpCollisionWorldForAllIntersections,
  # RwFrameTransform), so it cannot resolve until librw arrives. Fine in a static
  # library, which is why it is here and not in pc_selftest below.
  ${PC_DIR}/iCollide.cpp

  ${INPUT_SOURCES}
  ${AUDIO_SOURCES}
  ${HOST_SOURCES}
)

# compat/ shadows <math.h>, <string.h> and <cmath> to add the CodeWarrior
# extensions the game's sources use, chaining to the system headers with
# include_next. It has to come first for that to work.
target_include_directories(bfbb_platform PUBLIC
  ${PC_DIR}/compat
  ${PC_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/rwsdk
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Game
)

# --- self-test ---------------------------------------------------------------
# Exercises the parts of the layer that do not need the renderer, so that
# "implemented" means something ran and produced the right answer rather than
# something compiled. ctest, or run it directly.
enable_testing()

add_executable(pc_selftest ${PC_DIR}/tests/selftest.cpp
  ${PC_DIR}/iFile.cpp
  ${PC_DIR}/intrin.cpp
  ${PC_DIR}/iMath.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iTime.cpp
  ${PC_DIR}/isavegame.cpp
  ${PC_DIR}/iSnd.cpp
  # The three files copied from gc/ are deliberately absent from this target.
  #
  # iMath3 calls xVec3Sub, xVec3Length, xVec3Add and xVec3SMul, whose bodies the
  # decomp places in xCamera.cpp -- CodeWarrior emitted those weak inlines into
  # whichever object the linker happened to pick, and matching means putting
  # them back there. Linking xCamera.cpp to reach four vector helpers drags in
  # 57 further symbols.
  #
  # iCollide needs three RenderWare collision-world calls, so it cannot link at
  # all until librw arrives. iCollideFastInit is an empty function in retail
  # too, and a check that calls it and asserts nothing would be padding.
  #
  # All three are verbatim copies of shipping code, so what is unproven about
  # them is only that they build on a host -- which bfbb_platform already shows.

  ${INPUT_SOURCES}
  ${AUDIO_SOURCES}
  ${HOST_SOURCES}
)
target_include_directories(pc_selftest PRIVATE
  ${PC_DIR}/compat
  ${PC_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/rwsdk
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Game
)

add_test(NAME pc_selftest COMMAND pc_selftest)
