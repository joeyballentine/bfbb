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

# --- the platform layer ------------------------------------------------------
# Only the interfaces that are implemented. The rest of the i* set is listed in
# src/SB/Core/pc/README.md with what each one is waiting on; adding a file here
# before it exists would make this build fail rather than under-report, which
# is the right way round.
add_library(bfbb_platform STATIC
  ${PC_DIR}/iFile.cpp
  ${PC_DIR}/iMath.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iPad.cpp
  ${PC_DIR}/iSystem.cpp
  ${PC_DIR}/iTRC.cpp
  ${PC_DIR}/iTime.cpp
  ${INPUT_SOURCES}
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
  ${PC_DIR}/iMath.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iTime.cpp
  ${INPUT_SOURCES}
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
