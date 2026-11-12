@echo off
setlocal enabledelayedexpansion
chcp 936 >nul

:: ============================================================
:: 构建 EPH205 桌面客户端（CEF + Win32，Release）
:: 需要 3rdparty\cef-109（不入库），见 docs\CEF客户端方案.md
:: ============================================================

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%..\build-client"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VS_INSTALL_DIR="
for /f "usebackq tokens=*" %%I in (`call "%VSWHERE%" -version "[16.0,17.0)" -property installationPath -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) do (
    if not defined VS_INSTALL_DIR set "VS_INSTALL_DIR=%%I"
)
if not defined VS_INSTALL_DIR (
    echo [ERROR] VS2019 with C++ toolset not found.
    exit /b 1
)
call "%VS_INSTALL_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%SCRIPT_DIR%.." -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DEPH205_BUILD_CLIENT=ON ^
    -DCEF_RUNTIME_LIBRARY_FLAG=/MD ^
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
echo [SUCCESS] %BUILD_DIR%\EPH205.exe
endlocal
