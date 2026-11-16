@echo off
setlocal
set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build-debug"
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

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --parallel 2
