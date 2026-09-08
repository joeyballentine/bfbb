@echo off
rem Debug build of the PC port. Optional arguments: the render
rem backends and the architecture (x86 is the default, x64 the other).
rem
rem With no backend argument the executable carries D3D9 and GL3, and
rem video.backend in config.ini picks between them. Name one -- D3D9, D3D11,
rem GL3 or NULL -- to build only that.
rem
rem     build-debug.bat
rem     build-debug.bat GL3
rem     build-debug.bat D3D9 x64
rem
rem Puts bfbb.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Debug %1 %2
