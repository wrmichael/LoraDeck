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

## FT-MESH

The same FT messages and automatic QSOs, but **every station is also a repeater**: two stations that cannot hear
each other still complete a QSO through others. Each message carries a hop count and an id; a station repeats
it once, in relay windows after the sender's, the weakest-heard first, and stays quiet when another station
already repeated it. Slots are longer (15 s by default) to leave room for the repeats; three hops by default.
All stations on the channel must use the same mode and slot length. Repeating for others is a setting: off,
while TX is enabled (you at the page), or always (unattended; check the automatic-control rules, §97.109,
§97.205, §97.221). Repeating stations identify with the CW ID. Contacts are logged as `FT-MESH`.
Protocol: docs/FT-LoRa.md.

## FT-CW

FT8-style automatic contacts with the messages **in Morse code**, like RigDeck's FT-CW: the same sequence
(CQ, grid, report, R-report, RR73), Auto CQ / Auto hunt, tap to call, each message with RigDeck's two-character
check group (`CQ AC9HP EM69 7Q`). On the air it is a LoRa packet carrying the Morse in the MOPP format (see
below), so it decodes as reliably as FT-LoRa, and with **Sound** on (Terminal tab) the page plays each message
as CW at the FT-CW speed (default 40 wpm). Choose it in Settings → Mode; all stations alike. Logged as `FT-CW`.

## Terminal (chat and CW)

The **Terminal** tab: type and send.
- **LoRa text**: up to 120 characters in a LoRa packet, sent in your next slot; other LoRaDecks show it (and in
  FT-MESH repeat it).
- **CW over LoRa**: your text as Morse in a LoRa packet, in the open MOPP format ("Morse over packet",
  github.com/Morse-Code-over-IP/protocol-mopp), the way CW-over-LoRa devices such as the Morserino-32 send it,
  with your speed in the packet. Other LoRaDecks show it, and with **Sound** on their browser plays it at your
  speed (the board has no speaker). A Morserino works on 70 cm, so it cannot hear a 33 cm LoRaDeck; the format
  is the same.
- **CW (keyed)**: real CW, the radio's carrier keyed on and off on the channel frequency, at the CW speed in
  Settings; copy it with any 33 cm receiver. LoRaDecks cannot decode CW.
- Every message ends with **"&lt;to&gt; DE &lt;your call&gt;"** (or "DE &lt;your call&gt;" when no To call is set),
  unless it already contains your call. The To call fills in from the last "DE" call heard.
- Six macro buttons, edited in Settings ({MY} = your call, {TO} = the other station).
- **Log this contact**: call, mode (LoRa text `LORA-CHAT`, CW over LoRa `LORA-CW`, or keyed `CW`), RST sent/received, name,
  grid, note. Time on = the first message, time off = now.

The **CW ID** ("DE &lt;call&gt;" in CW) goes out every 10 minutes (setting) while the station transmits, in the
quiet end of its own slot.

## Clock

The slots need the stations' clocks to agree; LoRaDeck works up to almost half a slot apart (2.5 s with 5 s
slots) and shows each sender's offset as DT. Sources, best first: a **GPS module with PPS**, **internet time
(NTP)** when on the home network (asked as soon as it connects, then every 15 minutes), a **GPS module** alone,
then the **browser** that has the page open (corrected for the network delay). A better source set in the last
30 minutes is not overridden by a worse one. The header and the OLED show the source (OLED: N = NTP, G = GPS,
P = GPS+PPS, B = browser).

**GPS module (optional):** any NMEA module (NEO-6M, GT-U7, ATGM336H...) on two free pins, e.g. the module's
TXD to GPIO 48 and RXD to GPIO 47 (as in Meshtastic's V3 guide), VCC to 3V3, GND to GND; optionally its PPS pin
to another free GPIO. Set the pins and baud rate in Settings → GPS and restart. With "Grid locator from the GPS
position" on, the grid follows the GPS (6 characters).

## Station and operator call

**Station callsign** is the call on the air (e.g. a club call, W9AIM); **Operator callsign** is yours (AC9HP)
when they differ: logged as ADIF `OPERATOR`, with `STATION_CALLSIGN` the station call.

## Operating

Everything is done from the **web page**: join the LoRaDeck's Wi-Fi hotspot (`LoRaDeck-XXXXXX`, the last six characters of the board's MAC address, shown on the OLED; password `loradeck1`) and
open http://192.168.4.1, or add your home network in Settings. The OLED shows the callsign, the clock, the IP
address, the current QSO, the last message heard and the log count.

The first time a browser opens the page it shows the start screen (your callsign in gold, "LoRaDeck by AC9HP");
after that it is on the **About** page.

A floating light shows the transmit state: green = TX on, yellow = past half of the TX watchdog time, red = off or
timed out; it pulses while transmitting and all of it flashes "No link" when the page loses the board. While the
page is open and in view you count as present for the watchdog.

## Console log

The **Console** tab shows the radio settings in use, packets sent, received and received with errors, and the
console log: everything LoRaDeck prints (each packet sent with its frequency and time on air, each packet heard
with RSSI/SNR and its text, packets that are not FT-LoRa, CRC errors, clock changes, commands from the page), like
RigDeck's console log to the card. It is kept in the board's flash in `/data/logs/` and capped at about 256 KB
(two 128 KB files, the oldest lines go first). **Download** saves the whole log; **Clear** empties it. The OLED's
bottom-but-one line shows the counters: `T` sent, `R` received, `E` received with errors, last RSSI.

## Radio settings

Frequency (default **917.000 MHz**, in the 915–921 MHz spread-spectrum block of the ARRL 33 cm band plan, away
from Meshtastic's default and LoRaWAN; 150–960 MHz can be set, but the board's RF parts are built for one band: 863–928 MHz or 470–510 MHz),
spreading factor 7–12 (default 9), bandwidth 125/250/500 kHz, coding rate 4/5–4/8, power up to 22 dBm.
Stations must use the same frequency, spreading factor, bandwidth and slot length to hear each other.

**Rules (US, 33 cm, 902–928 MHz):** LoRa is spread spectrum, which Part 97 allows above 222 MHz (§97.305,
§97.311) with up to 10 W (the Heltec gives at most 0.16 W). Nothing is encrypted: FT-LoRa is an open, published
format (docs/FT-LoRa.md), so it does not obscure the meaning of messages (§97.113(a)(4)), and every standard
message carries your full callsign, which identifies the station in the mode itself (§97.119). The whole signal,
frequency ± half the bandwidth, must stay inside 902–928 MHz; the Settings page warns when it does not. 33 cm is
a secondary allocation (do not interfere with primary users) and is restricted in parts of Colorado and Wyoming
(§97.303). Auto CQ / Auto hunt run with you at the web page as control operator, with the TX watchdog. Or use the
board unlicensed under Part 15 within its limits. This is a summary, not legal advice — check the current rules.

## Installing and updating

**Browser flasher:** open **https://wrmichael.github.io/LoraDeck/** in Chrome or Edge, connect the board by USB,
pick Stable or Beta and press **Install LoRaDeck** (leave "Erase device" unticked to keep the log and
settings). **Over Wi-Fi:** Settings → Update LoRaDeck → **Install latest Stable / Beta** (the browser needs
internet), or **Install this file** with a downloaded .bin.

**Making a release (maintainer):** put the version in `version.txt`, run `tools\release.bat` (Beta) or
`tools\release.bat stable` (Beta and Stable). It builds, merges bootloader, partition table, update-slot
selector and program into one image for address 0x0 (`build\loradeck-x.y.z.bin`, `docs\firmware\`, and
`docs\stable\` for stable), writes the manifests and offers to commit and push. GitHub Pages must be on:
repository Settings → Pages → Deploy from branch `main`, folder `/docs`.

## Building

ESP-IDF 6.1, target esp32s3: `idf.py set-target esp32s3`, then `idf.py build flash`. On Windows,
`tools\update_flash.bat` pulls, builds and flashes. After editing `main/web/index.src.html` run
`python tools/build_web.py`. Host tests (no ESP-IDF needed): `sh test/build.sh` — includes two simulated stations
completing a QSO.

Firmware updates later can be installed from the web page (Settings → Update LoRaDeck).

## Licence

MIT (see LICENSE). FT8 message packing from ft8_lib by Kārlis Goba (MIT, components/ft8msg/LICENSE). Audiowide font
by Astigmatic, SIL Open Font License 1.1 (assets/OFL.txt). QSO sequencer from RigDeck.
