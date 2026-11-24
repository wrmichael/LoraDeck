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
- Slots 5 s even/odd (setting, all stations alike). Clock: GPS+PPS > NTP > GPS > browser (clk::Source,
  mayOverride: a better source set within 30 min wins). RX files a packet under its sent slot (arrival - air -
  150 ms, nearest slot): clocks may differ by almost half a slot; late TX (> 650 ms into the slot) is skipped.
- Modes (2026-11-23): FT-LoRa (0xF1, 11 bytes), FT-MESH (0xF2, 14 bytes, every station repeats, 15 s slots,
  relay windows, weakest first, suppression), Terminal text (0xC1, "<to> DE <me>" appended), real CW (SX1262
  SetTxContinuousWave keying) for CW ID and Terminal CW; FT-CW (ftMode 2: FT messages + RigDeck check group as
  Morse in MOPP packets, first byte 0x40-0x7F), CW over LoRa on the Terminal (MOPP, Morserino style), browser
  plays received CW (WebAudio); ADIF FT-LORA / FT-MESH / FT-CW / LORA-CHAT / LORA-CW / CW.
- Station call vs operator call (ADIF OPERATOR). Wayne uses club call W9AIM on the second board.
- Default 917.000 MHz SF9 BW125 CR4/5 14 dBm: inside the ARRL 33 cm 915-921 MHz spread-spectrum block, clear of
  Meshtastic LongFast (906.875) and LoRaWAN. Hams under Part 97: no 500 kHz minimum (that is §15.247, unlicensed).
  No encryption; the protocol is published. Settings warn when the signal crosses 902/928 MHz.
- Default hotspot LoRaDeck-XXXXXX (MAC suffix), password loradeck1, 192.168.4.1.

## Code map
- components/core (host-testable): nmea (RMC/GGA, Maidenhead), morse, mopp (MOPP codec, cwFrame/cwCheck), app (slots, TX queue, mesh relay, chat/CW, sequencer glue, ADIF log /data/<CALL>_log.adi, state JSON, commands),
  qso (sequencer), settings (key=value /data/settings.txt), ftmsg (pack/unpack), clock.
- components/ft8msg: ft8_lib message.c/text.c (MIT).
- main: sx1262 (SPI driver; pins NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14; TCXO 1.8 V on DIO3; RF
  switch on DIO2), oled (SSD1306 0x3C, SDA 17, SCL 18, RST 21, Vext GPIO 36 low), net, web, ota (/api/otachunk),
  main (FATFS "storage" at /data, UDP ADIF forwarding), gps (UART1 NMEA, PPS ISR), conlog (console log tee → /data/logs/console.txt, 128 KB
  then console.old.txt; /api/console, Console tab). App::trace hook = one log line per TX/RX/clock/command.
- Web page: edit main/web/index.src.html, then `python3 tools/build_web.py` (inlines the font) → index.html.
- Host tests: `sh test/build.sh`: packing, NMEA/locator, Morse, two stations (also with clocks ±2 s off, half
  duplex, even/odd discipline), three-station FT-MESH through a relay, chat, CW, Terminal log.
- Web flasher: docs/ (GitHub Pages, ESP Web Tools; Beta docs/manifest.json, Stable docs/stable/), built by
  tools\release.bat on Wayne's PC. Board page: Install latest Stable/Beta from wrmichael.github.io/LoraDeck.
- No ESP-IDF in the cloud session: board code is only stub-syntax-checked there; Wayne's build is the real test.
  Run `sh test/check_board.sh` after changing main/*.cpp: compiles main, sx1262, conlog, gps against
  test/idf_stubs with -Werror and the newest C++ (the real build is -Werror, gnu++26: e.g. ++ on a volatile and
  snprintf truncation are errors there; 0.2.10's first release failed on both).

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
- Two-board test 2026-11-23: no contact because board 1 was on 915.000 (fixed), then because both had AC9HP (a
  station ignores messages from its own call). 0.1.3: second board as AC9HP/P works (host test does exactly
  that); "/" in the call becomes "_" in the log file name; warning when another station sends as our call.
- **First real two-board FT-LoRa QSO 2026-11-23**: AC9HP and club call W9AIM, Auto CQ / Auto hunt, full sequence
  and log. Second board done.
- 2026-11-23 evening: 0.2.x running on both boards, "things seem to be working better". Wayne is recruiting more
  users for a wider test of this version. When reports come in: ask for the Console log download from each board.
- 0.2.11 (2026-11-24): the board whose settings would not save shows Storage "0 KB free of 4148 KB", also after a
  full erase (4148 KB total is right: 0x420000 partition minus wear levelling). Something fills it, or free is
  misreported: added /api/files + Console "Show what is in the storage" (paths, sizes) to find out. Waiting for
  that list and the console log. (Wayne pasted an answer from another chat about LittleFS/SPIFFS/4 MB flash:
  not LoRaDeck; it is ESP-IDF FATFS on 8 MB.) max_uri_handlers 10 -> 16 (was at the limit).
- 0.2.10: Wayne: "the log is empty on this unit, it should have 2 QSOs" (the unit whose settings would not save).
  Per-call logs (<CALL>_log.adi): a call change starts an empty log. Bug: a failed save still switched cfg in
  memory (new call shown, its empty log) -> now reverted. Log tab "Log for" lists every *_log.adi (/api/logs,
  /api/log?file=NAME), with a note when the current call has none but others do.
- 0.2.9: factory reset (Settings card, closed by default: Settings only / Everything; type RESET + confirm +
  one-time code "resettoken" valid 60 s, one try; App::factoryReset hook -> main doFactoryReset: settings =
  remove files, everything = unmount + erase the storage partition (formatted at the next mount); NVS erased;
  restart). PRG (GPIO0) held 10 s while running = settings reset with an OLED countdown. Log tab / README say
  where the log lives (flash: survives power-off and updates). Roadmap: SD card for the log (V3 has no slot).
- 0.2.8 (2026-11-24): one node said "Could not save the settings" when changing the call. Cause not known yet
  (suspect: storage full, e.g. the console log not trimmed). Now: Settings::lastError (errno) in the message,
  fallback write in place, Console tab shows storage free (state "fs"), conlog drops old lines below 256 KB free
  and never grows past 128 KB if the rename fails. Asked Wayne for that board's Console log.
- 0.2.7: Terminal messages repeated in every mode (0xC3 text / 0xC4 CW over LoRa with a path of repeater
  calls; Meshtastic-style: after hearing, weakest first, suppression; FT-MESH: in the quiet part after the
  relay windows); "↻n" and tap-for-details (route, SNR/RSSI, time). Fixed: a Terminal message planned in a
  slot first seen mid-way was dropped as late (now never scheduled in the past, and a late one is requeued).
  colPlain setting = bare MOPP (Morserino). Older boards ignore 0xC3/0xC4: update all.
- 0.2.6: Terminal tags TXT / CoL (CW over LoRa) / CW only in the All view; Clear button.
- 0.2.5: Terminal "Show: This mode / All" (default This mode; data-v on lines, localStorage ld_termAll); sound
  for CW over LoRa only when shown, FT-CW sound only in FT-CW mode.
- 0.2.4: Terminal: same message refused while waiting (Wayne tapped CW over LoRa 3 times, got it 3 times),
  waiting list with Cancel / Cancel all (state "waiting", command "cancel"), Send locked during the request,
  macro greyed while its message waits, sendSoon(): early in our own slot (< 700 ms) a message goes in that slot.
- 0.2.3: the 0.2.2 release's program said 0.2.1: CMake read version.txt only when it configured. Now
  CMAKE_CONFIGURE_DEPENDS on version.txt, release.ps1 runs reconfigure and refuses to publish when the version
  inside build\loradeck.bin (app image offset 48) differs from version.txt.
- 0.2.2: Wi-Fi update said "pieces out of order. Start again." (most likely two uploads at once: double tap or
  two tabs). Now: upload id (sid) per upload, the board answers with "have" (where it wants the next piece) and
  the page resumes there; Install buttons locked while sending. Boards on older firmware still have the old
  updater: one tap, one tab, or USB.
- 0.2.1: FT-CW and CW over LoRa (Wayne asked; the Morserino-32 sends CW over LoRa as packets, not a keyed carrier).
- 0.2.0 (2026-11-23, released as Beta, not yet tested on the boards): everything in "Modes" and "Clock" above, GPS, operator call,
  web flasher. Next: test it, enable GitHub Pages, first release; then the board updating itself from the internet
  (esp_https_ota, no browser), settings import/copy, LoRa APRS, mDNS,
  Heltec V4 support. Full list in docs/ROADMAP.md.
