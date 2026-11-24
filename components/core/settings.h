// Persistent settings, stored as key=value lines in a text file on the flash filesystem.
#pragma once
#include <string>
#include <cstdint>

enum OpMode { OP_MANUAL = 0, OP_AUTO_CQ = 1, OP_AUTO_HUNT = 2 };
enum FtMode { FT_LORA = 0, FT_MESH = 1 };                // all stations on a channel use the same
enum MeshRelay { RELAY_OFF = 0, RELAY_TX_ON = 1, RELAY_ALWAYS = 2 };

struct Settings {
    // station
    std::string call;                    // station callsign (sent on the air), must be set before LoRaDeck will transmit
    std::string operatorCall;            // the operator's own call when the station call is a club / other call (ADIF OPERATOR)
    std::string grid;                    // 4 or 6 character locator
    std::string cqTag;                   // directed CQ: "" plain, or DX / POTA ... (1-4 letters)
    // operating
    int opMode = OP_MANUAL;
    int maxRetries = 5;                  // repeats of one message before giving up
    bool useRR73 = true;
    bool confirmQso = false;             // "POTA mode": wait for Next QSO after each contact in auto modes
    int cqParity = 0;                    // 0 even slots, 1 odd
    int slotSec = 5;                     // FT-LoRa slot length; every station on the channel must use the same
    int watchdogMin = 6;                 // TX off after this long without an operator present (0 = off)
    // FT-MESH: every station also repeats what it hears (managed flooding) in relay windows of a longer slot
    int ftMode = FT_LORA;
    int meshSlotSec = 15;                // FT-MESH slot length
    int meshHops = 3;                    // how many times a message is repeated at most
    int meshRelay = RELAY_TX_ON;         // repeat for others: off / while TX is enabled (operator present) / always
    // LoRa-CW: real CW (keyed carrier) for the CW ID and CW messages; chat messages in LoRa packets
    int cwWpm = 20;
    int cwIdMin = 10;                    // CW ID every this many minutes while transmitting (0 = off)
    // Terminal macros ({MY} = my call, {TO} = the other station)
    std::string cwMacro[6] = {"CQ CQ CQ DE {MY} {MY} K", "{TO} DE {MY} UR 599 599 K", "{TO} DE {MY} R TU 73 SK",
                              "QRZ? DE {MY}", "{TO} DE {MY} AGN? K", "{MY}"};
    // LoRa radio
    uint32_t freqHz = 917000000;         // 33 cm: inside the 915-921 MHz spread-spectrum block of the ARRL band plan,
                                         // clear of LoRaWAN uplinks (<= 914.9), Meshtastic LongFast (906.875) and 915.0
    int sf = 9;                          // spreading factor 7..12
    int bwKhz = 125;                     // 125, 250 or 500
    int cr = 5;                          // coding rate 4/5..4/8 (5..8)
    int powerDbm = 14;                   // -9..22 dBm into the antenna connector
    // Wi-Fi
    std::string apSsid = "LoRaDeck";
    std::string apPass = "loradeck1";    // WPA2 needs >= 8 characters
    std::string staSsid, staPass;        // optional home network
    // logging
    std::string adifMode = "FT-LORA";    // ADIF MODE written for these contacts
    std::string adifMesh = "FT-MESH";    // ... in FT-MESH
    std::string adifChat = "LORA-CHAT";  // ... for contacts logged on the Terminal page
    std::string logHost;                 // UDP ADIF forwarding (e.g. Log4OM), empty = off
    int logPort = 2237;
    // GPS module (optional, NMEA on a UART): time, and the grid if gpsGrid
    int gpsRx = -1, gpsTx = -1;          // board pins: RX = the module's TXD, TX = the module's RXD; -1 = no GPS
    int gpsBaud = 9600;
    int gpsPps = -1;                     // pulse-per-second pin for exact time; -1 = none
    bool gpsGrid = true;                 // take the grid locator from the GPS position

    bool set(const std::string& k, const std::string& v);
    std::string get(const std::string& k) const;
    std::string serialize(bool secrets) const;     // key=value lines
    std::string json(bool secrets) const;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

// Band name for ADIF from a frequency (empty if outside the amateur bands we know).
std::string adifBand(uint32_t hz);
