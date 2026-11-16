@echo off
setlocal enabledelayedexpansion
chcp 936 >nul

:: ============================================================
:: Build helper
::   build.bat [arch] [config] [client]
::     arch    x64 (default) | x86
::     config  release (default) | debug
::     client  also build the CEF desktop client
:: Build dir: build\ninja-<arch>-<config>
:: ============================================================

set "SCRIPT_DIR=%~dp0"

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"

set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=release"

set "WITH_CLIENT=0"
if /i "%~3"=="client" set "WITH_CLIENT=1"

if /i "%ARCH%"=="x64" (
    set "VCVARS=vcvars64.bat"
) else if /i "%ARCH%"=="x86" (
    set "VCVARS=vcvars32.bat"
) else (
    echo [ERROR] unknown arch: %ARCH%  ^(x64 or x86^)
    exit /b 1
)

if /i "%CONFIG%"=="release" (
    set "BUILD_TYPE=Release"
    set "CFG_DIR=release"
) else if /i "%CONFIG%"=="debug" (
    set "BUILD_TYPE=Debug"
    set "CFG_DIR=debug"
) else (
    echo [ERROR] unknown config: %CONFIG%  ^(release or debug^)
    exit /b 1
)

set "BUILD_DIR=%SCRIPT_DIR%build\ninja-%ARCH%-%CFG_DIR%"

echo ============================================================
echo   SRC_DIR  : %SCRIPT_DIR%
echo   BUILD_DIR: %BUILD_DIR%
echo   Arch     : %ARCH%
echo   Config   : %BUILD_TYPE%
echo   Client   : %WITH_CLIENT%
echo ============================================================

:: --- Locate Visual Studio 2019 ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found.
    exit /b 1
)

set "VS_INSTALL_DIR="
for /f "usebackq tokens=*" %%I in (`call "%VSWHERE%" -version "[16.0,17.0)" -property installationPath -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) do (
    if not defined VS_INSTALL_DIR set "VS_INSTALL_DIR=%%I"
)
if not defined VS_INSTALL_DIR (
    echo [ERROR] VS2019 with C++ toolset not found.
    exit /b 1
)
echo VS2019 : !VS_INSTALL_DIR!

call "!VS_INSTALL_DIR!\VC\Auxiliary\Build\%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] failed to init VS environment.
    exit /b 1
)

where ninja >nul 2>&1
if errorlevel 1 (
    echo [ERROR] ninja not found.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%SCRIPT_DIR%." -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DBUILD_CEF_SHELL=%WITH_CLIENT% ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --parallel 2
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [SUCCESS] %BUILD_DIR%
endlocal
exit /b 0
