@echo off
rem LoRaDeck: get the newest code from GitHub, build it and flash the Heltec V3 on USB.
rem Put this repository in C:\LoraDeck (git clone https://github.com/wrmichael/LoraDeck C:\LoraDeck).
cd /d "%~dp0.."
git checkout -- .
git pull
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'; Set-Location '%~dp0..'; if (!(Test-Path sdkconfig)) { idf.py set-target esp32s3 2>&1 | Tee-Object -FilePath tools\flash.log }; idf.py build flash 2>&1 | Tee-Object -FilePath tools\flash.log -Append"
echo.
echo Finished. See tools\flash.log
timeout /t 15
