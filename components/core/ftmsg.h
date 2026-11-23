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
    std::string text;
};

// Packet on the air: 1 byte FT-LoRa marker/version + 10 bytes (the 77-bit FT8 payload).
const int FTL_PACKET = 11;
const uint8_t FTL_MAGIC = 0xF1;            // "FT-LoRa, version 1"

bool ftlPack(const std::string& text, uint8_t out[FTL_PACKET]);          // false: cannot be packed
bool ftlUnpack(const uint8_t* in, int len, std::string& text);           // false: not an FT-LoRa packet
