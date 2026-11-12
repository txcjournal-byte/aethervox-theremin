@echo off
rem ===========================================================================
rem  AetherVox - one-click Windows build (VST3 + Standalone) for FL Studio
rem  Double-click this file. It installs what is missing (via winget),
rem  builds the plugin and copies it to C:\Program Files\Common Files\VST3.
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title AetherVox - Windows build

echo.
echo  AETHERVOX  ^|  Heterodyne Theremin  ^|  Windows build
echo  ----------------------------------------------------
echo.

rem ---------- 1) tools -------------------------------------------------------
where winget >nul 2>nul || (
  echo [!] winget neni k dispozici. Nainstaluj "App Installer" z Microsoft Store a spust znovu.
  pause & exit /b 1
)

where git >nul 2>nul || (
  echo [*] Instaluji Git...
  winget install -e --id Git.Git --silent --accept-package-agreements --accept-source-agreements
  set "PATH=%PATH%;%ProgramFiles%\Git\cmd"
)

where cmake >nul 2>nul || (
  echo [*] Instaluji CMake...
  winget install -e --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
  set "PATH=%PATH%;%ProgramFiles%\CMake\bin"
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "HAS_VS="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "HAS_VS=%%i"
)
if not defined HAS_VS (
  echo [*] Instaluji Visual Studio 2022 Build Tools s C++ ^(muze trvat 10-20 minut^)...
  winget install -e --id Microsoft.VisualStudio.2022.BuildTools --accept-package-agreements --accept-source-agreements --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
)

rem ---------- 2) configure + build -------------------------------------------
echo.
echo [*] Konfigurace projektu ^(poprve se stahuje JUCE^)...
cmake -B build -G "Visual Studio 17 2022" -A x64 -DAETHERVOX_BUILD_TESTS=OFF
if errorlevel 1 ( echo [!] Konfigurace selhala. & pause & exit /b 1 )

echo.
echo [*] Kompilace Release...
cmake --build build --config Release --target AetherVox_VST3 AetherVox_Standalone --parallel
if errorlevel 1 ( echo [!] Kompilace selhala. & pause & exit /b 1 )

set "VST3_OUT=%~dp0build\AetherVox_artefacts\Release\VST3\Alpine Theremin.vst3"
set "APP_OUT=%~dp0build\AetherVox_artefacts\Release\Standalone\Alpine Theremin.exe"

rem ---------- 3) install for FL Studio ---------------------------------------
echo.
echo [*] Instaluji VST3 do "%CommonProgramFiles%\VST3" ^(potvrd prosim okno spravce^)...
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-Command','New-Item -ItemType Directory -Force -Path \"$env:CommonProgramFiles\VST3\" | Out-Null; Copy-Item -Recurse -Force -LiteralPath \"%VST3_OUT%\" -Destination \"$env:CommonProgramFiles\VST3\"'"

echo.
echo  HOTOVO.
echo   VST3:        %CommonProgramFiles%\VST3\Alpine Theremin.vst3
echo   Standalone:  %APP_OUT%
echo.
echo  FL Studio: Options ^> Manage plugins ^> Find more plugins, pak v Plugin
echo  databazi najdi "Alpine Theremin" ^(Generators^).
echo.
start "" "%APP_OUT%"
pause
