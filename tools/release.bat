@echo off
rem LoRaDeck: build a release for the browser flasher (https://wrmichael.github.io/LoraDeck/).
rem   tools\release.bat          Beta channel
rem   tools\release.bat stable   Beta and Stable
rem Put the new version number in version.txt first. Then it asks whether to publish (git commit + push).
cd /d "%~dp0.."
set ch=%1
if "%ch%"=="" set ch=beta
rem Build the newest code from GitHub (keep local helper files under other names, see CLAUDE.md).
git checkout -- .
git pull
if errorlevel 1 (echo git pull failed: nothing built. & timeout /t 15 & goto :eof)
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'; & '%~dp0release.ps1' '%ch%' 2>&1 | Tee-Object -FilePath tools\release.log"
if errorlevel 1 goto :eof
set /p ver=<version.txt
echo.
set /p pub=Publish LoRaDeck %ver% on the flasher page now (git commit and push)? [y/N]
if /i not "%pub%"=="y" goto :eof
git add docs version.txt
git commit -m "Release %ver% (%ch%)"
git push
echo Published. GitHub Pages shows it within a minute or two.
timeout /t 15
