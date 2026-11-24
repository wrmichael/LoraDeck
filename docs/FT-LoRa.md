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

- A receiver files a packet under the slot it was sent in: arrival time - time on air - 150 ms, rounded to the
  nearest slot start. So stations whose clocks differ by up to almost half a slot still work each other and keep
  to their own even/odd slots; the difference shows as DT (seconds) in the list.

## FT-MESH (packet 0xF2, 14 bytes)
Same messages and sequence, but every station also repeats what it hears.

| Byte | Content |
|---|---|
| 0 | `0xF2` |
| 1 | hops left (high 4 bits), hops taken (low 4 bits) |
| 2-3 | message id (16 bits, a hash of the payload and the slot), the same in every copy |
| 4-13 | the 77-bit FT8 payload, as bytes 1-10 of FT-LoRa |

- Slots of `meshSlotSec` seconds (default 15). The originator sends 150 ms into its slot (window 0); copies that
  have taken *n* hops go in window *n*, each window `W = max(500 ms, 2 x time on air + 150 ms)` long.
- A station repeats a message once (hops left - 1, hops taken + 1), at a delay inside its window that is shorter
  the weaker it heard the message, and drops its repeat if it hears another copy first. Messages addressed to the
  station itself are not repeated. The originator sets hops left to the hops that fit in the slot (at most 5).
- Repeating is a setting: off, while transmit is enabled, or always.
- Receivers take the first copy of each id; the hop count shows as "via n".

## Terminal text (packet 0xC1)
`0xC1`, hop byte, 16-bit id (as FT-MESH), then up to 120 ASCII characters. Sent in the sender's own slot after its
FT message. In FT-LoRa the hop byte is 0 (not repeated); in FT-MESH it is repeated like the FT messages. LoRaDeck
ends each message with "&lt;to&gt; DE &lt;sender&gt;" unless the text already holds the sender's call.

## CW
Real CW: the SX1262 keys an unmodulated carrier on the channel frequency, in the quiet part of the sender's own
slot (after the packets and, in FT-MESH, the relay windows). Used for the CW ID ("DE &lt;call&gt;", every
`cwIdMin` minutes while the station transmits, sped up to fit the slot if needed) and for CW messages from the
Terminal. It is copied by ear on any 33 cm receiver; LoRaDecks do not decode it.

## Signal reports
The report field carries the LoRa packet SNR in dB (rounded), range -24..+20 like FT8.

## Reference implementation
github.com/wrmichael/LoraDeck (MIT): components/core/ftmsg.cpp (packing), components/core/app.cpp (timing).
