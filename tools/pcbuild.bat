@echo off
setlocal enabledelayedexpansion
rem Build the PC port. Called by build-debug.bat and build-release.bat at the
rem repository root; run those rather than this.
rem
rem   tools\pcbuild.bat <Debug|Release> [backend]     backend: D3D9 (default), GL3, NULL
rem
rem Every configuration builds into its own directory -- the render backend is
rem baked into the CMake cache and into librw's compile definitions, so they
rem cannot share one -- but they all put bfbb.exe and the DLLs it needs into
rem bin\. One place to run from, one config.ini, and no hunting for which
rem directory the last build went to. The last build wins, which is why the
rem script says at the end what is now sitting there.

set "CONFIG=%~1"
set "BACKEND=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"
if "%BACKEND%"=="" set "BACKEND=D3D9"

set "ROOT=%~dp0.."
pushd "%ROOT%" || exit /b 1
set "ROOT=%CD%"

rem Lowercase the directory name: build-debug, build-release.
set "BUILDDIR=build-%CONFIG%"
if /i "%CONFIG%"=="Debug"   set "BUILDDIR=build-debug"
if /i "%CONFIG%"=="Release" set "BUILDDIR=build-release"

rem ---- the 32-bit MSVC environment -------------------------------------------
rem clang++ needs the MSVC toolchain and Windows SDK on PATH, and -m32 means the
rem x86 ones. Ask vswhere where Visual Studio is rather than hardcoding a year
rem and an edition.
if defined VSCMD_ARG_TGT_ARCH (
  if /i "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
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
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 (echo ERROR: vcvarsall x86 failed. & popd & exit /b 1)
:have_env

rem ---- FFmpeg ----------------------------------------------------------------
rem Optional. Without it the movie decoder and the soundtrack override build as
rem stubs -- a configuration the port supports, but not the one to playtest.
rem Set BFBB_VCPKG to point somewhere else.
if not defined BFBB_VCPKG set "BFBB_VCPKG=%USERPROFILE%\vcpkg\installed\x86-windows"
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

echo === %CONFIG% / %BACKEND% -^> %BUILDDIR%, exe into bin\ ===
cmake -S . -B "%BUILDDIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=%CONFIG% ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  -DBFBB_RENDER_BACKEND=%BACKEND% ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%ROOT:\=/%/bin" ^
  %PREFIX%
if errorlevel 1 (echo. & echo CONFIGURE FAILED & popd & exit /b 1)

cmake --build "%BUILDDIR%"
if errorlevel 1 (echo. & echo BUILD FAILED & popd & exit /b 1)

rem What is in bin\ now, so a slow Release-looking build is never a mystery.
> "%ROOT%\bin\BUILD-INFO.txt" echo %CONFIG% / %BACKEND%, built from %BUILDDIR%
echo.
echo === bin\bfbb.exe is now %CONFIG% / %BACKEND% ===
if not exist "%ROOT%\bin\config.ini" (
  echo     No bin\config.ini yet. The game writes one with the defaults on
  echo     first run; set [assets] path in it to your Xbox game files.
)
popd
endlocal
