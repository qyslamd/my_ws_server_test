@echo off
:: thin wrapper: debug build
::   build.bat <arch> <config>
call "%~dp0..\..\build.bat" x64 debug
exit /b %errorlevel%
