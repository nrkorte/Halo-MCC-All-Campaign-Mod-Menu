@echo off
rem Builds Toggle-HaloModMenu.exe using the C# compiler that ships with Windows,
rem then copies it to the Desktop. The HaloModMenu project path is baked in so
rem the exe can find the latest build from anywhere.
setlocal

for %%i in ("%~dp0..\..") do set "PROJECT_DIR=%%~fi"

> "%~dp0BuildInfo.cs" echo static class BuildInfo { public const string ProjectDir = @"%PROJECT_DIR%"; }

"%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /platform:x64 /optimize+ ^
  /win32manifest:"%~dp0app.manifest" /r:System.Windows.Forms.dll ^
  /out:"%~dp0Toggle-HaloModMenu.exe" "%~dp0ToggleHaloModMenu.cs" "%~dp0BuildInfo.cs" || exit /b 1

copy /y "%~dp0Toggle-HaloModMenu.exe" "%USERPROFILE%\Desktop\Toggle-HaloModMenu.exe" >nul || exit /b 1
echo Built and copied to %USERPROFILE%\Desktop\Toggle-HaloModMenu.exe
