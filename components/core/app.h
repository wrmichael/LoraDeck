// LoRaDeck application core: the slot schedule (FT-LoRa and FT-MESH), the QSO sequencer, relaying, the
// Terminal (chat and CW), logging and the state the web page shows. Hardware-free: the board code (main/)
// connects the SX1262 radio, Wi-Fi, GPS and storage.
#pragma once
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <functional>
#include <cstdint>
#include "settings.h"
#include "qso.h"
#include "ftmsg.h"
#include "clock.h"

class App {
public:
    Settings cfg;
    Sequencer seq;
    std::string version = "dev";
    // Time on air of one FT-LoRa packet (11 bytes) with the current radio settings (the board sets it), ms.
    int airMs = 180;
    // Stations send this long after their slot starts (so a station whose clock is a little behind still hears it).
    static const int TX_OFFSET_MS = 150;
    // host tests only: this station's clock is off by this much
    int64_t clockSkewMs = 0;

    // ---- hooks set by the board code ----
    // Start sending a packet now; false if the radio is busy or not ready.
    std::function<bool(const uint8_t* data, int len)> radioSend;
    // Start keying real CW (carrier on/off) now; false if busy. onTxDone when it ends.
    std::function<bool(const std::string& text, int wpm)> radioCw;
    // Stop any transmission at once (HALT).
    std::function<void()> radioAbort;
    // Time on air of a packet of `len` bytes, ms (if not set: estimated from airMs).
    std::function<int(int len)> airtime;
    // Radio settings changed (frequency, SF, bandwidth, coding rate, power).
    std::function<void()> radioConfigure;
    // Factory reset confirmed on the page: wipe the settings (everything = also the logs: format the storage) and restart.
    std::function<void(bool everything)> factoryReset;
    // A contact was logged: the ADIF record (for UDP forwarding).
    std::function<void(const std::string& adifRecord)> onLogged;
    // One line for the console log (what was sent and heard, clock changes, commands).
    std::function<void(const std::string& line)> trace;

    void begin(const std::string& dataDir);
    void tick();                                              // call every ~10 ms
    // A packet arrived (from the radio's receive interrupt task).
    void onPacket(const uint8_t* data, int len, int rssi, float snr);
    void onTxDone();                                          // the radio finished sending (packet or CW)
    // Web page commands and state.
    std::string command(const std::string& c, const std::map<std::string, std::string>& a);
    std::string stateJson(uint32_t sinceRx, bool withCfg);
    void activity();                                          // an operator is present (watchdog)
    // GPS position as a 6-character locator: becomes the grid if gpsGrid is on (saved when it changes).
    void gpsGrid(const std::string& grid6);

    std::string logPath() const;
    std::string logText(const std::string& name = std::string());   // the whole ADIF log (of another call: "AC9HP_P")
    std::string logListJson();                                // every log on the board: [{"name","n","current"}]
    int logCount() const { return logCount_; }
    std::string status() const { std::lock_guard<std::mutex> lk(mtx_); return status_; }
    // "<text> <to> DE <me>" (or "<text> DE <me>"), unless the text already holds "DE <me>".
    std::string withId(const std::string& text, const std::string& to) const;

    // for the OLED
    struct Glance { bool tx = false, txEn = false; std::string call, dx, last, statusText; int heard = 0, logged = 0; };
    Glance glance();

private:
    enum { TX_FT = 0, TX_RELAY = 1, TX_CHAT = 2, TX_CW = 3, TX_CWID = 4, TX_LCW = 5 };
    struct TxItem {
        int64_t at = 0;            // UTC ms (our clock) to start
        int64_t late = 500;        // dropped when it cannot start within this long
        int kind = TX_FT;
        std::vector<uint8_t> pkt;  // packet, or empty for CW
        std::string text;          // what it says (FT message, chat text, CW text)
        uint16_t mid = 0;          // FT-MESH / chat message id
    };
    mutable std::mutex mtx_;
    std::string dir_, status_;
    int64_t slotLen() const { return (int64_t)(mesh() ? cfg.meshSlotSec : cfg.slotSec) * 1000; }
    bool mesh() const { return cfg.ftMode == FT_MESH; }
    bool ftcw() const { return cfg.ftMode == FT_CW; }
    const char* modeName() const { return mesh() ? "FT-MESH" : ftcw() ? "FT-CW" : "FT-LoRa"; }
    int64_t now() const { return clk::utcMs() + clockSkewMs; }
    int air(int len) const { return airtime ? airtime(len) : airMs + (len - FTL_PACKET) * 5; }
    int windowMs() const;                                     // FT-MESH relay window
    int hopsFit() const;                                      // relay windows that fit in the slot
    int64_t quietFromMs() const;                              // where the slot is free for CW (after the packets)
    bool relayOn() const;                                     // FT-MESH messages
    bool termRelayOn() const;                                 // Terminal messages (every mode)
    void termRelay(const uint8_t* data, int len, int left, int taken, float snr, uint16_t id, const std::string& text);
    int myParity() const { return seq.txEnabled || !seq.dxCall.empty() ? seq.txParity : cfg.cqParity; }
    int64_t curSlot_ = -1;
    std::vector<Decode> rxSlot_;                              // heard, for the sequencer at the end of its slot
    std::deque<Decode> recent_;                               // for the web page
    uint32_t nextId_ = 1;
    std::vector<TxItem> txq_;                                 // planned transmissions, by start time
    bool txOn_ = false;
    TxItem cur_;                                              // what is on the air now
    std::string ftText_;                                      // the FT message planned for this slot
    std::deque<std::string> chatOut_, cwOut_, lcwOut_;        // Terminal messages waiting for our next slot
    int moppSerial_ = 0;
    std::map<uint16_t, int64_t> seen_;                        // FT-MESH / chat ids heard or sent -> slot
    int64_t lastTxMono_ = -1, lastIdMono_ = -1;               // for the CW ID
    int64_t lastUiMono_ = 0;
    bool watchdogTripped_ = false;
    int logCount_ = 0;
    std::map<std::string, int> worked_;                       // calls in the log (for "worked before")
    std::string lastDe_;                                      // last "DE <call>" heard in a chat message
    uint32_t resetToken_ = 0;                                 // factory reset: one-time code, valid for a minute
    int64_t resetTokenMono_ = 0;

    void status(const std::string& s) { status_ = s; }
    void tr(const std::string& s) { if (trace) trace(s); }
    void syncSeq();
    void startSlot(int64_t slot);
    void planSlot(int64_t slot);
    void planTerminal(int64_t slot, int64_t cursor, bool cwId = true);
    void sendSoon();                                          // early in our own slot: a new message goes now
    bool waiting(int kind, const std::string& text) const;    // already queued or planned, not yet sent
    std::string waitingJson() const;
    void queue(const TxItem& it);
    void sendDue(int64_t t);
    void dropOwn();                                           // our own planned transmissions (not relays)
    uint16_t msgId(const uint8_t* d, int n, int64_t slot) const;
    void heardFt(const std::string& text, int hops, int64_t sentMs, int rssi, float snr);
    void heardChat(const std::string& text, int hops, int64_t sentMs, int rssi, float snr, int kind = 1, int wpm = 0,
                   const std::string& path = std::string());
    bool meshDup(uint16_t id, int64_t slot, float snr);
    void meshRelay(const uint8_t* data, int len, int left, int taken, int64_t slot, float snr, uint16_t id);
    void addRecent(const Decode& d);
    void logQso(const QsoLog& q);
    bool appendLog(const std::string& rec, const std::string& call, int64_t whenMs);
    void loadLog();
    std::string adifRecord(const QsoLog& q) const;
};
