// Morse code for the CW (keyed carrier) transmissions: CW ID and CW messages.
#pragma once
#include <string>

// Dots and dashes for one character (".-"), or nullptr if it has none. Space is a word gap.
const char* morseOf(char c);
// Length of a text in Morse units (dot = 1, dash = 3, gaps 1 / 3 / 7), with characters that have no code left out.
int morseUnits(const std::string& text);
// Milliseconds to send a text at this speed (PARIS: 50 units a word, a unit = 1200 / wpm ms).
inline int morseMs(const std::string& text, int wpm) { return morseUnits(text) * 1200 / (wpm > 0 ? wpm : 20); }
// The character for dots and dashes (".-" -> 'A'), or 0.
char morseChar(const std::string& code);
// Upper case, only characters that have a code, single spaces.
std::string morseClean(const std::string& text);
