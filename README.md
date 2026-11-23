# LoRaDeck

**LoRaDeck by AC9HP** — firmware for the **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262 LoRa radio + 0.96" OLED)
that makes automatic, logged ham-radio contacts over LoRa. Sister project of
[RigDeck](https://github.com/wrmichael/RigDeck), which runs FT8 on real HF radios.

## FT-LoRa

The SX1262 cannot receive FT8, FT4 or PSK31 (it never hands over the raw signal), so LoRaDeck uses its own mode,
**FT-LoRa**: the FT8 message format (callsigns, grid, signal report, RR73 — 77 bits) carried in an 11-byte LoRa
packet instead of audio tones. Everything an FT8 operator knows carries over:

- CQ → grid → report → R-report → RR73, the same sequence as FT8, with Auto CQ, Auto hunt and POTA mode.
- Even/odd time slots, **5 seconds** by default (a full QSO in about 25 s). All stations on a channel must use the
  same slot length. The clock comes from the internet (NTP) or from the browser that has the page open.
- Reports are LoRa's packet SNR in dB.
- Contacts are logged in ADIF (mode `FT-LORA`, band from the frequency, e.g. 33cm) and can be sent to a logger
  such as Log4OM by UDP.

## Operating

Everything is done from the **web page**: join the LoRaDeck's Wi-Fi hotspot (`LoRaDeck`, password `loradeck1`) and
open http://192.168.4.1, or add your home network in Settings. The OLED shows the callsign, the clock, the IP
address, the current QSO, the last message heard and the log count.

The first time a browser opens the page it shows the start screen (your callsign in gold, "LoRaDeck by AC9HP");
after that it is on the **About** page.

A floating light shows the transmit state: green = TX on, yellow = past half of the TX watchdog time, red = off or
timed out; it pulses while transmitting and all of it flashes "No link" when the page loses the board. While the
page is open and in view you count as present for the watchdog.

## Radio settings

Frequency (150–960 MHz can be set, but the board's RF parts are built for one band: 863–928 MHz or 470–510 MHz),
spreading factor 7–12 (default 9), bandwidth 125/250/500 kHz, coding rate 4/5–4/8, power up to 22 dBm.
Stations must use the same frequency, spreading factor, bandwidth and slot length to hear each other.

**Rules:** on 902–928 MHz in the US you can operate as a radio amateur on the 33 cm band (LoRa counts as spread
spectrum, allowed above 222 MHz; every FT-LoRa message carries your callsign), or unlicensed under Part 15 within
its limits. Check the rules that apply to you.

## Building

ESP-IDF 6.1, target esp32s3: `idf.py set-target esp32s3`, then `idf.py build flash`. On Windows,
`tools\update_flash.bat` pulls, builds and flashes. After editing `main/web/index.src.html` run
`python tools/build_web.py`. Host tests (no ESP-IDF needed): `sh test/build.sh` — includes two simulated stations
completing a QSO.

Firmware updates later can be installed from the web page (Settings → Update LoRaDeck).

## Licence

MIT (see LICENSE). FT8 message packing from ft8_lib by Kārlis Goba (MIT, components/ft8msg/LICENSE). Audiowide font
by Astigmatic, SIL Open Font License 1.1 (assets/OFL.txt). QSO sequencer from RigDeck.
