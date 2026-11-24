#include "app.h"
#include "clock.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cctype>
#include <iterator>
#include "morse.h"

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
    std::string name = cfg.call.empty() ? std::string("NOCALL") : cfg.call;
    for (char& c : name) if (c == '/') c = '_';                // AC9HP/P -> AC9HP_P_log.adi
    return dir_ + "/" + name + "_log.adi";
}

void App::begin(const std::string& dataDir) {
    std::lock_guard<std::mutex> lk(mtx_);
    dir_ = dataDir;
    cfg.load(dir_ + "/settings.txt");
    seq.now = [this] { return now(); };
    seq.onLog = [this](const QsoLog& q) { logQso(q); };
    seq.workedBefore = [this](const std::string& c) { return worked_.count(c) > 0; };
    seq.workedToday = [this](const std::string& c) {
        auto it = worked_.find(c);
        return it != worked_.end() && it->second == atoi(utcDate(now()).c_str());
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

// ---------------------------------------------------------------- slots and transmissions

int App::windowMs() const { int w = air(FTM_PACKET) * 2 + 150; return w < 500 ? 500 : w; }

int App::hopsFit() const {                                  // relay windows that fit, with room left for CW
    int h = cfg.meshHops, w = windowMs();
    while (h > 0 && TX_OFFSET_MS + (int64_t)(h + 1) * w + 1500 > slotLen()) h--;
    return h;
}

int64_t App::quietFromMs() const { return TX_OFFSET_MS + (int64_t)(hopsFit() + 1) * windowMs() + 100; }

bool App::relayOn() const {
    if (!mesh() || cfg.call.empty()) return false;
    return cfg.meshRelay == RELAY_ALWAYS || (cfg.meshRelay == RELAY_TX_ON && seq.txEnabled);
}

uint16_t App::msgId(const uint8_t* d, int n, int64_t slot) const {   // FNV-1a over the content and the slot
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(slot >> (8 * i)); h *= 16777619u; }
    uint16_t id = (uint16_t)(h ^ (h >> 16));
    return id ? id : 1;
}

void App::queue(const TxItem& it) {
    auto pos = txq_.begin();
    while (pos != txq_.end() && pos->at <= it.at) ++pos;
    txq_.insert(pos, it);
}

void App::dropOwn() {
    for (auto it = txq_.begin(); it != txq_.end();) it = it->kind == TX_RELAY ? it + 1 : txq_.erase(it);
}

void App::tick() {
    std::lock_guard<std::mutex> lk(mtx_);
    int64_t mono = clk::monoMs();
    if (seq.txEnabled && cfg.watchdogMin > 0 && mono - lastUiMono_ > (int64_t)cfg.watchdogMin * 60000) {
        seq.txEnabled = false;
        dropOwn();
        chatOut_.clear();
        cwOut_.clear();
        if (txOn_ && (cur_.kind == TX_CW || cur_.kind == TX_CWID) && radioAbort) radioAbort();
        if (cfg.meshRelay != RELAY_ALWAYS) txq_.clear();
        watchdogTripped_ = true;
        status("TX watchdog: no operator present for " + std::to_string(cfg.watchdogMin) + " min, transmit disabled");
    }
    if (!clk::utcValid()) return;
    int64_t t = now(), L = slotLen();
    int64_t slot = t / L;
    if (slot != curSlot_) startSlot(slot);
    sendDue(t);
}

void App::sendDue(int64_t t) {
    if (txOn_) return;
    while (!txq_.empty() && txq_.front().at <= t) {
        TxItem it = txq_.front();
        txq_.erase(txq_.begin());
        if (t - it.at > it.late) {                          // the clock jumped, or the radio was busy
            tr("TX skipped, too late: " + it.text);
            continue;
        }
        bool sent;
        int wpm = cfg.cwWpm;
        if (it.kind == TX_CW || it.kind == TX_CWID) {
            if (it.kind == TX_CWID) {                       // fit the ID into what is left of the slot
                int64_t L = slotLen(), left = (it.at / L + 1) * L - t - 300;
                int units = morseUnits(it.text);
                while (wpm < 40 && left > 0 && (int64_t)units * 1200 / wpm > left) wpm++;
            }
            sent = radioCw && radioCw(it.text, wpm);
        } else {
            sent = radioSend && radioSend(it.pkt.data(), (int)it.pkt.size());
        }
        if (!sent) {
            status("Radio not ready: nothing sent");
            tr("TX failed, radio not ready: " + it.text);
            continue;
        }
        txOn_ = true;
        cur_ = it;
        int64_t mono = clk::monoMs();
        if (it.kind == TX_CWID) lastIdMono_ = mono;
        else lastTxMono_ = mono;
        int64_t L = slotLen();
        const char* par = (it.at / L) & 1 ? "odd" : "even";
        switch (it.kind) {
        case TX_FT: tr(std::string("TX ") + par + " slot: " + it.text); break;
        case TX_RELAY: tr("relay (hop " + std::to_string(it.pkt[1] & 15) + "): " + it.text); break;
        case TX_CHAT: tr(std::string("TX chat, ") + par + " slot: " + it.text); break;
        default: tr("TX CW at " + std::to_string(wpm) + " wpm (" + std::to_string(morseMs(it.text, wpm)) + " ms): " + it.text);
        }
        return;
    }
}

void App::startSlot(int64_t slot) {
    int64_t L = slotLen();
    if (curSlot_ >= 0 && slot == curSlot_ + 1) {            // what was sent in the slot that just ended
        std::vector<Decode> heard, later;
        for (const Decode& d : rxSlot_) {
            int64_t s = d.slotMs / L;
            if (s == curSlot_) heard.push_back(d);
            else if (s > curSlot_) later.push_back(d);      // our clock is behind the sender's: keep for its slot
        }
        rxSlot_.swap(later);
        seq.onDecodes(heard, (int)(curSlot_ & 1));
    } else {
        rxSlot_.clear();
    }
    curSlot_ = slot;
    for (auto it = seen_.begin(); it != seen_.end();) it = it->second < slot - 4 ? seen_.erase(it) : std::next(it);
    planSlot(slot);
}

void App::planSlot(int64_t slot) {
    int64_t L = slotLen(), s0 = slot * L, cursor = s0 + TX_OFFSET_MS;
    int par = (int)(slot & 1);
    ftText_.clear();
    std::string text;
    if (!cfg.call.empty() && !txOn_ && seq.nextTx(par, text)) {
        uint8_t b[FTL_PACKET];
        if (!ftlPack(text, b)) {
            status("Cannot send \"" + text + "\" (not a valid FT message)");
            seq.halt();
        } else {
            TxItem it;
            it.at = cursor;
            it.kind = TX_FT;
            it.text = text;
            if (mesh()) {
                uint16_t id = msgId(b + 1, FTL_PACKET - 1, slot);
                it.pkt.assign(FTM_PACKET, 0);
                it.pkt[0] = FTM_MAGIC;
                it.pkt[1] = (uint8_t)(hopsFit() << 4);
                it.pkt[2] = (uint8_t)(id >> 8);
                it.pkt[3] = (uint8_t)id;
                memcpy(&it.pkt[4], b + 1, FTL_PACKET - 1);
                it.mid = id;
                seen_[id] = slot;                            // our own message coming back is not news
            } else {
                it.pkt.assign(b, b + FTL_PACKET);
            }
            queue(it);
            ftText_ = text;
            cursor += air((int)it.pkt.size()) + 80;
        }
    }
    if (cfg.call.empty() || par != myParity()) return;
    if (!chatOut_.empty()) {                                // a Terminal message, after our FT message
        std::string t = chatOut_.front();
        chatOut_.pop_front();
        TxItem it;
        it.at = cursor;
        it.kind = TX_CHAT;
        it.text = t;
        uint16_t id = msgId((const uint8_t*)t.data(), (int)t.size(), slot);
        it.pkt = {CHAT_MAGIC, (uint8_t)(mesh() ? hopsFit() << 4 : 0), (uint8_t)(id >> 8), (uint8_t)id};
        it.pkt.insert(it.pkt.end(), t.begin(), t.end());
        it.mid = id;
        seen_[id] = slot;
        queue(it);
        cursor += air((int)it.pkt.size()) + 80;
    }
    // CW (keyed carrier) in the quiet part of our slot: after the packets (and the relay windows in FT-MESH)
    int64_t quiet = mesh() ? s0 + quietFromMs() : cursor + 100;
    if (quiet < cursor) quiet = cursor;
    int64_t mono = clk::monoMs();
    TxItem it;
    if (!cwOut_.empty()) {
        it.kind = TX_CW;
        it.text = cwOut_.front();
        cwOut_.pop_front();
    } else if (cfg.cwIdMin > 0 && lastTxMono_ >= 0 && mono - lastTxMono_ < cfg.cwIdMin * 60000LL &&
               (lastIdMono_ < 0 || mono - lastIdMono_ >= cfg.cwIdMin * 60000LL)) {
        it.kind = TX_CWID;
        it.text = "DE " + cfg.call;
    } else {
        return;
    }
    it.at = quiet;
    it.late = 1500;
    queue(it);
}

void App::onTxDone() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!txOn_) return;
    txOn_ = false;
    Decode d;                                               // show our own transmissions in the list too
    d.id = nextId_++;
    d.slotMs = (cur_.at / slotLen()) * slotLen();
    d.parity = (int)((cur_.at / slotLen()) & 1);
    d.snr = 99;                                              // marks "sent by us"
    d.kind = cur_.kind == TX_FT ? 0 : cur_.kind == TX_CHAT ? 1 : cur_.kind == TX_RELAY ? 3 : 2;
    d.hops = cur_.kind == TX_RELAY && !cur_.pkt.empty() ? (cur_.pkt[1] & 15) : 0;
    d.text = cur_.text;
    addRecent(d);
    if (cur_.kind == TX_FT) seq.txDone();
}

void App::addRecent(const Decode& d) {
    recent_.push_back(d);
    while (recent_.size() > 150) recent_.pop_front();
}

// ---------------------------------------------------------------- receiving

namespace {
// The call after the last "DE " in a text ("... W9AIM DE AC9HP K" -> AC9HP), and the word before it (the addressee).
bool deCalls(const std::string& t, std::string& from, std::string& to) {
    std::string u;
    for (char c : t) u += (char)toupper((unsigned char)c);
    size_t p = u.rfind("DE ");
    while (p != std::string::npos && p > 0 && u[p - 1] != ' ') p = p >= 1 ? u.rfind("DE ", p - 1) : std::string::npos;
    if (p == std::string::npos) return false;
    size_t s = p + 3, e = s;
    while (e < u.size() && (isalnum((unsigned char)u[e]) || u[e] == '/')) e++;
    from = u.substr(s, e - s);
    to.clear();
    if (p >= 2) {
        size_t we = p - 1, ws = we;
        while (ws > 0 && u[ws - 1] != ' ') ws--;
        to = u.substr(ws, we - ws);
    }
    return from.size() >= 3;
}
bool hasWord(const std::string& text, const std::string& w) {
    std::string u;
    for (char c : text) u += (char)toupper((unsigned char)c);
    size_t p = 0;
    while ((p = u.find(w, p)) != std::string::npos) {
        bool l = p == 0 || !isalnum((unsigned char)u[p - 1]), r = p + w.size() >= u.size() || !isalnum((unsigned char)u[p + w.size()]);
        if (l && r) return true;
        p++;
    }
    return false;
}
}

void App::onPacket(const uint8_t* data, int len, int rssi, float snr) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (len < 1) return;
    uint8_t m = data[0];
    if (m == FTL_MAGIC) {
        if (mesh()) { tr("heard an FT-LoRa packet: ignored, this station is set to FT-MESH"); return; }
        std::string text;
        if (!ftlUnpack(data, len, text)) { tr("heard a damaged FT-LoRa packet (" + std::to_string(len) + " bytes)"); return; }
        heardFt(text, 0, now() - air(len) - TX_OFFSET_MS, rssi, snr);
        return;
    }
    if ((m == FTM_MAGIC && len == FTM_PACKET) || (m == CHAT_MAGIC && len >= 5 && len <= 4 + CHAT_MAX)) {
        if (m == FTM_MAGIC && !mesh()) { tr("heard an FT-MESH packet: ignored, this station is set to FT-LoRa"); return; }
        int left = data[1] >> 4, taken = data[1] & 15;
        uint16_t id = (uint16_t)(data[2] << 8 | data[3]);
        // when its originator's slot started: arrival - time on air - TX offset - the relay windows before
        int W = windowMs();
        int64_t start = now() - air(len) - TX_OFFSET_MS - (taken ? (int64_t)taken * W + (W - air(len)) / 2 : 0);
        int64_t L = slotLen(), slot = (start + L / 2) / L;
        if (meshDup(id, slot, snr)) return;
        seen_[id] = slot;
        std::string text;
        bool toMe = false;
        if (m == FTM_MAGIC) {
            uint8_t b[FTL_PACKET];
            b[0] = FTL_MAGIC;
            memcpy(b + 1, data + 4, FTL_PACKET - 1);
            if (!ftlUnpack(b, FTL_PACKET, text)) { tr("heard a damaged FT-MESH packet"); return; }
            ParsedMsg p;
            toMe = parseMsg(text, p) && p.to == cfg.call;
            heardFt(text, taken, start, rssi, snr);
        } else {
            for (int i = 4; i < len; i++) text += (data[i] >= 0x20 && data[i] < 0x7F) ? (char)data[i] : '?';
            std::string from, to;
            toMe = deCalls(text, from, to) && to == cfg.call;
            heardChat(text, taken, start, rssi, snr);
        }
        if (left > 0 && relayOn() && !toMe) meshRelay(data, len, left, taken, slot, snr, id);
        return;
    }
    char b[80];
    snprintf(b, sizeof b, "heard a packet that is not LoRaDeck's (%d bytes, first byte 0x%02x)", len, m);
    tr(b);
}

bool App::meshDup(uint16_t id, int64_t slot, float snr) {
    auto it = seen_.find(id);
    if (it == seen_.end() || llabs(it->second - slot) > 2) return false;
    for (auto q = txq_.begin(); q != txq_.end();) {         // someone else repeated it first: ours is not needed
        if (q->kind == TX_RELAY && q->mid == id) {
            tr("relay not needed, another station repeated it: " + q->text);
            q = txq_.erase(q);
        } else {
            ++q;
        }
    }
    (void)snr;
    return true;
}

void App::meshRelay(const uint8_t* data, int len, int left, int taken, int64_t slot, float snr, uint16_t id) {
    if (taken + 1 > hopsFit()) return;
    int W = windowMs();
    // Weakly heard copies go first: that station is probably farthest from the sender and extends the range most.
    double f = (snr + 15.0) / 25.0;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    int spare = W - air(len) - 60;
    TxItem it;
    it.at = slot * slotLen() + TX_OFFSET_MS + (int64_t)(taken + 1) * W + (int64_t)(f * (spare > 0 ? spare : 0));
    it.late = 60;
    it.kind = TX_RELAY;
    it.pkt.assign(data, data + len);
    it.pkt[1] = (uint8_t)((left - 1) << 4 | (taken + 1));
    it.mid = id;
    if (data[0] == FTM_MAGIC) {
        uint8_t b[FTL_PACKET];
        b[0] = FTL_MAGIC;
        memcpy(b + 1, data + 4, FTL_PACKET - 1);
        ftlUnpack(b, FTL_PACKET, it.text);
    } else {
        it.text.assign((const char*)data + 4, len - 4);
    }
    if (it.at + it.late < now()) { tr("too late to relay: " + it.text); return; }
    queue(it);
}

void App::heardFt(const std::string& text, int hops, int64_t startMs, int rssi, float snr) {
    int64_t L = slotLen(), slot = (startMs + L / 2) / L;
    Decode d;
    d.id = nextId_++;
    d.slotMs = slot * L;
    d.parity = (int)(slot & 1);
    d.snr = (int)lroundf(snr);
    d.rssi = rssi;
    d.dt = hops ? 0.0f : (float)(startMs - d.slotMs) / 1000.0f;   // how far the sender's clock seems behind ours
    d.hops = hops;
    d.text = text;
    char b[64];
    snprintf(b, sizeof b, " (SNR %d, %+.2f s, %s slot%s)", d.snr, d.dt, d.parity ? "odd" : "even",
             hops ? (", via " + std::to_string(hops) + (hops == 1 ? " relay" : " relays")).c_str() : "");
    tr("heard: " + text + b);
    ParsedMsg p;
    if (!cfg.call.empty() && parseMsg(text, p) && p.from == cfg.call) {
        status("Heard another station using your callsign " + cfg.call + ": give each LoRaDeck its own call (e.g. " +
               cfg.call + "/P)");
        tr("another station is sending as " + cfg.call + "; its messages are ignored");
    }
    rxSlot_.push_back(d);
    addRecent(d);
}

void App::heardChat(const std::string& text, int hops, int64_t startMs, int rssi, float snr) {
    int64_t L = slotLen(), slot = (startMs + L / 2) / L;
    Decode d;
    d.id = nextId_++;
    d.slotMs = slot * L;
    d.parity = (int)(slot & 1);
    d.snr = (int)lroundf(snr);
    d.rssi = rssi;
    d.kind = 1;
    d.hops = hops;
    d.text = text;
    std::string from, to;
    if (deCalls(text, from, to) && from != cfg.call) lastDe_ = from;
    tr("heard chat: " + text + " (SNR " + std::to_string(d.snr) + (hops ? ", via " + std::to_string(hops) : std::string()) + ")");
    addRecent(d);
}

std::string App::withId(const std::string& text, const std::string& to) const {
    std::string t = text;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    if (cfg.call.empty() || hasWord(t, cfg.call)) return t;
    std::string id = (to.empty() ? std::string() : to + " ") + "DE " + cfg.call;
    return t.empty() ? id : t + " " + id;
}

// ---------------------------------------------------------------- the log

std::string App::adifRecord(const QsoLog& q) const {
    char mhz[20];
    snprintf(mhz, sizeof mhz, "%.6f", cfg.freqHz / 1e6);
    std::string r;
    r += field("CALL", q.call);
    r += field("GRIDSQUARE", q.grid);
    r += field("MODE", mesh() ? cfg.adifMesh : cfg.adifMode);
    if (q.rptSent != Sequencer::UNSET) r += field("RST_SENT", std::to_string(q.rptSent));
    if (q.rptRcvd != Sequencer::UNSET) r += field("RST_RCVD", std::to_string(q.rptRcvd));
    r += field("QSO_DATE", utcDate(q.startMs));
    r += field("TIME_ON", utcTime(q.startMs));
    r += field("QSO_DATE_OFF", utcDate(q.endMs));
    r += field("TIME_OFF", utcTime(q.endMs));
    r += field("FREQ", mhz);
    r += field("BAND", adifBand(cfg.freqHz));
    r += field("STATION_CALLSIGN", cfg.call);
    if (cfg.operatorCall != cfg.call) r += field("OPERATOR", cfg.operatorCall);   // empty: left out
    r += field("MY_GRIDSQUARE", cfg.grid);
    r += field("PROGRAMID", "LoRaDeck");
    r += field("PROGRAMVERSION", version);
    r += field("COMMENT", std::string(mesh() ? "FT-MESH" : "FT-LoRa") + " SF" + std::to_string(cfg.sf) + " BW" + std::to_string(cfg.bwKhz));
    return r + "<EOR>\n";
}

bool App::appendLog(const std::string& rec, const std::string& call, int64_t whenMs) {   // lock held
    FILE* f = fopen(logPath().c_str(), "a");
    if (!f) { status("Could not write the log"); return false; }
    if (ftell(f) == 0) fputs("LoRaDeck log\n<ADIF_VER:5>3.1.4 <PROGRAMID:8>LoRaDeck <EOH>\n", f);
    fputs(rec.c_str(), f);
    fclose(f);
    logCount_++;
    worked_[call] = atoi(utcDate(whenMs).c_str());
    status("Logged " + call);
    tr("logged " + call);
    if (onLogged) onLogged(rec);
    return true;
}

void App::logQso(const QsoLog& q) { appendLog(adifRecord(q), q.call, q.endMs); }   // called by the sequencer

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

namespace {
std::string upperS(std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; }
std::string expand(std::string t, const std::string& my, const std::string& to) {   // macros: {MY} {TO}
    for (size_t p; (p = t.find("{MY}")) != std::string::npos;) t.replace(p, 4, my);
    for (size_t p; (p = t.find("{TO}")) != std::string::npos;) t.replace(p, 4, to);
    std::string o;                                            // an empty {TO} leaves a double space
    for (char c : t) if (!(c == ' ' && !o.empty() && o.back() == ' ')) o += c;
    while (!o.empty() && o.front() == ' ') o.erase(0, 1);
    return o;
}
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
        // GPS or NTP set within the last 30 minutes is better than the browser: keep it
        if (!clk::mayOverride(clk::SRC_BROWSER)) return ok();
        if (!clk::utcValid() || clk::source() == clk::SRC_NONE || llabs(ms - clk::utcMs()) > 150) {
            if (clk::source() != clk::SRC_NONE) tr("clock set from the browser, was " + std::to_string(clk::utcMs() - ms) + " ms off");
            else tr("clock set from the browser");
            clk::setUtcMs(ms, clk::SRC_BROWSER);
        }
        return ok();
    }
    if (c == "txen") {
        bool on = arg(a, "on") == "1";
        if (on && cfg.call.empty()) return err("Set your callsign first");
        seq.txEnabled = on;
        watchdogTripped_ = false;
        if (!on) dropOwn();
        if (on && seq.opMode == OP_AUTO_CQ && seq.dxCall.empty() && seq.txMsg == 0) seq.startCq();
        status(on ? "Transmit enabled" : "Transmit off");
        return ok();
    }
    if (c == "halt") {
        seq.halt();
        dropOwn();
        chatOut_.clear();
        cwOut_.clear();
        if (txOn_ && (cur_.kind == TX_CW || cur_.kind == TX_CWID) && radioAbort) radioAbort();
        status("Halted");
        return ok();
    }
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
            if (d.kind != 0) return err("That is a Terminal message: answer it on the Terminal page");
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
    // ---- Terminal: chat over LoRa, or real CW; "<to> DE <me>" added unless the text holds my call
    if (c == "chat" || c == "cw") {
        if (cfg.call.empty()) return err("Set your callsign first");
        std::string to = upperS(arg(a, "to"));
        std::string t = withId(expand(arg(a, "text"), cfg.call, to), to);
        if (c == "chat") {
            std::string clean;
            for (char ch : t) clean += (ch >= 0x20 && ch < 0x7F) ? ch : ' ';
            if (clean.size() > (size_t)CHAT_MAX) return err("Too long: " + std::to_string(clean.size()) + " characters with the calls, at most " + std::to_string(CHAT_MAX));
            if (chatOut_.size() >= 5) return err("Five messages are already waiting");
            chatOut_.push_back(clean);
            status("Message waiting for our next slot");
            return "{\"ok\":true,\"text\":" + jstr(clean) + "}";
        }
        std::string cw = morseClean(t);
        if (cw.empty()) return err("Nothing to send in CW");
        int ms = morseMs(cw, cfg.cwWpm);
        if (ms > 60000) return err("Too long for one CW message (" + std::to_string(ms / 1000) + " s at " + std::to_string(cfg.cwWpm) + " wpm; at most 60 s)");
        if (cwOut_.size() >= 3) return err("Three CW messages are already waiting");
        cwOut_.push_back(cw);
        status("CW waiting for our next slot (" + std::to_string(ms / 1000 + 1) + " s)");
        return "{\"ok\":true,\"text\":" + jstr(cw) + ",\"ms\":" + jnum(ms) + "}";
    }
    if (c == "logchat") {                                     // a contact made on the Terminal page
        std::string call = upperS(arg(a, "call"));
        Settings chk;
        if (call.empty() || !chk.set("call", call)) return err("Enter the other station's callsign");
        bool cw = arg(a, "mode") == "cw";
        int64_t end = now(), start = atoll(arg(a, "start").c_str());
        if (start < 1735689600000LL || start > end) start = end;
        char mhz[20];
        snprintf(mhz, sizeof mhz, "%.6f", cfg.freqHz / 1e6);
        std::string grid = upperS(arg(a, "grid"));
        if (!grid.empty() && !chk.set("grid", grid)) return err("That grid locator is not valid");
        std::string r;
        r += field("CALL", call);
        r += field("GRIDSQUARE", chk.grid);
        r += field("MODE", cw ? "CW" : cfg.adifChat);
        r += field("RST_SENT", arg(a, "rstS").substr(0, 8));
        r += field("RST_RCVD", arg(a, "rstR").substr(0, 8));
        r += field("NAME", arg(a, "name").substr(0, 40));
        r += field("QSO_DATE", utcDate(start));
        r += field("TIME_ON", utcTime(start));
        r += field("QSO_DATE_OFF", utcDate(end));
        r += field("TIME_OFF", utcTime(end));
        r += field("FREQ", mhz);
        r += field("BAND", adifBand(cfg.freqHz));
        r += field("STATION_CALLSIGN", cfg.call);
        if (cfg.operatorCall != cfg.call) r += field("OPERATOR", cfg.operatorCall);
        r += field("MY_GRIDSQUARE", cfg.grid);
        r += field("PROGRAMID", "LoRaDeck");
        r += field("PROGRAMVERSION", version);
        std::string note = arg(a, "note").substr(0, 80);
        r += field("COMMENT", std::string(cw ? "CW on 33 cm" : "LoRaDeck Terminal") + (note.empty() ? "" : ": " + note));
        r += "<EOR>\n";
        return appendLog(r, call, end) ? ok() : err("Could not write the log");
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
        bool modeChanged = n.ftMode != cfg.ftMode || n.meshSlotSec != cfg.meshSlotSec || n.slotSec != cfg.slotSec;
        cfg = n;
        if (!cfg.save(dir_ + "/settings.txt")) return err("Could not save the settings");
        syncSeq();
        seq.setOpMode(cfg.opMode);
        if (callChanged) loadLog();
        if (modeChanged) { txq_.clear(); rxSlot_.clear(); curSlot_ = -1; tr(std::string("mode ") + (mesh() ? "FT-MESH" : "FT-LoRa") + ", " + std::to_string(slotLen() / 1000) + " s slots"); }
        if (radio && radioConfigure) radioConfigure();
        status("Settings saved");
        return ok();
    }
    return err("unknown command");
}

std::string App::stateJson(uint32_t sinceRx, bool withCfg) {
    std::lock_guard<std::mutex> lk(mtx_);
    int64_t L = slotLen(), mono = clk::monoMs(), now = this->now();
    int wdLeft = -1;
    if (seq.txEnabled && cfg.watchdogMin > 0) {
        int64_t left = (int64_t)cfg.watchdogMin * 60000 - (mono - lastUiMono_);
        wdLeft = left > 0 ? (int)(left / 1000) : 0;
    }
    bool pending = false;
    for (const TxItem& it : txq_) if (it.kind == TX_FT) pending = true;
    bool ftOn = txOn_ && cur_.kind == TX_FT;
    std::string txText = ftOn ? cur_.text : pending ? ftText_ : (seq.txMsg ? seq.compose(seq.txMsg) : std::string());
    std::string o;
    o.reserve(4096);
    o += "{\"ver\":" + jstr(version) + ",\"call\":" + jstr(cfg.call) + ",\"grid\":" + jstr(cfg.grid);
    o += ",\"utc\":" + jnum(now) + ",\"clock\":" + jbool(clk::utcValid()) + ",\"clkSrc\":" + jstr(clk::sourceName(clk::source())) +
         ",\"clkAge\":" + jnum(clk::sinceSetMs() / 1000) + ",\"opCall\":" + jstr(cfg.operatorCall);
    o += ",\"mode\":" + jstr(mesh() ? "FT-MESH" : "FT-LoRa") + ",\"hops\":" + jnum(mesh() ? hopsFit() : 0) +
         ",\"relay\":" + jbool(relayOn()) + ",\"wpm\":" + jnum(cfg.cwWpm);
    o += ",\"slotSec\":" + jnum(L / 1000) + ",\"phase\":" + jnum(now % L) + ",\"parity\":" + jnum((now / L) & 1);
    o += ",\"radio\":{\"freq\":" + jnum(cfg.freqHz) + ",\"sf\":" + jnum(cfg.sf) + ",\"bw\":" + jnum(cfg.bwKhz) +
         ",\"cr\":" + jnum(cfg.cr) + ",\"pwr\":" + jnum(cfg.powerDbm) + ",\"band\":" + jstr(adifBand(cfg.freqHz)) + "}";
    o += ",\"tx\":{\"en\":" + std::string(jbool(seq.txEnabled)) + ",\"on\":" + jbool(txOn_) + ",\"kind\":" + jnum(txOn_ ? cur_.kind : -1) +
         ",\"pending\":" + jbool(pending) + ",\"msg\":" + jnum(seq.txMsg) + ",\"text\":" + jstr(txText) +
         ",\"dx\":" + jstr(seq.dxCall) + ",\"dxGrid\":" + jstr(seq.dxGrid) +
         ",\"rptS\":" + jnum(seq.rptSent) + ",\"rptR\":" + jnum(seq.rptRcvd) + ",\"op\":" + jnum(seq.opMode) +
         ",\"txParity\":" + jnum(seq.txParity) + ",\"retries\":" + jnum(seq.retries) +
         ",\"wait\":" + jbool(seq.waitNext) + ",\"wd\":" + jbool(watchdogTripped_) +
         ",\"wdLeft\":" + jnum(wdLeft) + ",\"wdMin\":" + jnum(cfg.watchdogMin) +
         ",\"chatQ\":" + jnum((int64_t)chatOut_.size()) + ",\"cwQ\":" + jnum((int64_t)cwOut_.size()) + "}";
    o += ",\"lastDe\":" + jstr(lastDe_);
    o += ",\"rx\":[";
    bool first = true;
    for (const Decode& d : recent_) {
        if (d.id <= sinceRx) continue;
        if (!first) o += ',';
        first = false;
        char dt[12];
        snprintf(dt, sizeof dt, "%.1f", d.dt);
        o += "{\"id\":" + jnum(d.id) + ",\"t\":" + jnum(d.slotMs) + ",\"snr\":" + jnum(d.snr) + ",\"rssi\":" +
             jnum(d.rssi) + ",\"p\":" + jnum(d.parity) + ",\"dt\":" + dt + ",\"k\":" + jnum(d.kind) + ",\"h\":" + jnum(d.hops) +
             ",\"text\":" + jstr(d.text) + "}";
    }
    o += "],\"log\":" + jnum(logCount_) + ",\"status\":" + jstr(status_);
    if (withCfg) o += ",\"cfg\":" + cfg.json(false);
    return o + "}";
}

void App::gpsGrid(const std::string& grid6) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!cfg.gpsGrid || grid6.size() != 6 || grid6 == cfg.grid) return;
    Settings n = cfg;
    if (!n.set("grid", grid6) || n.grid == cfg.grid) return;
    tr("grid from GPS: " + n.grid + (cfg.grid.empty() ? "" : " (was " + cfg.grid + ")"));
    cfg.grid = n.grid;
    cfg.save(dir_ + "/settings.txt");
    syncSeq();
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
