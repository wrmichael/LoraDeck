// Semtech SX1262 LoRa radio on the Heltec WiFi LoRa 32 V3 (SPI, BUSY, DIO1 interrupt, TCXO on DIO3,
// RF switch on DIO2).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

struct LoraConfig {
    uint32_t freqHz = 915000000;
    int sf = 9, bwKhz = 125, cr = 5, powerDbm = 14;
};

namespace sx1262 {
// Start the radio and listen. False if the chip does not answer.
bool begin(const LoraConfig& c);
// Change frequency / modulation / power (goes back to receiving).
void configure(const LoraConfig& c);
// Send one packet (up to 255 bytes); returns at once. False if busy sending.
bool send(const uint8_t* data, int len);
// Key real CW (an unmodulated carrier on the frequency, on and off) for this text; returns at once.
// False if busy sending. onSent when it ends.
bool sendCw(const std::string& text, int wpm);
// Stop CW keying at once (HALT). A packet already on the air finishes (it is short).
void abortTx();
// Called from the radio task: a packet arrived (RSSI dBm, SNR dB) / a send finished.
extern std::function<void(const uint8_t*, int, int, float)> onReceive;
extern std::function<void()> onSent;
// Time on air of a packet of `len` bytes with this configuration, in ms.
int airtimeMs(const LoraConfig& c, int len);
// Counters for the status page.
struct Stats { uint32_t rx = 0, rxCrc = 0, tx = 0; int lastRssi = 0; float lastSnr = 0; };
Stats stats();
}
