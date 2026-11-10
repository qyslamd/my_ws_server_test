@echo off
setlocal enabledelayedexpansion
chcp 936 >nul

:: ============================================================
:: Build ws demos: mongoose_demo + lws_demo (VS2019 x64 + Ninja)
:: ============================================================

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"
set "BUILD_TYPE=Release"

echo ============================================================
echo   SRC_DIR  : %SCRIPT_DIR%
echo   BUILD_DIR: %BUILD_DIR%
echo   Build    : %BUILD_TYPE%
echo ============================================================

:: --- Locate VS2019 ---
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

:: --- Initialize VS2019 x64 env ---
call "!VS_INSTALL_DIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
if !errorlevel! neq 0 (
    echo [ERROR] Failed to init VS2019 x64 environment.
    exit /b 1
)

:: --- check ninja ---
where ninja >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] ninja not found.
    exit /b 1
)

:: --- Configure ---
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%SCRIPT_DIR%." -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5

if !errorlevel! neq 0 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

:: --- Build ---
cmake --build "%BUILD_DIR%" --parallel 2
if !errorlevel! neq 0 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo ============================================================
echo [SUCCESS] Built in: %BUILD_DIR%
echo ============================================================
endlocal
exit /b 0