# Build a LoRaDeck release for every board: complete .bin files for the browser flasher (docs/) and esptool,
# and for the Cardputer also the program alone, for an SD card (M5Launcher).
# Run tools\release.bat (it loads ESP-IDF first). Put the new version number in version.txt before.
#
#   tools\release.bat            Beta
#   tools\release.bat stable     Beta and Stable (the flasher's default channel)
#
# Heltec V3:     build\                 docs\firmware\loradeck.bin               docs\manifest.json
# Cardputer-Adv: build-cardputer\       docs\cardputer\firmware\loradeck-cardputer.bin (+ -app.bin for the SD card)
#                                       docs\cardputer\manifest.json
# Stable copies: docs\stable\ and docs\cardputer\stable\.
#
# A complete image holds bootloader (0x0), partition table (0x8000), blank update-slot selector (0xE000) and the
# program (0x20000) and ends there, so the log and settings partition (0x3E0000) is never touched.
# The blank selector makes a board start the version just written, even after web updates.
param([string]$channel = "beta")
$ErrorActionPreference = "Continue"   # idf.py and esptool write progress to stderr; failures are checked by exit code
Set-Location -Path (Join-Path $PSScriptRoot "..")
$root = Get-Location
$ver = (Get-Content version.txt -TotalCount 1).Trim()

function Manifest($name, $path) {
@"
{
  "name": "$name",
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
function WriteText($rel, $text) { [System.IO.File]::WriteAllText((Join-Path $root $rel), $text) }   # UTF-8, no BOM

# board, build folder, sdkconfig file, project name (= program file name), docs folder ("" or "cardputer\"), title
$boards = @(
    @("heltec_v3",     "build",           "sdkconfig",           "loradeck",           "",            "LoRaDeck (Heltec V3)"),
    @("cardputer_adv", "build-cardputer", "sdkconfig.cardputer", "loradeck-cardputer", "cardputer\",  "LoRaDeck (Cardputer-Adv)")
)
foreach ($b in $boards) {
    $board, $dir, $sdk, $proj, $docs, $title = $b
    Write-Host "Building LoRaDeck $ver for $board ..." -ForegroundColor Cyan
    $idfArgs = @("-B", $dir, "-D", "LORADECK_BOARD=$board", "-D", "SDKCONFIG=$sdk", "-D", "IDF_TARGET=esp32s3")
    idf.py @idfArgs reconfigure | Out-Null               # picks up a new version.txt
    idf.py @idfArgs build
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $board" }

    # The version inside the program must be the one published (the board shows it and compares it).
    $app = [System.IO.File]::ReadAllBytes((Join-Path $root "$dir\$proj.bin"))
    $inside = [System.Text.Encoding]::ASCII.GetString($app, 48, 32).Trim([char]0)
    if ($inside -ne $ver) { throw "The $board program says it is ${inside}, version.txt says ${ver}; nothing published (run it again)" }
    Write-Host "Program version (${board}): $inside" -ForegroundColor Green

    $out = "$proj-$ver.bin"
    Push-Location $dir
    try {
        python -m esptool --chip esp32s3 merge-bin -o $out "@flash_args"
        if ($LASTEXITCODE -ne 0) { throw "merge-bin failed for $board" }
    } finally { Pop-Location }

    New-Item -ItemType Directory -Force -Path "docs\${docs}firmware" | Out-Null
    Copy-Item -Force "$dir\$out" "docs\${docs}firmware\$proj.bin"
    if ($board -eq "cardputer_adv") { Copy-Item -Force "$dir\$proj.bin" "docs\${docs}firmware\$proj-app.bin" }
    WriteText "docs\${docs}manifest.json" (Manifest $title "firmware/$proj.bin")
    if ($channel -eq "stable") {
        New-Item -ItemType Directory -Force -Path "docs\${docs}stable" | Out-Null
        Copy-Item -Force "$dir\$out" "docs\${docs}stable\$proj.bin"
        if ($board -eq "cardputer_adv") { Copy-Item -Force "$dir\$proj.bin" "docs\${docs}stable\$proj-app.bin" }
        WriteText "docs\${docs}stable\manifest.json" (Manifest $title "$proj.bin")
    }
    $size = (Get-Item "$dir\$out").Length
    Write-Host "Done: $dir\$out ($([math]::Round($size/1MB,2)) MB)" -ForegroundColor Green
}
Write-Host ""
Write-Host "LoRaDeck $ver for both boards is on the flasher as $channel" -ForegroundColor Green
