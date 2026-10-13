@echo off
rem Builds build\Release\version.dll using the CMake that ships with Visual Studio.
setlocal

for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with C++ tools not found.
    exit /b 1
)

set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo CMake not found in "%VSDIR%". Install the "C++ CMake tools for Windows" component.
    exit /b 1
)

cd /d "%~dp0"
"%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
"%CMAKE%" --build build --config Release || exit /b 1

echo.
echo Built: %~dp0build\Release\version.dll
