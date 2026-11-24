// FT-LoRa messages: the FT8 77-bit message format (callsigns, grid, report, RR73...) carried in a
// LoRa packet instead of audio tones.
#pragma once
#include <cstdint>
#include <string>

// One message heard on the air (from one LoRa packet).
struct Decode {
    uint32_t id = 0;
    int64_t slotMs = 0;        // UTC ms of the start of the slot it was heard in
    int parity = 0;            // 0 = even slot, 1 = odd
    int snr = 0;               // dB (LoRa packet SNR)
    int rssi = 0;              // dBm
    float dt = 0;              // seconds after the slot start
    float freq = 0;            // unused for LoRa (kept for the sequencer)
    int kind = 0;              // 0 FT message, 1 chat (Terminal) text, 2 keyed CW we sent, 3 a relay we sent,
                               // 4 CW over LoRa (MOPP, Terminal)
    int wpm = 0;               // MOPP packets (FT-CW, CW over LoRa): the sender's speed, for playing it
    std::string path;          // Terminal messages: the stations that repeated it, in order
    int hops = 0;              // FT-MESH / chat: how many stations repeated it before we heard it
    std::string text;
};

// Packet on the air: 1 byte FT-LoRa marker/version + 10 bytes (the 77-bit FT8 payload).
const int FTL_PACKET = 11;
const uint8_t FTL_MAGIC = 0xF1;            // "FT-LoRa, version 1"
// FT-MESH: 0xF2, hop byte (hops left << 4 | hops taken), 16-bit message id, the 10 payload bytes.
const uint8_t FTM_MAGIC = 0xF2;
const int FTM_PACKET = 14;
// Chat (Terminal) text: 0xC1, hop byte, 16-bit message id, up to CHAT_MAX characters of text.
const uint8_t CHAT_MAGIC = 0xC1;
const int CHAT_MAX = 120;
// Repeatable Terminal packets: magic, hop byte, 16-bit id, path length, path (the calls of the stations that
// repeated it, "W9AIM,KC9RRR"), then the text (0xC3) or a MOPP CW-over-LoRa packet (0xC4).
const uint8_t TERM_MAGIC = 0xC3, COLR_MAGIC = 0xC4;
const int PATH_MAX = 40;

bool ftlPack(const std::string& text, uint8_t out[FTL_PACKET]);          // false: cannot be packed
bool ftlUnpack(const uint8_t* in, int len, std::string& text);           // false: not an FT-LoRa packet
