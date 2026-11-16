@echo off
:: thin wrapper: the real switch lives in build.bat
::   build.bat <arch> <config> client
call "%~dp0..uild.bat" x64 release client
exit /b %errorlevel%
