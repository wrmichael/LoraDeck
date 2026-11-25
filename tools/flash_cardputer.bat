@echo off
rem LoRaDeck for the M5Stack Cardputer-Adv + Cap LoRa-1262: pull, build and flash over USB.
rem The Cardputer's USB-C is the ESP32-S3's own USB; if it is not found, switch it off, hold G0 (the button
rem on the side) while plugging in, and run this again. Log in tools\flash_cardputer.log.
rem It runs from a copy in %TEMP%: the pull may change this file, and Windows reads a batch file while it runs.
if not "%~1"=="--copy" (
    copy /y "%~f0" "%TEMP%\loradeck_flash_cardputer.bat" >nul
    "%TEMP%\loradeck_flash_cardputer.bat" --copy "%~dp0.."
    goto :eof
)
cd /d "%~2"
git checkout -- .
git pull
if errorlevel 1 (
    echo git pull failed: nothing built.
    timeout /t 15
    goto :eof
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'; idf.py -B build-cardputer -D LORADECK_BOARD=cardputer_adv -D SDKCONFIG=sdkconfig.cardputer -D IDF_TARGET=esp32s3 build flash 2>&1 | Tee-Object -FilePath tools\flash_cardputer.log"
timeout /t 15
