@echo off
rem Release build of the PC port. Optional arguments: the render
rem backend (D3D9 is the default, GL3 and NULL are the others) and the
rem architecture (x86 is the default, x64 the other).
rem
rem     build-release.bat
rem     build-release.bat GL3
rem     build-release.bat D3D9 x64
rem
rem This is the one to playtest: Debug is unoptimised and slow.
rem Puts bfbb.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Release %1 %2
