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
rem Puts bfbb.exe, the DLLs it needs and bfbb_config.exe in bin\.
rem
rem The configurator is drawn in Win32 controls by default. For the wxWidgets
rem one instead, which is the same window on every host:
rem
rem     set BFBB_CONFIG_UI=wx
rem     set BFBB_WX=vendored
rem
rem See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Debug %1 %2
