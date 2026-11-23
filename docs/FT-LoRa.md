# FT-LoRa protocol (version 1)

Published so anyone can receive and decode FT-LoRa transmissions; nothing in it is encrypted or obscured.

## Radio layer
- Semtech LoRa (chirp spread spectrum), explicit header, CRC on, preamble 16 symbols, sync word 0x1424
  ("private network" value, as used by many LoRa projects).
- Default: 917.000 MHz, spreading factor 9, bandwidth 125 kHz, coding rate 4/5, normal IQ. All settable.

## Packet (11 bytes)
| Byte | Content |
|---|---|
| 0 | 0xF1: FT-LoRa, version 1 |
| 1-10 | The 77-bit FT8 message payload, exactly as in FT8/FT4 (WSJT-X "77-bit" message format, packing as in ft8_lib's `ftx_message_encode`), MSB first; the last 3 bits of byte 10 are 0 |

The message texts are the familiar FT8 ones: `CQ AC9HP EM69`, `AC9HP W1AW FN31`, `W1AW AC9HP -08`,
`AC9HP W1AW R-05`, `W1AW AC9HP RR73`, `AC9HP W1AW 73`, and directed CQs such as `CQ POTA AC9HP EM69`.
Every standard message carries the full callsign of the sending station (station identification).

## Timing
- Even/odd slots of `slotSec` seconds (default 5) from UTC; a station sends 150 ms after the start of its slot.
- What was heard in a slot is answered in the next slot, as in FT8. A QSO is CQ / grid / report / R-report /
  RR73 — five overs, about 25 s.

## Signal reports
The report field carries the LoRa packet SNR in dB (rounded), range -24..+20 like FT8.

## Reference implementation
github.com/wrmichael/LoraDeck (MIT): components/core/ftmsg.cpp (packing), components/core/app.cpp (timing).
