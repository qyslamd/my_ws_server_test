@echo off
:: thin wrapper: debug build
::   build.bat <arch> <config>
call "%~dp0..uild.bat" x64 debug
exit /b %errorlevel%
