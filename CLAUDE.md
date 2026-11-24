# LoRaDeck — notes for working on this repository

Sister project of RigDeck (github.com/wrmichael/RigDeck), kept separate. Owner: Wayne Michael, AC9HP.
Commits: `git -c user.name="Wayne Michael" -c user.email="wrmichael@users.noreply.github.com" commit`.

## What it is
Firmware for the **Heltec WiFi LoRa 32 V3** (ESP32-S3, 8 MB flash, no PSRAM, SX1262, 0.96" OLED, Wi-Fi):
automatic, logged QSOs in the **FT-LoRa** mode (FT8 77-bit messages in 11-byte LoRa packets, docs/FT-LoRa.md).
Everything is operated from the web page; the OLED shows status only.

## Decisions (2026-11-23)
- Names: mode **FT-LoRa** (ADIF MODE "FT-LORA"), device **LoRaDeck**. Branding: web start screen with the callsign
  in gold (Audiowide) and "LoRaDeck by AC9HP" on a browser's first visit, then on the About page.
- Own repo; pieces copied once from RigDeck (QSO sequencer) and ft8_lib (message packing), evolving separately.
- Slots 5 s even/odd (setting, all stations alike). Clock from NTP or the browser.
- Default 917.000 MHz SF9 BW125 CR4/5 14 dBm: inside the ARRL 33 cm 915-921 MHz spread-spectrum block, clear of
  Meshtastic LongFast (906.875) and LoRaWAN. Hams under Part 97: no 500 kHz minimum (that is §15.247, unlicensed).
  No encryption; the protocol is published. Settings warn when the signal crosses 902/928 MHz.
- Default hotspot LoRaDeck-XXXXXX (MAC suffix), password loradeck1, 192.168.4.1.

## Code map
- components/core (host-testable): app (slots, sequencer glue, ADIF log /data/<CALL>_log.adi, state JSON, commands),
  qso (sequencer), settings (key=value /data/settings.txt), ftmsg (pack/unpack), clock.
- components/ft8msg: ft8_lib message.c/text.c (MIT).
- main: sx1262 (SPI driver; pins NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14; TCXO 1.8 V on DIO3; RF
  switch on DIO2), oled (SSD1306 0x3C, SDA 17, SCL 18, RST 21, Vext GPIO 36 low), net, web, ota (/api/otachunk),
  main (FATFS "storage" at /data, UDP ADIF forwarding), conlog (console log tee → /data/logs/console.txt, 128 KB
  then console.old.txt; /api/console, Console tab). App::trace hook = one log line per TX/RX/clock/command.
- Web page: edit main/web/index.src.html, then `python3 tools/build_web.py` (inlines the font) → index.html.
- Host tests: `sh test/build.sh` (two simulated stations complete a QSO).

## Build and flash (Wayne's PC)
- Repo at C:\LoraDeck; ESP-IDF 6.1; `tools\update_flash.bat` pulls, builds (set-target esp32s3 the first time) and
  flashes; log in tools\flash.log (UTF-16). Board on a CP210x port (COM9 for the first board).
- Boot console: tools\capture_now.bat (local copy) or tools\capture.bat → tools\monitor.log.
- An untracked file at a path the pull wants to create makes `git pull` fail and the old code gets built: keep
  local helper files under other names.

## Status
- 0.1.0 compiled and ran on the first board (SX1262 ready, hotspot, web server). 0.1.1: hotspot name with MAC
  suffix. Default frequency changed to 917.000 MHz after that (the first board still has 915.000 saved).
- 0.1.2: console log + radio counters (Console tab, OLED "T R E" line), added while two boards (Auto CQ / Auto
  hunt) did not hear each other; first suspect: board 1 still on 915.000 MHz.
- Next: second board, first real two-board QSO; then firmware update from a flasher web page, settings import/
  copy, CW ID beacon, LoRa APRS, mDNS, Heltec V4 support (FEM/PA enable pins, 16 MB flash). Full list in
  docs/ROADMAP.md.
- Planned new mode **FT-MESH** (2026-11-23): FT-LoRa where every station also repeats (managed flooding, TTL,
  longer slots split into relay windows). Design notes and the open ADIF mode-name question in docs/ROADMAP.md.
