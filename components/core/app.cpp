#include "app.h"
#include "clock.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>

namespace {
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) o += ' ';
        else o += c;
    }
    return o + "\"";
}
std::string jnum(int64_t n) { return std::to_string(n); }
const char* jbool(bool b) { return b ? "true" : "false"; }
std::string ok() { return "{\"ok\":true}"; }
std::string err(const std::string& e) { return "{\"ok\":false,\"err\":" + jstr(e) + "}"; }
std::string arg(const std::map<std::string, std::string>& a, const char* k) {
    auto it = a.find(k);
    return it == a.end() ? std::string() : it->second;
}
std::string utcDate(int64_t ms) {          // YYYYMMDD
    time_t t = (time_t)(ms / 1000);
    struct tm tm;
    gmtime_r(&t, &tm);
    char b[12];
    strftime(b, sizeof b, "%Y%m%d", &tm);
    return b;
}
std::string utcTime(int64_t ms) {          // HHMMSS
    time_t t = (time_t)(ms / 1000);
    struct tm tm;
    gmtime_r(&t, &tm);
    char b[10];
    strftime(b, sizeof b, "%H%M%S", &tm);
    return b;
}
std::string field(const char* name, const std::string& v) {
    if (v.empty()) return "";
    return "<" + std::string(name) + ":" + std::to_string(v.size()) + ">" + v + " ";
}
}

std::string App::logPath() const {
    return dir_ + "/" + (cfg.call.empty() ? std::string("NOCALL") : cfg.call) + "_log.adi";
}

void App::begin(const std::string& dataDir) {
    std::lock_guard<std::mutex> lk(mtx_);
    dir_ = dataDir;
    cfg.load(dir_ + "/settings.txt");
    seq.now = [] { return clk::utcMs(); };
    seq.onLog = [this](const QsoLog& q) { logQso(q); };
    seq.workedBefore = [this](const std::string& c) { return worked_.count(c) > 0; };
    seq.workedToday = [this](const std::string& c) {
        auto it = worked_.find(c);
        return it != worked_.end() && it->second == atoi(utcDate(clk::utcMs()).c_str());
    };
    syncSeq();
    seq.setOpMode(cfg.opMode);
    loadLog();
    lastUiMono_ = clk::monoMs();
    status(cfg.call.empty() ? "Set your callsign in Settings before transmitting" : "Ready");
}

void App::syncSeq() {
    seq.myCall = cfg.call;
    seq.myGrid = cfg.grid.substr(0, 4);
    seq.cqTag = cfg.cqTag;
    seq.maxRetries = cfg.maxRetries;
    seq.useRR73 = cfg.useRR73;
    seq.cqParity = cfg.cqParity;
    seq.confirmEach = cfg.confirmQso;
}

void App::activity() {
    std::lock_guard<std::mutex> lk(mtx_);
    lastUiMono_ = clk::monoMs();
}

void App::tick() {
    std::lock_guard<std::mutex> lk(mtx_);
    int64_t mono = clk::monoMs();
    if (seq.txEnabled && cfg.watchdogMin > 0 && mono - lastUiMono_ > (int64_t)cfg.watchdogMin * 60000) {
        seq.txEnabled = false;
        txPending_ = false;
        watchdogTripped_ = true;
        status("TX watchdog: no operator present for " + std::to_string(cfg.watchdogMin) + " min, transmit disabled");
    }
    if (!clk::utcValid()) return;
    int64_t now = clk::utcMs(), L = slotLen();
    int64_t slot = now / L;
    if (slot != curSlot_) startSlot(slot);
    // send a little after the slot start, so a station whose clock is slightly behind still hears it in this slot
    if (txPending_ && now - txSlotStart_ >= 150) {
        txPending_ = false;
        if (radioSend && radioSend(txBuf_, FTL_PACKET)) {
            txOn_ = true;
            tr("TX " + std::string((txSlotStart_ / L) & 1 ? "odd" : "even") + " slot: " + txText_);
        } else {
            status("Radio not ready: nothing sent");
            tr("TX failed, radio not ready: " + txText_);
        }
    }
}

void App::startSlot(int64_t slot) {
    if (curSlot_ >= 0 && slot == curSlot_ + 1) {            // what we heard in the slot that just ended
        std::vector<Decode> heard;
        heard.swap(rxSlot_);
        seq.onDecodes(heard, (int)(curSlot_ & 1));
    } else {
        rxSlot_.clear();
    }
    curSlot_ = slot;
    if (txOn_) return;                                       // still sending (should not happen with short packets)
    std::string text;
    if (cfg.call.empty() || !seq.nextTx((int)(slot & 1), text)) return;
    if (!ftlPack(text, txBuf_)) {
        status("Cannot send \"" + text + "\" (not a valid FT message)");
        seq.halt();
        return;
    }
    txText_ = text;
    txSlotStart_ = slot * slotLen();
    txPending_ = true;
}

void App::onTxDone() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!txOn_) return;
    txOn_ = false;
    Decode d;                                               // show our own transmission in the list too
    d.id = nextId_++;
    d.slotMs = txSlotStart_;
    d.parity = (int)((txSlotStart_ / slotLen()) & 1);
    d.text = txText_;
    d.snr = 99;                                              // marks "sent by us"
    recent_.push_back(d);
    while (recent_.size() > 100) recent_.pop_front();
    seq.txDone();
}

void App::onPacket(const uint8_t* data, int len, int rssi, float snr) {
    std::string text;
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ftlUnpack(data, len, text)) {
        char b[80];
        snprintf(b, sizeof b, "heard a packet that is not FT-LoRa (%d bytes, first byte 0x%02x)", len, len > 0 ? data[0] : 0);
        tr(b);
        return;
    }
    int64_t now = clk::utcMs(), L = slotLen();
    Decode d;
    d.id = nextId_++;
    d.slotMs = (now / L) * L;
    d.parity = (int)((now / L) & 1);
    d.snr = (int)lroundf(snr);
    d.rssi = rssi;
    d.dt = (float)(now - d.slotMs) / 1000.0f;
    d.text = text;
    {
        char b[48];
        snprintf(b, sizeof b, " (SNR %d, %+.2f s, %s slot)", d.snr, d.dt, d.parity ? "odd" : "even");
        tr("heard: " + text + b);
    }
    rxSlot_.push_back(d);
    recent_.push_back(d);
    while (recent_.size() > 100) recent_.pop_front();
}

std::string App::adifRecord(const QsoLog& q) const {
    char mhz[20];
    snprintf(mhz, sizeof mhz, "%.6f", cfg.freqHz / 1e6);
    std::string r;
    r += field("CALL", q.call);
    r += field("GRIDSQUARE", q.grid);
    r += field("MODE", cfg.adifMode);
    if (q.rptSent != Sequencer::UNSET) r += field("RST_SENT", std::to_string(q.rptSent));
    if (q.rptRcvd != Sequencer::UNSET) r += field("RST_RCVD", std::to_string(q.rptRcvd));
    r += field("QSO_DATE", utcDate(q.startMs));
    r += field("TIME_ON", utcTime(q.startMs));
    r += field("QSO_DATE_OFF", utcDate(q.endMs));
    r += field("TIME_OFF", utcTime(q.endMs));
    r += field("FREQ", mhz);
    r += field("BAND", adifBand(cfg.freqHz));
    r += field("STATION_CALLSIGN", cfg.call);
    r += field("MY_GRIDSQUARE", cfg.grid);
    r += field("PROGRAMID", "LoRaDeck");
    r += field("PROGRAMVERSION", version);
    r += field("COMMENT", "FT-LoRa SF" + std::to_string(cfg.sf) + " BW" + std::to_string(cfg.bwKhz));
    return r + "<EOR>\n";
}

void App::logQso(const QsoLog& q) {                         // called by the sequencer, lock held
    std::string path = logPath();
    FILE* f = fopen(path.c_str(), "a");
    if (!f) { status("Could not write the log"); return; }
    if (ftell(f) == 0) fputs("LoRaDeck log\n<ADIF_VER:5>3.1.4 <PROGRAMID:8>LoRaDeck <EOH>\n", f);
    std::string rec = adifRecord(q);
    fputs(rec.c_str(), f);
    fclose(f);
    logCount_++;
    worked_[q.call] = atoi(utcDate(q.endMs).c_str());
    status("Logged " + q.call);
    if (onLogged) onLogged(rec);
}

void App::loadLog() {
    logCount_ = 0;
    worked_.clear();
    FILE* f = fopen(logPath().c_str(), "r");
    if (!f) return;
    std::string all;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    fclose(f);
    size_t p = 0;
    while ((p = all.find("<CALL:", p)) != std::string::npos) {
        size_t c = all.find('>', p);
        int len = atoi(all.c_str() + p + 6);
        if (c == std::string::npos || len <= 0) break;
        std::string call = all.substr(c + 1, len);
        size_t eor = all.find("<EOR>", c);
        size_t d = all.find("<QSO_DATE:8>", c);
        int day = (d != std::string::npos && d < eor) ? atoi(all.substr(d + 12, 8).c_str()) : 0;
        worked_[call] = day;
        logCount_++;
        p = eor == std::string::npos ? all.size() : eor;
    }
}

std::string App::logText() {
    std::lock_guard<std::mutex> lk(mtx_);
    FILE* f = fopen(logPath().c_str(), "r");
    if (!f) return "";
    std::string all;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    fclose(f);
    return all;
}

std::string App::command(const std::string& c, const std::map<std::string, std::string>& a) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (c != "time") lastUiMono_ = clk::monoMs();
    if (c != "time" && c != "ping") {
        std::string line = "command: " + c;
        for (auto& kv : a)
            if (kv.first.find("Pass") == std::string::npos) line += " " + kv.first + "=" + kv.second;
        tr(line);
    }
    if (c == "ping") return ok();
    if (c == "time") {
        int64_t ms = atoll(arg(a, "ms").c_str());
        if (ms < 1735689600000LL) return err("bad time");
        if (!clk::utcValid() || llabs(ms - clk::utcMs()) > 500) {
            if (clk::utcValid()) tr("clock set from the browser, was " + std::to_string(clk::utcMs() - ms) + " ms off");
            else tr("clock set from the browser");
            clk::setUtcMs(ms);
        }
        return ok();
    }
    if (c == "txen") {
        bool on = arg(a, "on") == "1";
        if (on && cfg.call.empty()) return err("Set your callsign first");
        seq.txEnabled = on;
        watchdogTripped_ = false;
        if (!on) txPending_ = false;
        if (on && seq.opMode == OP_AUTO_CQ && seq.dxCall.empty() && seq.txMsg == 0) seq.startCq();
        status(on ? "Transmit enabled" : "Transmit off");
        return ok();
    }
    if (c == "halt") { seq.halt(); txPending_ = false; status("Halted"); return ok(); }
    if (c == "cq") {
        if (cfg.call.empty()) return err("Set your callsign first");
        seq.startCq();
        seq.txEnabled = true;
        watchdogTripped_ = false;
        return ok();
    }
    if (c == "pick") {                                       // a message in the list was tapped
        uint32_t id = (uint32_t)strtoul(arg(a, "id").c_str(), nullptr, 10);
        for (const Decode& d : recent_) {
            if (d.id != id || d.snr == 99) continue;
            ParsedMsg p;
            if (!parseMsg(d.text, p) || p.from.empty()) return err("Not a message I can answer");
            if (cfg.call.empty()) return err("Set your callsign first");
            if (p.to == cfg.call) seq.respondTo(p, d.snr, d.parity);
            else seq.callStation(p.from, p.grid, d.snr, d.parity);
            seq.txEnabled = true;
            watchdogTripped_ = false;
            return ok();
        }
        return err("That message is no longer in the list");
    }
    if (c == "opmode") {
        long m = strtol(arg(a, "m").c_str(), nullptr, 10);
        if (m < 0 || m > 2) return err("bad mode");
        cfg.opMode = (int)m;
        seq.setOpMode((int)m);
        cfg.save(dir_ + "/settings.txt");
        return ok();
    }
    if (c == "txmsg") { seq.setTxMsg(atoi(arg(a, "n").c_str())); return ok(); }
    if (c == "clear") { seq.clearQso(); seq.txMsg = 0; return ok(); }
    if (c == "nextqso") { seq.nextQso(); return ok(); }
    if (c == "log") {
        int r = seq.forceLog();
        if (r == 0) return ok();
        return err(r == 1 ? "Already logged" : r == -1 ? "No contact in progress" : "No report sent or received yet");
    }
    if (c == "parity") {
        cfg.cqParity = arg(a, "v") == "1" ? 1 : 0;
        syncSeq();
        seq.txParity = cfg.cqParity;
        cfg.save(dir_ + "/settings.txt");
        return ok();
    }
    if (c == "set") {
        Settings n = cfg;
        std::string bad;
        for (auto& kv : a)
            if (!n.set(kv.first, kv.second)) bad += (bad.empty() ? "" : ", ") + kv.first;
        if (!bad.empty()) return err("Not accepted: " + bad);
        bool radio = n.freqHz != cfg.freqHz || n.sf != cfg.sf || n.bwKhz != cfg.bwKhz || n.cr != cfg.cr || n.powerDbm != cfg.powerDbm;
        bool callChanged = n.call != cfg.call;
        cfg = n;
        if (!cfg.save(dir_ + "/settings.txt")) return err("Could not save the settings");
        syncSeq();
        seq.setOpMode(cfg.opMode);
        if (callChanged) loadLog();
        if (radio && radioConfigure) radioConfigure();
        status("Settings saved");
        return ok();
    }
    return err("unknown command");
}

std::string App::stateJson(uint32_t sinceRx, bool withCfg) {
    std::lock_guard<std::mutex> lk(mtx_);
    int64_t now = clk::utcMs(), L = slotLen(), mono = clk::monoMs();
    int wdLeft = -1;
    if (seq.txEnabled && cfg.watchdogMin > 0) {
        int64_t left = (int64_t)cfg.watchdogMin * 60000 - (mono - lastUiMono_);
        wdLeft = left > 0 ? (int)(left / 1000) : 0;
    }
    std::string txText = txOn_ || txPending_ ? txText_ : (seq.txMsg ? seq.compose(seq.txMsg) : std::string());
    std::string o;
    o.reserve(4096);
    o += "{\"ver\":" + jstr(version) + ",\"call\":" + jstr(cfg.call) + ",\"grid\":" + jstr(cfg.grid);
    o += ",\"utc\":" + jnum(now) + ",\"clock\":" + jbool(clk::utcValid());
    o += ",\"slotSec\":" + jnum(cfg.slotSec) + ",\"phase\":" + jnum(now % L) + ",\"parity\":" + jnum((now / L) & 1);
    o += ",\"radio\":{\"freq\":" + jnum(cfg.freqHz) + ",\"sf\":" + jnum(cfg.sf) + ",\"bw\":" + jnum(cfg.bwKhz) +
         ",\"cr\":" + jnum(cfg.cr) + ",\"pwr\":" + jnum(cfg.powerDbm) + ",\"band\":" + jstr(adifBand(cfg.freqHz)) + "}";
    o += ",\"tx\":{\"en\":" + std::string(jbool(seq.txEnabled)) + ",\"on\":" + jbool(txOn_) +
         ",\"pending\":" + jbool(txPending_) + ",\"msg\":" + jnum(seq.txMsg) + ",\"text\":" + jstr(txText) +
         ",\"dx\":" + jstr(seq.dxCall) + ",\"dxGrid\":" + jstr(seq.dxGrid) +
         ",\"rptS\":" + jnum(seq.rptSent) + ",\"rptR\":" + jnum(seq.rptRcvd) + ",\"op\":" + jnum(seq.opMode) +
         ",\"txParity\":" + jnum(seq.txParity) + ",\"retries\":" + jnum(seq.retries) +
         ",\"wait\":" + jbool(seq.waitNext) + ",\"wd\":" + jbool(watchdogTripped_) +
         ",\"wdLeft\":" + jnum(wdLeft) + ",\"wdMin\":" + jnum(cfg.watchdogMin) + "}";
    o += ",\"rx\":[";
    bool first = true;
    for (const Decode& d : recent_) {
        if (d.id <= sinceRx) continue;
        if (!first) o += ',';
        first = false;
        o += "{\"id\":" + jnum(d.id) + ",\"t\":" + jnum(d.slotMs) + ",\"snr\":" + jnum(d.snr) + ",\"rssi\":" +
             jnum(d.rssi) + ",\"p\":" + jnum(d.parity) + ",\"text\":" + jstr(d.text) + "}";
    }
    o += "],\"log\":" + jnum(logCount_) + ",\"status\":" + jstr(status_);
    if (withCfg) o += ",\"cfg\":" + cfg.json(false);
    return o + "}";
}

App::Glance App::glance() {
    std::lock_guard<std::mutex> lk(mtx_);
    Glance g;
    g.tx = txOn_;
    g.txEn = seq.txEnabled;
    g.call = cfg.call;
    g.dx = seq.dxCall;
    for (auto it = recent_.rbegin(); it != recent_.rend(); ++it)
        if (it->snr != 99) { g.last = it->text; break; }
    g.heard = (int)recent_.size();
    g.logged = logCount_;
    g.statusText = status_;
    return g;
}
