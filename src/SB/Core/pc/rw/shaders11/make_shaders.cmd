@echo off
rem Regenerates the checked-in shader blobs from their .hlsl sources.
rem
rem The shader model 4 tree, for the D3D11 backend. ../shaders is the same set
rem at ps_2_0 for D3D9; the two carry the same file names, and CMake puts one of
rem them on the include path. As there, the compiled blob is checked in so the
rem build needs no shader compiler.
rem
rem Run this only when a .hlsl changes, and commit the .h with it.
call "%~dp0..\..\..\..\..\..\third_party\librw\src\d3d\shaders11\findfxc.cmd" || exit /b 1

"%FXC%" /nologo /T ps_4_0 /Fh distort_PS.h distort_PS.hlsl || exit /b 1
"%FXC%" /nologo /T ps_4_0 /Fh glow_bright_PS.h glow_bright_PS.hlsl || exit /b 1
"%FXC%" /nologo /T ps_4_0 /Fh glow_blur_PS.h glow_blur_PS.hlsl || exit /b 1

echo All shaders compiled.
