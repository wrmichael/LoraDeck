// LoRaDeck application core: the FT-LoRa slot schedule, the QSO sequencer, logging and the state the
// web page shows. Hardware-free: the board code (main/) connects the SX1262 radio, Wi-Fi and storage.
#pragma once
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <functional>
#include "settings.h"
#include "qso.h"
#include "ftmsg.h"

class App {
public:
    Settings cfg;
    Sequencer seq;
    std::string version = "dev";

    // ---- hooks set by the board code ----
    // Start sending a packet now; false if the radio is busy or not ready.
    std::function<bool(const uint8_t* data, int len)> radioSend;
    // Radio settings changed (frequency, SF, bandwidth, coding rate, power).
    std::function<void()> radioConfigure;
    // A contact was logged: the ADIF record (for UDP forwarding).
    std::function<void(const std::string& adifRecord)> onLogged;
    // One line for the console log (what was sent and heard, clock changes, commands).
    std::function<void(const std::string& line)> trace;

    void begin(const std::string& dataDir);
    void tick();                                              // call every ~10 ms
    // A packet arrived (from the radio's receive interrupt task).
    void onPacket(const uint8_t* data, int len, int rssi, float snr);
    void onTxDone();                                          // the radio finished sending
    // Web page commands and state.
    std::string command(const std::string& c, const std::map<std::string, std::string>& a);
    std::string stateJson(uint32_t sinceRx, bool withCfg);
    void activity();                                          // an operator is present (watchdog)

    std::string logPath() const;
    std::string logText();                                    // the whole ADIF log
    int logCount() const { return logCount_; }
    std::string status() const { std::lock_guard<std::mutex> lk(mtx_); return status_; }

    // for the OLED
    struct Glance { bool tx = false, txEn = false; std::string call, dx, last, statusText; int heard = 0, logged = 0; };
    Glance glance();

private:
    mutable std::mutex mtx_;
    std::string dir_, status_;
    int64_t slotLen() const { return (int64_t)cfg.slotSec * 1000; }
    int64_t curSlot_ = -1;
    std::vector<Decode> rxSlot_;                              // heard in the current slot
    std::deque<Decode> recent_;                               // for the web page
    uint32_t nextId_ = 1;
    bool txOn_ = false, txPending_ = false;
    std::string txText_;
    uint8_t txBuf_[FTL_PACKET] = {};
    int64_t txSlotStart_ = 0;
    int64_t lastUiMono_ = 0;
    bool watchdogTripped_ = false;
    int logCount_ = 0;
    std::map<std::string, int> worked_;                       // calls in the log (for "worked before")

    void status(const std::string& s) { status_ = s; }
    void tr(const std::string& s) { if (trace) trace(s); }
    void syncSeq();
    void startSlot(int64_t slot);
    void logQso(const QsoLog& q);
    void loadLog();
    std::string adifRecord(const QsoLog& q) const;
};
