@echo off
setlocal
set "ROOT=%~dp0.."
set "SRC=%ROOT%\build-client"
set "DIST=%ROOT%\dist"
set "APP=%DIST%\EPH205"

if not exist "%SRC%\EPH205.exe" (
    echo [ERROR] run tools\build_client.bat first
    exit /b 1
)

if exist "%APP%" rmdir /s /q "%APP%"
mkdir "%APP%" || exit /b 1
mkdir "%APP%\www" || exit /b 1
mkdir "%APP%\data" || exit /b 1
mkdir "%APP%\exports" || exit /b 1

copy /y "%SRC%\EPH205.exe" "%APP%\" >nul || exit /b 1
call :copy "%SRC%" "%APP%" libcef.dll
call :copy "%SRC%" "%APP%" chrome_elf.dll
call :copy "%SRC%" "%APP%" libEGL.dll
call :copy "%SRC%" "%APP%" libGLESv2.dll
call :copy "%SRC%" "%APP%" d3dcompiler_47.dll
call :copy "%SRC%" "%APP%" vk_swiftshader.dll
call :copy "%SRC%" "%APP%" vk_swiftshader_icd.json
call :copy "%SRC%" "%APP%" vulkan-1.dll
call :copy "%SRC%" "%APP%" icudtl.dat
call :copy "%SRC%" "%APP%" snapshot_blob.bin
call :copy "%SRC%" "%APP%" v8_context_snapshot.bin
call :copy "%SRC%" "%APP%" chrome_100_percent.pak
call :copy "%SRC%" "%APP%" chrome_200_percent.pak
call :copy "%SRC%" "%APP%" resources.pak

xcopy /e /i /q /y "%SRC%\locales" "%APP%\locales" >nul || exit /b 1
xcopy /e /i /q /y "%ROOT%\www\v1" "%APP%\www\v1" >nul || exit /b 1

if exist "%DIST%\EPH205-client.zip" del /q "%DIST%\EPH205-client.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%APP%\*' -DestinationPath '%DIST%\EPH205-client.zip' -CompressionLevel Optimal"

echo [OK] %APP%
endlocal
exit /b 0

:copy
if exist "%~1\%~3" copy /y "%~1\%~3" "%~2\" >nul
exit /b 0
