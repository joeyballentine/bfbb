@echo off
rem Debug build of the PC port. Optional arguments: the render
rem backend (D3D9 is the default, GL3 and NULL are the others) and the
rem architecture (x86 is the default, x64 the other).
rem
rem     build-debug.bat
rem     build-debug.bat GL3
rem     build-debug.bat D3D9 x64
rem
rem Puts bfbb.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Debug %1 %2
