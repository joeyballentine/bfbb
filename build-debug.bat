@echo off
rem Debug build of the PC port. Optional argument: the render backend
rem (D3D9 is the default, GL3 and NULL are the others).
rem
rem     build-debug.bat
rem     build-debug.bat GL3
rem
rem Puts bfbb.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Debug %1
