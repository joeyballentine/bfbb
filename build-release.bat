@echo off
rem Release build of the PC port. Optional argument: the render backend
rem (D3D9 is the default, GL3 and NULL are the others).
rem
rem     build-release.bat
rem     build-release.bat GL3
rem
rem This is the one to playtest: Debug is 32-bit unoptimised and slow.
rem Puts bfbb.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Release %1
