# Build a LoRaDeck release: one complete .bin for the browser flasher (docs/) and for esptool.
# Run tools\release.bat (it loads ESP-IDF first). Put the new version number in version.txt before.
#
#   tools\release.bat            Beta: docs\manifest.json + docs\firmware\loradeck.bin
#   tools\release.bat stable     also copies that release to docs\stable\ (the flasher's default channel)
#
# The image holds bootloader (0x0), partition table (0x8000), blank update-slot selector (0xE000) and the
# program (0x20000) and ends there, so the log and settings partition (0x3E0000) is never touched.
# The blank selector makes a board start the version just written, even after web updates.
param([string]$channel = "beta")
$ErrorActionPreference = "Continue"   # idf.py and esptool write progress to stderr; failures are checked by exit code
Set-Location -Path (Join-Path $PSScriptRoot "..")
$ver = (Get-Content version.txt -TotalCount 1).Trim()
Write-Host "Building LoRaDeck $ver ..." -ForegroundColor Cyan

if (!(Test-Path sdkconfig)) { idf.py set-target esp32s3 }
idf.py reconfigure | Out-Null                         # picks up a new version.txt
idf.py build
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# The version inside the program must be the one published (the board shows it and compares it).
$app = [System.IO.File]::ReadAllBytes((Join-Path (Get-Location) "build\loradeck.bin"))
$inside = [System.Text.Encoding]::ASCII.GetString($app, 48, 32).Trim([char]0)
if ($inside -ne $ver) { throw "The program says it is ${inside}, version.txt says ${ver}; nothing published (run it again)" }
Write-Host "Program version: $inside" -ForegroundColor Green

$out = "loradeck-$ver.bin"
Push-Location build
try {
    python -m esptool --chip esp32s3 merge-bin -o $out "@flash_args"
    if ($LASTEXITCODE -ne 0) { throw "merge-bin failed" }
} finally { Pop-Location }

New-Item -ItemType Directory -Force -Path docs\firmware | Out-Null
Copy-Item -Force "build\$out" docs\firmware\loradeck.bin
function Manifest($path) {
@"
{
  "name": "LoRaDeck",
  "version": "$ver",
  "new_install_prompt_erase": true,
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "$path", "offset": 0 }
      ]
    }
  ]
}
"@
}
[System.IO.File]::WriteAllText((Join-Path (Get-Location) "docs\manifest.json"), (Manifest "firmware/loradeck.bin"))   # UTF-8, no BOM
if ($channel -eq "stable") {
    New-Item -ItemType Directory -Force -Path docs\stable | Out-Null
    Copy-Item -Force "build\$out" docs\stable\loradeck.bin
    [System.IO.File]::WriteAllText((Join-Path (Get-Location) "docs\stable\manifest.json"), (Manifest "loradeck.bin"))
}

$size = (Get-Item "build\$out").Length
Write-Host ""
Write-Host "Done: build\$out ($([math]::Round($size/1MB,2)) MB), on the flasher as $channel" -ForegroundColor Green
