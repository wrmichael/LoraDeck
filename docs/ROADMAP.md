# LoRaDeck roadmap

## Now
- Second board, first real two-board FT-LoRa QSO (both on 917.000 MHz, clocks from NTP).
- Tolerate clock offsets between stations: file a received packet under the slot it was sent in (arrival time
  minus TX offset and airtime), and tighten the browser time sync.

## Next
- Firmware update from a flasher web page.
- Settings import / copy between boards.
- CW ID beacon.
- LoRa APRS.
- mDNS (loradeck.local).
- **Heltec WiFi LoRa 32 V4** support. Same size and pinout as the V3 and the same SX1262 radio, so most code
  carries over. Differences to handle:
  - A front-end module (PA/LNA) between the SX1262 and the antenna, up to 28 dBm. It differs by revision:
    GC1109 on V4.2, KCT8103L on V4.3. Its enable pin (GPIO2, FEM_EN / PA_CSD) has to be driven, and on V4.2 also
    a PA control pin (GPIO46), or the board will hardly transmit or receive. Check the V4.3 schematic.
  - The power setting has to allow for the PA's gain: the SX1262 setting is no longer the output power. Show the
    real output power (up to about 0.6 W) on the page.
  - 16 MB flash (a bigger partition table and more log space) and 2 MB PSRAM (8 MB on the V4-R8).
  - Vext control is GPIO36 on the V4 and GPIO40 on the V4-R8 (OLED power). The LED pin moves too. There are solar
    and GNSS connectors (GNSS gives time and grid, as on the Cardputer).
  - Choose the board in the build (sdkconfig / Kconfig) or detect it at start, with one firmware image per board
    in the flasher.
- Other boards: M5Stack Cardputer-Adv + Cap LoRa-1262 (SX1262 on G3/G4/G6/G40/G14/G39/G5, ST7789 screen,
  keyboard, GPS for time and grid).

## FT-MESH (new mode, planned)
Same idea as FT-LoRa (FT8 77-bit messages, automatic logged QSOs, even/odd slots), but **every station is also a
repeater**, so two stations out of direct range can still complete a QSO through others.

Working design:
- **Separate channel or packet marker** from FT-LoRa (e.g. magic byte 0xF2), so FT-LoRa stations never see relays.
- **Packet** = FT8 payload (10 bytes) + small header: message id (16 bits, hash of payload + original slot),
  hops left (TTL, default 3), hops taken. Still well under 20 bytes.
- **Longer slots** (setting, e.g. 15 s) split into **relay windows**: the originator sends in window 0, first-hop
  repeats in window 1, and so on, with the reply slot after the last window. Slot length = (max hops + 1) × window
  + margin; at SF9/BW125 a packet is about 0.2 s, so 1 s windows leave room for several repeaters.
- **Managed flooding**: each station repeats a message id once (duplicate cache), only while TTL > 0, and waits a
  delay inside the window that is shorter the weaker it heard the packet (distant stations go first, extending
  range); a station that hears the same id repeated by someone else before its turn stays quiet.
- **Clock**: all stations share the slot schedule, so time accuracy matters more than in FT-LoRa (NTP, GPS, or
  offset-tolerant receive).
- **Log**: report is the SNR of the best copy heard; relay calls/hop count go in the ADIF COMMENT.
- **Rules to check** before release (US Part 97): relaying others' messages unattended is repeater /
  automatically controlled digital station operation (§97.205, §97.221, §97.109); each relaying station must still
  identify (§97.119) — e.g. its call in a periodic ID packet or the CW ID beacon.

## Open question: what to call the mode in logs
On the air (FCC emission) both modes are **spread spectrum** (LoRa is chirp spread spectrum, §97.311). ADIF has no
LoRa mode; nearest are `PKT` (packet) and `CHIP` (a specific direct-sequence spread-spectrum mode, not LoRa).
Options: keep `FT-LORA` / `FT-MESH` as MODE (clear, but LoTW and some loggers reject unknown modes unless mapped to
DATA in TQSL), or a standard MODE with SUBMODE `FT-LORA` / `FT-MESH` (ADIF does not validate SUBMODE). The ADIF
mode is already a setting (`adifMode`).
