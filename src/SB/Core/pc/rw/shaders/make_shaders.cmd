@echo off
rem Regenerates the checked-in shader blobs from their .hlsl sources.
rem
rem Same arrangement as librw's own shaders (third_party/librw/src/d3d/shaders):
rem the compiled blob is checked in, so the build needs no shader compiler and
rem nobody has to have a DirectX SDK to build the port. Run this only when the
rem .hlsl changes, and commit the .h with it.
rem
rem fxc comes with the Windows SDK; adjust the version if yours differs. The
rem blob it emits is called g_ps20_main, which is what distort.cpp expects.

set FXC="%ProgramFiles(x86)%\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"

%FXC% /nologo /T ps_2_0 /Fh distort_PS.h distort_PS.hlsl

%FXC% /nologo /T ps_2_0 /Fh glow_bright_PS.h glow_bright_PS.hlsl

%FXC% /nologo /T ps_2_0 /Fh glow_blur_PS.h glow_blur_PS.hlsl
