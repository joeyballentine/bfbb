@echo off
setlocal enabledelayedexpansion
rem Build the PC port. Called by build-debug.bat and build-release.bat at the
rem repository root; run those rather than this.
rem
rem   tools\pcbuild.bat <Debug|Release> [backends] [arch]
rem
rem       backends: D3D9 and GL3 in one executable by default. Name one to
rem                 build only that: D3D9, D3D11, GL3 or NULL. D3D11 cannot
rem                 share a build with D3D9.
rem       arch:     x86 (default), x64
rem
rem The first build of a fresh directory also compiles wxWidgets, which the
rem settings program is drawn in, unless one is installed for CMake to find.
rem It is a long compile and it happens once per directory; -DBFBB_BUILD_
rem CONFIGURATOR=OFF skips it for a build that only wants the game.
rem
rem Every configuration builds into its own directory -- the backend set is
rem baked into the CMake cache and into librw's compile definitions, so two of
rem them cannot share one -- but they all put bfbb.exe, bfbb_config.exe and the
rem DLLs the game needs into bin\. One place to run from, one config.ini, and no hunting for which
rem directory the last build went to. The last build wins, which is why the
rem script says at the end what is now sitting there.
rem
rem Which of the backends in an executable actually draws is video.backend in
rem config.ini, not this.

set "CONFIG=%~1"
set "BACKEND=%~2"
set "ARCH=%~3"
if "%CONFIG%"=="" set "CONFIG=Debug"
rem Empty means whatever CMakeLists.txt picks for this host, which on Windows is
rem D3D9 and GL3 together.
rem
rem -U rather than nothing at all: a directory configured with a backend before
rem has it in its cache, and a cache entry outlives the argument that set it --
rem so leaving the option off would silently keep building the old set. Both
rem spellings, because the old singular one is still honoured.
set "BACKENDARG=-UBFBB_RENDER_BACKEND -UBFBB_RENDER_BACKENDS"
if not "%BACKEND%"=="" set "BACKENDARG=-DBFBB_RENDER_BACKENDS=%BACKEND%"
if "%ARCH%"=="" set "ARCH=x86"
if /i not "%ARCH%"=="x86" if /i not "%ARCH%"=="x64" (
  echo ERROR: arch must be x86 or x64, not %ARCH%.
  exit /b 1
)
set "M32=ON"
set "SUFFIX="
if /i "%ARCH%"=="x64" (set "M32=OFF" & set "SUFFIX=-x64")

set "ROOT=%~dp0.."
pushd "%ROOT%" || exit /b 1
set "ROOT=%CD%"

rem Lowercase the directory name: build-debug, build-release.
set "BUILDDIR=build-%CONFIG%"
if /i "%CONFIG%"=="Debug"   set "BUILDDIR=build-debug"
if /i "%CONFIG%"=="Release" set "BUILDDIR=build-release"
set "BUILDDIR=%BUILDDIR%%SUFFIX%"

rem ---- the MSVC environment --------------------------------------------------
rem clang++ needs the MSVC toolchain and Windows SDK on PATH, and they have to be
rem the ones matching ARCH: -m32 links against the x86 import libraries and the
rem 64-bit build against the x64 ones. Ask vswhere where Visual Studio is rather
rem than hardcoding a year and an edition.
if defined VSCMD_ARG_TGT_ARCH (
  if /i "%VSCMD_ARG_TGT_ARCH%"=="%ARCH%" goto :have_env
)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found. Is Visual Studio installed?
  popd & exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo ERROR: no Visual Studio installation found.
  popd & exit /b 1
)
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" (
  echo ERROR: %VSPATH% has no C++ toolchain ^(vcvarsall.bat missing^).
  echo        Install the "Desktop development with C++" workload.
  popd & exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 (echo ERROR: vcvarsall %ARCH% failed. & popd & exit /b 1)
:have_env

rem ---- FFmpeg ----------------------------------------------------------------
rem Optional. Without it the movie decoder and the soundtrack override build as
rem stubs -- a configuration the port supports, but not the one to playtest.
rem Set BFBB_VCPKG to point somewhere else.
if not defined BFBB_VCPKG set "BFBB_VCPKG=%USERPROFILE%\vcpkg\installed\%ARCH%-windows"
set "PREFIX="
if exist "%BFBB_VCPKG%\include" (
  set "PREFIX=-DCMAKE_PREFIX_PATH=%BFBB_VCPKG:\=/%"
) else (
  echo NOTE: %BFBB_VCPKG% not found -- building without FFmpeg, so no FMV and
  echo       no soundtrack override. Set BFBB_VCPKG if it lives elsewhere.
)

rem ---- configure and build ---------------------------------------------------
where clang++ >nul 2>&1 || (echo ERROR: clang++ is not on PATH. & popd & exit /b 1)
where ninja   >nul 2>&1 || (echo ERROR: ninja is not on PATH. & popd & exit /b 1)

echo === %CONFIG% / %ARCH% -^> %BUILDDIR%, exe into bin\ ===
cmake -S . -B "%BUILDDIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=%CONFIG% ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  %BACKENDARG% ^
  -DBFBB_BUILD_32BIT=%M32% ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%ROOT:\=/%/bin" ^
  %PREFIX%
if errorlevel 1 (echo. & echo CONFIGURE FAILED & popd & exit /b 1)

cmake --build "%BUILDDIR%"
if errorlevel 1 (echo. & echo BUILD FAILED & popd & exit /b 1)

rem What the configure settled on, read back rather than echoed from the
rem argument: with no argument the answer is CMakeLists.txt's, and it is the
rem thing worth reporting.
set "BACKENDNAME=?"
for /f "tokens=2 delims==" %%i in ('findstr /b "BFBB_RENDER_BACKENDS:" "%BUILDDIR%\CMakeCache.txt"') do set "BACKENDNAME=%%i"

rem What is in bin\ now, so a slow Release-looking build is never a mystery.
> "%ROOT%\bin\BUILD-INFO.txt" echo %CONFIG% / %BACKENDNAME% / %ARCH%, built from %BUILDDIR%
echo.
echo === bin\bfbb.exe is now %CONFIG% / %BACKENDNAME% / %ARCH% ===
if exist "%ROOT%\bin\bfbb_config.exe" (
  echo     bin\bfbb_config.exe edits config.ini with controls on it.
)
if not exist "%ROOT%\bin\config.ini" (
  echo     No bin\config.ini yet. The game writes one with the defaults on
  echo     first run; set [assets] path in it to your Xbox game files.
)
popd
endlocal
