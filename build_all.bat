@echo off
rem Builds both architectures, stages the layout and compiles the installer.
rem All the work is in tools\build_all.ps1; this is the double-clickable way to run it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_all.ps1" %*
