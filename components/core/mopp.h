// CW in LoRa packets: the MOPP format ("Morse over packet", github.com/Morse-Code-over-IP/protocol-mopp),
// as CW transceivers over LoRa (e.g. the Morserino-32) send it: a 14-bit header (version 01, 6-bit serial
// number, 6-bit speed in wpm), then 2 bits per element: 01 dit, 10 dah, 00 end of character, 11 end of word
// (in place of the last end of character). MSB first, the last byte padded with zero bits.
// The first byte is 0x40..0x7F, so it never looks like LoRaDeck's own packets (0xC1, 0xF1, 0xF2).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

std::vector<uint8_t> moppEncode(const std::string& text, int wpm, int serial);
// False if it is not MOPP or a character has no Morse code.
bool moppDecode(const uint8_t* d, int len, std::string& text, int& wpm, int& serial);
inline bool looksMopp(const uint8_t* d, int len) { return len >= 3 && (d[0] >> 6) == 1; }

// FT-CW (as RigDeck): a message with a two-character check group, "CQ AC9HP EM69" -> "CQ AC9HP EM69 7Q".
std::string cwCheck(const std::string& text);
std::string cwFrame(const std::string& text);
bool cwUnframe(const std::string& framed, std::string& text);   // verifies and strips the check group
