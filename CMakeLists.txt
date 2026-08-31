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
# docs/PCPORT.md.
add_compile_options(-fno-strict-aliasing)

# Selects the port's own struct layouts in include/rwsdk -- the ones that mirror
# librw so an RwFrame* is an rw::Frame*. It has to be a positive marker that
# ONLY the port defines, and it must match what tools/pcprogress.py measures
# with, or the two disagree the way they did over -m32.
#
# NOT -DGAMECUBE inverted, which is what those guards used to key on: GAMECUBE
# lives in configure.py's cflags_bfbb and so is absent from src/rwsdk,
# src/dolphin and src/bink. Those are compiled by CodeWarrior for the console
# and were silently getting the port's layouts; src/rwsdk stopped compiling
# outright once the camera structs landed.
add_compile_definitions(PLATFORM_PC)

# Microsoft's STL hard-errors (STL1000) when the clang major version is not the
# one it shipped against. That has nothing to do with this port -- it is a
# toolchain version check -- and it takes out librw as well as the game code,
# which is why this is a global definition rather than a per-target one.
# tools/pcprogress.py passes the same flag for the same reason; if you change
# it here, change it there.
add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)

# The CRT marks fopen, getenv, strcpy, strncpy and strcat deprecated in favour
# of its own _s variants. The game calls all five and will keep calling them --
# they are what the console's MSL had -- so the deprecation is 71 warnings the
# port cannot act on. It was invisible until include/types.h stopped stubbing
# __declspec out on this build, which is also what made __declspec(deprecated)
# start meaning something.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# 32-bit is a project-wide constraint, not a preference. gMemInfo.DRAM.addr is
# a U32 and xMemInitHeap does pointer arithmetic on it as an integer; every
# asset-overlaid struct addresses memory in 32 bits; and sizeof(xSndVoiceInfo)
# is 100, which iSndPlay divides a byte offset by. On LP64 all of that is
# wrong. See docs/PCPORT-HANDOFF.md 5(a): 162/198 units compile 64-bit against these
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
# "sdl" is the default and reaches every controller SDL knows: DualSense,
# Switch Pro, arcade sticks and generic USB pads as well as the Xbox ones,
# through DirectInput, raw input, HID and Windows.Gaming.Input at once. It also
# maps them all onto one standard layout, which is what lets config.ini name a
# button and have it mean the same thing on every device.
#
# "win32" is XInput only, and needs nothing built alongside it: XInput is loaded
# at run time, so it needs no import library and starts on a machine that has
# none. It is what a checkout with no submodules still has.
#
# "null" reports no controllers and is available everywhere. It is a real
# configuration, not a missing implementation: a build with no input library
# genuinely has no controllers, and the game handles that -- it shows its
# "please reconnect the controller" screen.
if(WIN32)
  set(BFBB_INPUT_BACKEND "sdl" CACHE STRING "Host input backend: sdl, win32, null")
else()
  set(BFBB_INPUT_BACKEND "null" CACHE STRING "Host input backend: null")
endif()
set_property(CACHE BFBB_INPUT_BACKEND PROPERTY STRINGS sdl win32 null)

# The keyboard is not a backend. It covers port 0 whenever no controller is on
# it, and which controller API a build talks to says nothing about how it should
# read a keyboard -- so both Windows backends link the same one. The SDL backend
# deliberately does not use SDL's: SDL_GetKeyboardState wants the video
# subsystem and an event loop tied to an SDL window, and iWindow owns the only
# window this process has.
set(INPUT_DEFINES)
set(INPUT_LIBS)
if(BFBB_INPUT_BACKEND STREQUAL "null")
  set(INPUT_SOURCES ${PC_DIR}/iPadHostNull.cpp)
elseif(BFBB_INPUT_BACKEND STREQUAL "win32")
  if(NOT WIN32)
    message(FATAL_ERROR "BFBB_INPUT_BACKEND=win32 needs a Windows host")
  endif()
  set(INPUT_SOURCES ${PC_DIR}/iPadHostWin32.cpp ${PC_DIR}/iPadKeyboardWin32.cpp)
  # So the self-test knows which backend it is linked against: the contract it
  # checks is the same either way, but "no controller on port 0" means an
  # unplugged pad under null and a keyboard under the other two.
  set(INPUT_DEFINES BFBB_INPUT_BACKEND_WIN32 BFBB_INPUT_HAVE_KEYBOARD)
elseif(BFBB_INPUT_BACKEND STREQUAL "sdl")
  if(NOT WIN32)
    message(FATAL_ERROR "BFBB_INPUT_BACKEND=sdl needs a Windows host for its keyboard half")
  endif()
  set(INPUT_SOURCES ${PC_DIR}/iPadHostSDL.cpp ${PC_DIR}/iPadKeyboardWin32.cpp)
  set(INPUT_DEFINES BFBB_INPUT_BACKEND_SDL BFBB_INPUT_HAVE_KEYBOARD)

  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt)
    message(FATAL_ERROR
      "third_party/SDL is empty -- run: git submodule update --init --recursive\n"
      "  (or configure with -DBFBB_INPUT_BACKEND=win32 for XInput only)")
  endif()

  # Only the gamepad half of SDL is wanted. Video stays ON despite no SDL window
  # ever being created: SDL gates its DirectInput joystick driver on the video
  # target, so turning video off silently drops support for exactly the older
  # pads this backend exists to reach. Compiled in and never initialised costs
  # a larger static library and nothing at run time.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_GPU OFF CACHE BOOL "" FORCE)
  set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
  set(SDL_POWER OFF CACHE BOOL "" FORCE)

  add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

  # SDL is not warning-clean under this compiler and it is not this project's
  # code to fix. Same treatment librw gets.
  target_compile_options(SDL3-static PRIVATE -Wno-everything)

  set(INPUT_LIBS SDL3::SDL3-static)
else()
  message(FATAL_ERROR "unknown BFBB_INPUT_BACKEND '${BFBB_INPUT_BACKEND}'")
endif()

# --- audio backend ---------------------------------------------------------
# Same arrangement as the input backend. "null" is silent but keeps time: it
# knows each sample's length and reports a voice as playing for exactly that
# long, because a great deal of the game waits on a sound -- dialogue lines,
# cutscene beats, NPC barks. A backend that finished everything instantly would
# desynchronise all of them and look like a bug in the game code.
#
# "win32" is a software mixer feeding one WASAPI stream: 64 voices summed here
# rather than handed to an API that has its own idea of what a voice is. It
# keeps the null backend's timing promise -- if no device opens, it goes silent
# and the voices still advance on the clock, so nothing the game derives from a
# sound's length changes.
if(WIN32)
  set(BFBB_AUDIO_BACKEND "win32" CACHE STRING "Host audio backend: win32, null")
else()
  set(BFBB_AUDIO_BACKEND "null" CACHE STRING "Host audio backend: null")
endif()
set_property(CACHE BFBB_AUDIO_BACKEND PROPERTY STRINGS win32 null)

# --- movie playback ----------------------------------------------------------
#
# Two backends again, for the same reason as audio and input: what decodes a
# movie is a host question. The port's movies are the Xbox release's .xmv --
# WMV2 video in an XMV container -- and the only sane way to decode WMV2 is to
# hand it to something that already does.
#
# FFmpeg is OPTIONAL. Not finding it is a supported configuration rather than a
# failure: the null decoder is built instead and movies are reported to the game
# as having run to the end, which is what the port did before any of this.
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
find_library(FFMPEG_AVCODEC_LIBRARY avcodec)
find_library(FFMPEG_AVFORMAT_LIBRARY avformat)
find_library(FFMPEG_AVUTIL_LIBRARY avutil)
find_library(FFMPEG_SWSCALE_LIBRARY swscale)
find_library(FFMPEG_SWRESAMPLE_LIBRARY swresample)

if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVCODEC_LIBRARY AND FFMPEG_AVFORMAT_LIBRARY
   AND FFMPEG_AVUTIL_LIBRARY AND FFMPEG_SWSCALE_LIBRARY AND FFMPEG_SWRESAMPLE_LIBRARY)
  set(FMV_SOURCES ${PC_DIR}/iFMVDecoderFFmpeg.cpp)
  set(FMV_LIBRARIES
    ${FFMPEG_AVFORMAT_LIBRARY}
    ${FFMPEG_AVCODEC_LIBRARY}
    ${FFMPEG_SWSCALE_LIBRARY}
    ${FFMPEG_SWRESAMPLE_LIBRARY}
    ${FFMPEG_AVUTIL_LIBRARY}
  )
  set(FMV_INCLUDES ${FFMPEG_INCLUDE_DIR})
  # Also read by iSoundtrack.cpp, which decodes a replacement music track
  # through the same libraries. A build without them still plays WAVE.
  set(FMV_DEFINES BFBB_HAVE_FFMPEG)
  message(STATUS "FMV decoder: ffmpeg (${FFMPEG_INCLUDE_DIR})")
else()
  set(FMV_SOURCES ${PC_DIR}/iFMVDecoderNull.cpp)
  set(FMV_LIBRARIES)
  set(FMV_INCLUDES)
  set(FMV_DEFINES)
  message(STATUS "FMV decoder: none (FFmpeg not found; movies will be skipped)")
endif()

# The movie's own audio output, which is deliberately not the game's mixer --
# see iFMVAudio.h. waveOut needs winmm.
if(WIN32)
  list(APPEND FMV_SOURCES ${PC_DIR}/iFMVAudioWin32.cpp)
  list(APPEND FMV_LIBRARIES winmm)
else()
  list(APPEND FMV_SOURCES ${PC_DIR}/iFMVAudioNull.cpp)
endif()

set(AUDIO_DEFINES)
if(BFBB_AUDIO_BACKEND STREQUAL "null")
  set(AUDIO_SOURCES ${PC_DIR}/iSndHostNull.cpp)
elseif(BFBB_AUDIO_BACKEND STREQUAL "win32")
  if(NOT WIN32)
    message(FATAL_ERROR "BFBB_AUDIO_BACKEND=win32 needs a Windows host")
  endif()
  set(AUDIO_SOURCES ${PC_DIR}/iSndHostWin32.cpp)
  set(AUDIO_DEFINES BFBB_AUDIO_BACKEND_WIN32)
else()
  message(FATAL_ERROR "unknown BFBB_AUDIO_BACKEND '${BFBB_AUDIO_BACKEND}'")
endif()

# The environmental reverb, in both backends rather than only the one that can
# be heard. It is pure DSP with no OS in it, so building and testing it does not
# depend on which backend was picked, and a change that breaks it should break
# every configuration rather than only the audible one.
list(APPEND AUDIO_SOURCES ${PC_DIR}/iSndReverb.cpp)

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


# --- window backend ---------------------------------------------------------
# The one host seam whose implementation is chosen by the RENDER backend rather
# than by the operating system: librw's EngineOpenParams is per backend, so
# D3D9 wants a raw HWND and GL3 wants whatever GLFW or SDL hands back. See
# src/SB/Core/pc/iWindow.h.
if(BFBB_RENDER_BACKEND STREQUAL "GL3")
  message(FATAL_ERROR
    "BFBB_RENDER_BACKEND=GL3 needs a GLFW or SDL iWindow implementation that does "
    "not exist yet -- see the GL3 arm of RwEngineOpen in "
    "src/SB/Core/pc/rw/engine_start.cpp. Use D3D9 or NULL.")
elseif(WIN32)
  set(WINDOW_SOURCES ${PC_DIR}/iWindowWin32.cpp)
else()
  # NULL builds headless, so no window is needed and none is provided; a D3D9
  # build off Windows is not a thing.
  set(WINDOW_SOURCES)
endif()

# --- librw -------------------------------------------------------------------
# A git submodule, pinned. Update it deliberately -- the shim in
# src/SB/Core/pc/rw mirrors librw's object LAYOUTS, so a librw that reorders a
# field is not a drop-in: layout*.cpp static_asserts every offset the port
# depends on and the build stops rather than reading the wrong bytes. That is
# the whole reason those assertions exist.
#
#     git submodule update --init --recursive
#
# The render backend. Three of librw's are reachable from here:
#
#   NULL   no renderer. Headless, and what the shim's selftest runs against --
#          keeping this working is what lets the RenderWare layer be tested on
#          a machine with no display at all.
#   D3D9   what the port targets today.
#   GL3    reachable in principle, and NOT yet: it needs an iWindowGlfw.cpp (or
#          SDL) beside iWindowWin32.cpp and the matching EngineOpenParams arm in
#          rw/engine_start.cpp, which #errors rather than guessing. GL3's params
#          differ by which gfx library librw was built against, so there is
#          nothing sensible to write until that choice is made.
#
# RwIm2DVertex follows this: rwplcore.h declares it per backend and
# rw/layout_im2d.cpp asserts it against the backend's own struct, which is what
# stops a mismatched vertex reaching a vertex declaration built from offsetof.
set(BFBB_RENDER_BACKEND "D3D9" CACHE STRING "Render backend: D3D9, GL3 or NULL")
set_property(CACHE BFBB_RENDER_BACKEND PROPERTY STRINGS D3D9 GL3 NULL)

set(LIBRW_PLATFORM "${BFBB_RENDER_BACKEND}" CACHE STRING "librw render backend" FORCE)
set(LIBRW_TOOLS OFF CACHE BOOL "" FORCE)
set(LIBRW_INSTALL OFF CACHE BOOL "" FORCE)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/CMakeLists.txt)
  message(FATAL_ERROR
    "third_party/librw is empty -- run: git submodule update --init --recursive")
endif()

add_subdirectory(third_party/librw)

# librw is not warning-clean and it is not this project's code to fix.
target_compile_options(librw PRIVATE -Wno-everything)

# librw links libm whenever the compiler ID is GNU or Clang
# (third_party/librw/src/CMakeLists.txt:114). Targeting the MSVC ABI, clang
# still identifies as "Clang" and there is no m.lib -- the maths functions are
# in the CRT -- so every consumer of librw fails to link with
#
#     lld-link: error: could not open 'm.lib': no such file or directory
#
# It is PRIVATE, but a static library's private dependencies still reach its
# consumers as $<LINK_ONLY:...>, so it cannot be ignored. Stripped here rather
# than patched in the submodule: the submodule is a pinned upstream checkout
# and a local edit to it would not survive `git submodule update`.
if(WIN32)
  get_target_property(_librw_link_interface librw INTERFACE_LINK_LIBRARIES)
  if(_librw_link_interface)
    list(REMOVE_ITEM _librw_link_interface "$<LINK_ONLY:m>")
    set_target_properties(librw PROPERTIES
      INTERFACE_LINK_LIBRARIES "${_librw_link_interface}")
  endif()
endif()

# --- the RenderWare shim -----------------------------------------------------
# The RenderWare C API, implemented on top of librw's rw:: C++ one. See
# src/SB/Core/pc/rw/README.md for why this layer has to exist -- librw is a
# reimplementation of RenderWare, not an implementation of its C API, and the
# game calls the C API at sites the GameCube build matches against.
file(GLOB RW_SHIM_SOURCES ${PC_DIR}/rw/*.cpp)
# iScreen is in here rather than in the platform layer because
# rw/engine_start.cpp sets the virtual screen from it, and bfbb_rw does not
# link bfbb_platform. It holds one size and depends on nothing.
add_library(bfbb_rw STATIC ${RW_SHIM_SOURCES} ${WINDOW_SOURCES} ${PC_DIR}/iScreen.cpp)

target_include_directories(bfbb_rw PUBLIC
  ${PC_DIR}/compat
  ${PC_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/rwsdk
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Game
)
target_link_libraries(bfbb_rw PUBLIC librw)
target_compile_options(bfbb_rw PRIVATE -Wno-everything)

# --- the platform layer ------------------------------------------------------
# Only the interfaces that are implemented. The rest of the i* set is listed in
# src/SB/Core/pc/README.md with what each one is waiting on; adding a file here
# before it exists would make this build fail rather than under-report, which
# is the right way round.
add_library(bfbb_platform STATIC
  ${PC_DIR}/iConfig.cpp
  ${PC_DIR}/iDrawDist.cpp
  ${PC_DIR}/iFile.cpp
  ${PC_DIR}/intrin.cpp
  ${PC_DIR}/iMath.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iMenuFrame.cpp
  ${PC_DIR}/iPad.cpp
  ${PC_DIR}/iPadBind.cpp
  ${PC_DIR}/iPadStick.cpp
  ${PC_DIR}/iSystem.cpp
  ${PC_DIR}/iTextPatch.cpp
  ${PC_DIR}/iTRC.cpp
  ${PC_DIR}/iTime.cpp
  ${PC_DIR}/isavegame.cpp
  ${PC_DIR}/iSnd.cpp
  ${PC_DIR}/iSndData.cpp
  ${PC_DIR}/iSoundtrack.cpp

  # Copied unchanged from gc/ -- pure geometry and collision, nothing
  # GameCube-specific in them. See VERBATIM.txt; --drift reports divergence.
  ${PC_DIR}/iMath3.cpp
  ${PC_DIR}/iCollideFast.cpp

  # Ported, and a verbatim copy like the two above: gc/iLight.cpp is pure
  # RenderWare C API with nothing GameCube-specific in it, so there was nothing
  # for a rewrite to improve. The light flag and type enumerations it passes
  # through are asserted against librw's in rw/layout_camera.cpp.
  ${PC_DIR}/iLight.cpp

  # Also verbatim. Its FastS16unpack/FastS16weight2/FastS16weight4 are plain
  # scalar loops on the console too -- no paired-single, no PowerPC intrinsics
  # -- and none of the three shares a name with a C library function, so the
  # xSpline trap does not apply. It is the first and only caller of the
  # RenderWare user-data plugin on the PC path; see rw/usrdata.cpp.
  ${PC_DIR}/iMorph.cpp

  # NOT verbatim, and the only one of the ported modules that is not. 1087
  # lines, of which exactly one needed changing: it clears
  # rpINTERPOLATORDIRTYSPHERE on an RpAtomic, and the port's RpAtomic has no
  # interpolator because librw's is 84 bytes to RenderWare's 112. rw/TODO.md
  # predicted this would be the third such site. Its skeleton support is
  # rw/hanim.cpp.
  ${PC_DIR}/iModel.cpp

  # Ported, and a verbatim copy as well: gc/iEnv.cpp is RenderWare world and
  # clump rendering with nothing GameCube-specific in it. The render-state
  # enumerations it passes through (cull mode, blend, z-write) are asserted
  # against librw's in rw/layout_camera.cpp.
  ${PC_DIR}/iEnv.cpp

  # Also a verbatim copy, but three of its calls are RenderWare collision-world
  # functions (RpAtomicForAllIntersections, RpCollisionWorldForAllIntersections,
  # RwFrameTransform), so it cannot resolve until librw arrives. Fine in a static
  # library, which is why it is here and not in pc_selftest below.
  ${PC_DIR}/iCollide.cpp

  # Ported rather than copied: gc/iAnimSKB.cpp defines std::fabsf with
  # __declspec(weak) to reproduce where retail's one copy of that symbol lives,
  # which collides with compat/_stdfloatmath.h on a host. The definition is
  # guarded there; gc/ is untouched. Needs RtQuatSetupSlerpCache, so like
  # iCollide it links when librw does.
  ${PC_DIR}/iAnimSKB.cpp

  # Ported: retail seeks to a cutscene inside its HIP with a DVD sector delta,
  # (ainfo.sector - fileInfo.startAddr) << 5. There is no disc and tag_iFile has
  # no DVDFileInfo, so the port does the same arithmetic without it -- exactly,
  # not approximately; the comment at the site shows why. Needs the RwEngineInstance
  # global, so like the others it resolves when librw does.
  ${PC_DIR}/iCutscene.cpp

  # Ported, and a verbatim copy: gc/iAnim.cpp is a thin front on iAnimSKB plus
  # one quaternion blend written entirely in RtQuat calls, with nothing
  # GameCube-specific in it. It passes no RenderWare enumerations through, so
  # there is nothing new for rw/layout_*.cpp to assert.
  ${PC_DIR}/iAnim.cpp

  # Ported, and a verbatim copy too: gc/iParMgr.cpp is the particle renderer,
  # and it builds its quads into RxObjSpace3DVertex arrays and hands them to
  # RwIm3DTransform -- no GX, no OS, nothing GameCube-specific in it. The one
  # RenderWare enumeration it passes through numerically is rwPRIMTYPETRILIST,
  # already asserted against librw's in rw/layout_camera.cpp;
  # rwRENDERSTATETEXTURERASTER is translated by the shim's own switch in
  # rw/renderstate.cpp rather than passed through, so it needs no assert.
  # It also owns the gRenderArr / gRenderBuffer globals.
  ${PC_DIR}/iParMgr.cpp


  # Ported, and the first of these that is NOT a verbatim copy. gc/iScrFX.cpp is
  # RenderWare 2D rendering and one dead motion-blur state machine, and all of
  # it copies -- except that iCameraOverlayRender brackets its draw with
  # RwRasterLock/RwRasterUnlock, which the shim does not have and which a host
  # must not do anyway: a locked raster is one the device cannot sample from.
  # The bracket is dropped, with the reasoning at the site. Everything else is
  # byte-for-byte gc/. The blend, filter and primitive-type enumerations it
  # passes through are all asserted in rw/layout_camera.cpp already.
  #
  # Note that iScrFX.h is deliberately diverged rather than copied -- see the
  # comment about rwGameCube2DVertex in VERBATIM.txt -- so this module is off
  # the drift list at both ends.
  # Ported as a REFUSAL, and the game already handles it: xFX.cpp:883 leaves an
  # atomic on its default pipeline when this returns NULL, so surfaces with
  # animated texture coordinates draw static instead of moving. The file says
  # why there is nothing to forward to and what a real implementation costs.
  # Not a port of gc/iFMV.cpp and never will be -- that file decodes Bink, which
  # is proprietary middleware that cannot be redistributed. Movies return
  # immediately as "ran to the end" so the game advances past them. The plan of
  # record is ffmpeg ahead of time plus a different player; the file lists what
  # that will need.
  ${PC_DIR}/iFMV.cpp

  ${PC_DIR}/iFX.cpp

  ${PC_DIR}/iScrFX.cpp

  # Ported. gc/iDraw.cpp is three functions: two empty ones and iDrawSetFBMSK,
  # a frame-buffer colour/alpha write mask that had no spelling in RenderWare's
  # portable render state or in librw's. It was a no-op for a while, which made
  # the depth-priming first pass at four call sites paint an opaque copy of
  # itself. Closed by adding a COLORWRITEMASK render state to the librw fork;
  # this file forwards to it, inverting per channel because the GS convention is
  # the opposite of librw's.
  ${PC_DIR}/iDraw.cpp

  # The STUBS block that used to sit here is GONE, and that is the milestone:
  # all eleven platform modules have real implementations above. Seven were
  # byte-identical copies of their gc counterparts, two needed one hunk each,
  # and two -- iFX and iFMV -- are documented refusals whose files say what is
  # lost and what closing them would take.
  #
  # src/SB/Core/pc/iStub.h is deliberately kept even though nothing includes it
  # any more. It is the pattern for the next module that has to be stubbed
  # before it can be written, and PORTING.md points at it.

  ${INPUT_SOURCES}
  ${AUDIO_SOURCES}
  ${HOST_SOURCES}
  ${FMV_SOURCES}
)

# NOT just for the include path: this is what gives bfbb_platform the backend
# define. librw exports RW_D3D9 (or RW_GL3, or RW_NULL) as a PUBLIC compile
# definition, bfbb_rw passes it on, and include/rwsdk/rwplcore.h declares
# RwIm2DVertex PER BACKEND -- so a target that compiles without it gets a
# different struct than the rest of the port.
#
# That is an ODR violation and it was live: bfbb_platform built with
# -DPLATFORM_PC alone while bfbb_rw and bfbb_game built with -DRW_D3D9, so
# iScrFX.h's _iMotionBlurData had one colour order in iScrFX.cpp and another
# everywhere that touches it. Same size, so nothing failed to link and nothing
# asserted -- it would have surfaced as wrong colours, or worse.
target_link_libraries(bfbb_platform PUBLIC bfbb_rw)

# The movie decoder and its audio device. PRIVATE: nothing above bfbb_platform
# includes an FFmpeg header, and iFMV.cpp is the only caller of either seam.
target_link_libraries(bfbb_platform PRIVATE ${FMV_LIBRARIES})

# The input backend's library, if it has one. PUBLIC because both the game
# and the self-test link the backend through this target.
target_link_libraries(bfbb_platform PUBLIC ${INPUT_LIBS})
if(FMV_INCLUDES)
  target_include_directories(bfbb_platform PRIVATE ${FMV_INCLUDES})
endif()
if(FMV_DEFINES)
  target_compile_definitions(bfbb_platform PRIVATE ${FMV_DEFINES})
endif()

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


# --- the game code -----------------------------------------------------------
# Every translation unit in src/SB/Core/x and src/SB/Game -- 198 of them, the
# same set tools/pcprogress.py measures. All 198 compile.
#
# Compiling is not linking, and the difference is the point of this target
# existing. pcprogress compiles each unit ALONE with -fsyntax-only, so it can
# say nothing about undefined symbols, duplicate definitions, or a function
# whose declaration and definition disagree across two files. Every one of
# those is invisible until something tries to resolve them, which is what
# tools/pclink.py does with this library.
#
# GLOB rather than an explicit list: this set is "all of it", and a unit that
# stops compiling should break the build rather than quietly leave the
# measurement.
file(GLOB_RECURSE BFBB_GAME_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Game/*.cpp
)
add_library(bfbb_game STATIC ${BFBB_GAME_SOURCES})
target_link_libraries(bfbb_game PUBLIC bfbb_platform bfbb_rw)
target_compile_options(bfbb_game PRIVATE -Wno-everything)

# --- the RenderWare shim's own test ------------------------------------------
# 451 checks against a live librw engine. Separate from pc_selftest because it
# needs librw and pc_selftest deliberately does not -- the platform layer is
# testable without a renderer and that is worth keeping true.
# iFX.cpp is compiled in rather than linked from bfbb_platform, and it is the
# only platform module that is. It is the one that talks to rw:: directly --
# everything else in that library reaches RenderWare through the shim -- so it
# is the one whose behaviour needs a live engine to check, and bfbb_platform
# would drag in the rest of the layer to get it. The four xFXanimUV globals it
# reads are stood up by the test itself.
add_executable(rw_selftest ${PC_DIR}/rw/tests/selftest.cpp ${PC_DIR}/iFX.cpp)
target_link_libraries(rw_selftest PRIVATE bfbb_rw)
target_compile_options(rw_selftest PRIVATE -Wno-everything)
add_test(NAME rw_selftest COMMAND rw_selftest)

# --- the game --------------------------------------------------------------
# It links. Whether it RUNS is the next question, and the reason this target
# exists: eleven of its platform modules are stubs (see bfbb_platform above),
# so what this executable is for right now is booting it and reading, in order,
# which stub the startup path reaches first. That log is the worklist.
#
# main() is the game's own, in src/SB/Game/zMain.cpp -- nothing here supplies an
# entry point.
# The icon is a Windows resource, so it is a source file of the executable
# rather than anything the linker is told about separately. Only Windows has
# the concept; on a host without a resource compiler the list is simply empty
# and the binary has no icon, which is not worth failing a build over.
set(BFBB_EXE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/pc/bfbb_main.cpp)

if(WIN32)
  # CMake only knows how to build .rc files if RC is an enabled language, and
  # enabling it is what finds llvm-rc (or rc.exe) and sets CMAKE_RC_COMPILER.
  enable_language(RC OPTIONAL)

  if(CMAKE_RC_COMPILER)
    list(APPEND BFBB_EXE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/pc/res/bfbb.rc)
    message(STATUS "bfbb: icon resource via ${CMAKE_RC_COMPILER}")
  else()
    message(STATUS "bfbb: no resource compiler found, executable will have no icon")
  endif()
endif()

add_executable(bfbb ${BFBB_EXE_SOURCES})
target_link_libraries(bfbb PRIVATE bfbb_game bfbb_platform bfbb_rw)

# The movie decoder's DLLs, beside the executable.
#
# Copied rather than left to the PATH because the port is run from its build
# directory far more often than it is installed, and a missing DLL does not fail
# at build time -- it fails when someone starts the game, with a dialog that
# names a file rather than a reason.
if(WIN32 AND FMV_INCLUDES)
  file(GLOB FMV_RUNTIME_DLLS "${FFMPEG_INCLUDE_DIR}/../bin/*.dll")
  if(FMV_RUNTIME_DLLS)
    add_custom_command(TARGET bfbb POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FMV_RUNTIME_DLLS} $<TARGET_FILE_DIR:bfbb>
      COMMENT "Staging the movie decoder's DLLs"
    )
  endif()
endif()
target_compile_options(bfbb PRIVATE -Wno-everything)

if(WIN32)
  # dbghelp is the crash handler in bfbb_main.cpp; see the comment there.
  # ole32 is CoCreateInstance in the WASAPI backend; the interfaces themselves
  # are declared by the SDK headers and their GUIDs are defined in
  # iSndHostWin32.cpp, so uuid.lib is deliberately not needed.
  target_link_libraries(bfbb PRIVATE user32 gdi32 dbghelp ole32)

  # EXPERIMENT: 64MB stack, to test whether the boot fault is stack depth.
  target_link_options(bfbb PRIVATE -Xlinker /STACK:67108864)
endif()

# --- self-test ---------------------------------------------------------------
# Exercises the parts of the layer that do not need the renderer, so that
# "implemented" means something ran and produced the right answer rather than
# something compiled. ctest, or run it directly.
enable_testing()

add_executable(pc_selftest ${PC_DIR}/tests/selftest.cpp
  ${PC_DIR}/iConfig.cpp
  ${PC_DIR}/iDrawDist.cpp
  ${PC_DIR}/iFile.cpp
  ${PC_DIR}/iScreen.cpp
  ${PC_DIR}/iTextPatch.cpp
  ${PC_DIR}/intrin.cpp
  ${PC_DIR}/iMath.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iTime.cpp
  ${PC_DIR}/isavegame.cpp
  ${PC_DIR}/iSnd.cpp

  # The sample cache, with test doubles in selftest.cpp standing in for the
  # three asset-system calls it makes. Linking it here rather than stubbing it
  # is what makes its eviction and pinning rules testable at all; the real
  # xstransvc closure is far too large for this target.
  ${PC_DIR}/iSndData.cpp

  # The soundtrack override, which iSndData asks before it reads an archive.
  # Deliberately built here WITHOUT BFBB_HAVE_FFMPEG: the WAVE reader is the
  # arm that has to work in every configuration, and testing it needs no
  # external codec and produces the same bytes on every machine.
  ${PC_DIR}/iSoundtrack.cpp

  # xordarray is the NPC list's container. It is linked rather than doubled
  # because the ordering test's whole point is that XOrdSort and XOrdLookup
  # agree with each other, which only the real pair can show.
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x/xordarray.cpp

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

  # The HIP container reader, so the byte-swapping in xhipio.cpp is exercised
  # rather than asserted. Its whole dependency closure is iFile, which this
  # layer already implements.
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x/xhipio.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x/xbinio.cpp

  # xUtil_idtag2string, which every asset id in a debug string goes through and
  # which feeds isprint a signed char. The host CRT aborts on the out-of-range
  # argument rather than shrugging at it, so this is worth a check that runs
  # every build. Its only unresolved symbol is xurand, which xMath.cpp defines
  # and which nothing this file tests calls -- linking xMath.cpp to reach it
  # drags in the weak-inline placement problem described above, so selftest.cpp
  # defines it instead.
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x/xutil.cpp

  # iPad.cpp for the stick shaping, which is where the host's circular stick is
  # turned into the octagon the game's own magnitude test assumes. Its only
  # unresolved symbols are gTrcPad and xTRCPad; selftest.cpp defines both rather
  # than linking xTRC.cpp, which drags in the whole TRC screen stack.
  ${PC_DIR}/iPad.cpp
  ${PC_DIR}/iPadBind.cpp
  ${PC_DIR}/iPadStick.cpp

  ${INPUT_SOURCES}
  ${AUDIO_SOURCES}
  ${HOST_SOURCES}
)
target_compile_definitions(pc_selftest PRIVATE ${INPUT_DEFINES} ${AUDIO_DEFINES})

# pc_selftest compiles the backend sources itself rather than linking
# bfbb_platform, so the input backend's library is its own to ask for.
target_link_libraries(pc_selftest PRIVATE ${INPUT_LIBS})
target_include_directories(pc_selftest PRIVATE
  ${PC_DIR}/compat
  ${PC_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/rwsdk
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/x
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Game
)

if(WIN32)
  # Same COM dependency as the game: the self-test links the real audio backend
  # rather than a stub, so that what it checks about voice timing is what the
  # game will actually do. dbghelp is iHostPrintCallers, in the host backend
  # this target links whole.
  target_link_libraries(pc_selftest PRIVATE ole32 dbghelp)
endif()

add_test(NAME pc_selftest COMMAND pc_selftest)
