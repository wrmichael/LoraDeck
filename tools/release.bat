@echo off
rem LoRaDeck: build a release for the browser flasher (https://wrmichael.github.io/LoraDeck/).
rem   tools\release.bat          Beta channel
rem   tools\release.bat stable   Beta and Stable
rem It pulls the newest code from GitHub first (the version number comes from version.txt there).
rem Then it asks whether to publish (git commit + push).
rem
rem It runs from a copy in %TEMP%: the pull may change this file, and Windows reads a batch file while it runs.
if not "%~1"=="--copy" (
    copy /y "%~f0" "%TEMP%\loradeck_release.bat" >nul
    "%TEMP%\loradeck_release.bat" --copy "%~dp0.." %1
    goto :eof
)
cd /d "%~2"
set ch=%3
if "%ch%"=="" set ch=beta
git checkout -- .
git pull
if errorlevel 1 (
    echo git pull failed: nothing built.
    timeout /t 15
    goto :eof
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'; & '%CD%\tools\release.ps1' '%ch%' 2>&1 | Tee-Object -FilePath tools\release.log"
if errorlevel 1 goto :eof
set /p ver=<version.txt
echo.
set /p pub=Publish LoRaDeck %ver% (%ch%) on the flasher page now (git commit and push)? [y/N]
if /i not "%pub%"=="y" goto :eof
git add docs version.txt
git commit -m "Release %ver% (%ch%)"
git push
echo Published. GitHub Pages shows it within a minute or two.
timeout /t 15
