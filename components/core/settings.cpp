#include "settings.h"
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <vector>

namespace {
bool toInt(const std::string& v, long lo, long hi, long& out) {
    if (v.empty()) return false;
    char* e = nullptr;
    long n = strtol(v.c_str(), &e, 10);
    if (*e || n < lo || n > hi) return false;
    out = n;
    return true;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
// 3 to 11 characters (FT8's longest call), letters, digits and "/" between parts (VE3/AC9HP, AC9HP/P), with a digit.
bool callOk(const std::string& s) {
    if (s.size() < 3 || s.size() > 11 || s.front() == '/' || s.back() == '/' || s.find("//") != std::string::npos) return false;
    bool digit = false;
    int slashes = 0;
    for (char c : s) {
        if (c == '/') slashes++;
        else if (isdigit((unsigned char)c)) digit = true;
        else if (!isalpha((unsigned char)c)) return false;
    }
    return digit && slashes <= 2;
}
// GPIOs the Heltec V3 uses itself (radio, OLED, Vext, console) or that are not usable
bool pinOk(long p) {
    if (p == -1) return true;
    if (p < 0 || p > 48 || (p >= 22 && p <= 32)) return false;   // 22-25 do not exist, 26-32 are the flash
#if defined(LORADECK_CARDPUTER)
    // Cardputer-Adv + Cap LoRa-1262: radio, I2C, LCD, keyboard IRQ, battery, USB, audio, LED (GPS 13/15 allowed)
    static const int used[] = {3, 4, 5, 6, 14, 39, 40, 8, 9, 33, 34, 35, 36, 37, 38, 10, 11, 19, 20, 21, 41, 42, 43, 45, 46};
#else
    static const int used[] = {8, 9, 10, 11, 12, 13, 14, 17, 18, 21, 36, 43, 44};
#endif
    for (int u : used) if (p == u) return false;
    return true;
}
bool gridOk(const std::string& s) {
    if (s.size() != 4 && s.size() != 6) return false;
    if (!isalpha((unsigned char)s[0]) || !isalpha((unsigned char)s[1]) || !isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3]))
        return false;
    if (s.size() == 6 && (!isalpha((unsigned char)s[4]) || !isalpha((unsigned char)s[5]))) return false;
    return true;
}
const char* const kKeys[] = {"call", "operator", "grid", "cqTag", "opMode", "maxRetries", "useRR73", "confirmQso", "cqParity",
                             "slotSec", "watchdogMin", "ftMode", "meshSlotSec", "meshHops", "meshRelay", "colPlain", "cwWpm", "ftcwWpm", "cwIdMin", "freqHz", "sf", "bwKhz", "cr", "powerDbm", "apSsid", "apPass",
                             "staSsid", "staPass", "adifMode", "adifMesh", "adifChat", "adifFtcw", "adifLcw", "logHost", "logPort",
                             "gpsRx", "gpsTx", "gpsBaud", "gpsPps", "gpsGrid",
                             "macro1", "macro2", "macro3", "macro4", "macro5", "macro6"};
bool secret(const std::string& k) { return k == "apPass" || k == "staPass"; }
}

bool Settings::set(const std::string& k, const std::string& v) {
    long n = 0;
    if (k == "call") { std::string u = upper(v); if (!u.empty() && !callOk(u)) return false; call = u; return true; }
    if (k == "operator") { std::string u = upper(v); if (!u.empty() && !callOk(u)) return false; operatorCall = u; return true; }
    if (k == "grid") { std::string u = upper(v); if (!u.empty() && !gridOk(u)) return false;
                       if (u.size() == 6) { u[4] = (char)tolower(u[4]); u[5] = (char)tolower(u[5]); } grid = u; return true; }
    if (k == "cqTag") {
        std::string u = upper(v);
        if (u.size() > 4) return false;
        for (char c : u)
            if (!isalnum((unsigned char)c)) return false;
        cqTag = u;
        return true;
    }
    if (k == "opMode") { if (!toInt(v, 0, 2, n)) return false; opMode = (int)n; return true; }
    if (k == "maxRetries") { if (!toInt(v, 1, 20, n)) return false; maxRetries = (int)n; return true; }
    if (k == "useRR73") { if (!toInt(v, 0, 1, n)) return false; useRR73 = n; return true; }
    if (k == "confirmQso") { if (!toInt(v, 0, 1, n)) return false; confirmQso = n; return true; }
    if (k == "cqParity") { if (!toInt(v, 0, 1, n)) return false; cqParity = (int)n; return true; }
    if (k == "slotSec") { if (!toInt(v, 2, 15, n)) return false; slotSec = (int)n; return true; }
    if (k == "watchdogMin") { if (!toInt(v, 0, 120, n)) return false; watchdogMin = (int)n; return true; }
    if (k.size() == 6 && k.compare(0, 5, "macro") == 0 && k[5] >= '1' && k[5] <= '6') {
        if (v.size() > 80) return false;
        for (char c : v) if ((unsigned char)c < 0x20) return false;
        cwMacro[k[5] - '1'] = upper(v);
        return true;
    }
    if (k == "ftMode") { if (!toInt(v, 0, 2, n)) return false; ftMode = (int)n; return true; }
    if (k == "ftcwWpm") { if (!toInt(v, 20, 60, n)) return false; ftcwWpm = (int)n; return true; }
    if (k == "adifFtcw") { std::string u = upper(v); if (u.empty() || u.size() > 16) return false; adifFtcw = u; return true; }
    if (k == "adifLcw") { std::string u = upper(v); if (u.empty() || u.size() > 16) return false; adifLcw = u; return true; }
    if (k == "meshSlotSec") { if (!toInt(v, 6, 60, n)) return false; meshSlotSec = (int)n; return true; }
    if (k == "meshHops") { if (!toInt(v, 1, 5, n)) return false; meshHops = (int)n; return true; }
    if (k == "meshRelay") { if (!toInt(v, 0, 2, n)) return false; meshRelay = (int)n; return true; }
    if (k == "colPlain") { if (!toInt(v, 0, 1, n)) return false; colPlain = n; return true; }
    if (k == "cwWpm") { if (!toInt(v, 5, 40, n)) return false; cwWpm = (int)n; return true; }
    if (k == "cwIdMin") { if (!toInt(v, 0, 30, n)) return false; cwIdMin = (int)n; return true; }
    if (k == "adifMesh") { std::string u = upper(v); if (u.empty() || u.size() > 16) return false; adifMesh = u; return true; }
    if (k == "adifChat") { std::string u = upper(v); if (u.empty() || u.size() > 16) return false; adifChat = u; return true; }
    if (k == "freqHz") { if (!toInt(v, 150000000, 960000000, n)) return false; freqHz = (uint32_t)n; return true; }
    if (k == "sf") { if (!toInt(v, 7, 12, n)) return false; sf = (int)n; return true; }
    if (k == "bwKhz") { if (!toInt(v, 125, 500, n) || (n != 125 && n != 250 && n != 500)) return false; bwKhz = (int)n; return true; }
    if (k == "cr") { if (!toInt(v, 5, 8, n)) return false; cr = (int)n; return true; }
    if (k == "powerDbm") { if (!toInt(v, -9, 22, n)) return false; powerDbm = (int)n; return true; }
    if (k == "apSsid") { if (v.empty() || v.size() > 32) return false; apSsid = v; return true; }
    if (k == "apPass") { if (v.size() < 8 || v.size() > 63) return false; apPass = v; return true; }
    if (k == "staSsid") { if (v.size() > 32) return false; staSsid = v; return true; }
    if (k == "staPass") { if (v.size() > 63) return false; staPass = v; return true; }
    if (k == "adifMode") { std::string u = upper(v); if (u.empty() || u.size() > 16) return false; adifMode = u; return true; }
    if (k == "logHost") { if (v.size() > 63) return false; logHost = v; return true; }
    if (k == "logPort") { if (!toInt(v, 1, 65535, n)) return false; logPort = (int)n; return true; }
    if (k == "gpsRx") { if (!toInt(v, -1, 48, n) || !pinOk(n)) return false; gpsRx = (int)n; return true; }
    if (k == "gpsTx") { if (!toInt(v, -1, 48, n) || !pinOk(n)) return false; gpsTx = (int)n; return true; }
    if (k == "gpsPps") { if (!toInt(v, -1, 48, n) || !pinOk(n)) return false; gpsPps = (int)n; return true; }
    if (k == "gpsBaud") { if (!toInt(v, 4800, 921600, n)) return false; gpsBaud = (int)n; return true; }
    if (k == "gpsGrid") { if (!toInt(v, 0, 1, n)) return false; gpsGrid = n; return true; }
    return false;
}

std::string Settings::get(const std::string& k) const {
    if (k == "call") return call;
    if (k == "operator") return operatorCall;
    if (k == "grid") return grid;
    if (k == "cqTag") return cqTag;
    if (k == "opMode") return std::to_string(opMode);
    if (k == "maxRetries") return std::to_string(maxRetries);
    if (k == "useRR73") return useRR73 ? "1" : "0";
    if (k == "confirmQso") return confirmQso ? "1" : "0";
    if (k == "cqParity") return std::to_string(cqParity);
    if (k == "slotSec") return std::to_string(slotSec);
    if (k == "watchdogMin") return std::to_string(watchdogMin);
    if (k.size() == 6 && k.compare(0, 5, "macro") == 0 && k[5] >= '1' && k[5] <= '6') return cwMacro[k[5] - '1'];
    if (k == "ftMode") return std::to_string(ftMode);
    if (k == "ftcwWpm") return std::to_string(ftcwWpm);
    if (k == "adifFtcw") return adifFtcw;
    if (k == "adifLcw") return adifLcw;
    if (k == "meshSlotSec") return std::to_string(meshSlotSec);
    if (k == "meshHops") return std::to_string(meshHops);
    if (k == "meshRelay") return std::to_string(meshRelay);
    if (k == "colPlain") return colPlain ? "1" : "0";
    if (k == "cwWpm") return std::to_string(cwWpm);
    if (k == "cwIdMin") return std::to_string(cwIdMin);
    if (k == "adifMesh") return adifMesh;
    if (k == "adifChat") return adifChat;
    if (k == "freqHz") return std::to_string(freqHz);
    if (k == "sf") return std::to_string(sf);
    if (k == "bwKhz") return std::to_string(bwKhz);
    if (k == "cr") return std::to_string(cr);
    if (k == "powerDbm") return std::to_string(powerDbm);
    if (k == "apSsid") return apSsid;
    if (k == "apPass") return apPass;
    if (k == "staSsid") return staSsid;
    if (k == "staPass") return staPass;
    if (k == "adifMode") return adifMode;
    if (k == "logHost") return logHost;
    if (k == "logPort") return std::to_string(logPort);
    if (k == "gpsRx") return std::to_string(gpsRx);
    if (k == "gpsTx") return std::to_string(gpsTx);
    if (k == "gpsPps") return std::to_string(gpsPps);
    if (k == "gpsBaud") return std::to_string(gpsBaud);
    if (k == "gpsGrid") return gpsGrid ? "1" : "0";
    return "";
}

std::string Settings::serialize(bool secrets) const {
    std::string o;
    for (const char* k : kKeys) {
        if (!secrets && secret(k)) continue;
        o += k;
        o += '=';
        o += get(k);
        o += '\n';
    }
    return o;
}

std::string Settings::json(bool secrets) const {
    std::string o = "{";
    bool first = true;
    for (const char* k : kKeys) {
        if (!secrets && secret(k)) continue;
        if (!first) o += ',';
        first = false;
        o += '"';
        o += k;
        o += "\":\"";
        for (char c : get(k)) {
            if (c == '"' || c == '\\') o += '\\';
            if ((unsigned char)c >= 0x20) o += c;
        }
        o += '"';
    }
    return o + "}";
}

bool Settings::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        set(s.substr(0, eq), s.substr(eq + 1));
    }
    fclose(f);
    return true;
}

std::string Settings::lastError;

// Written to a new file first and then renamed, so a power cut cannot leave half a file. If that does not work,
// the file is written in place (better than not saving at all), and the reason is kept for the page.
bool Settings::save(const std::string& path) const {
    std::string s = serialize(true), tmp = path + ".new";
    auto write = [&](const std::string& p, const char* what) {
        FILE* f = fopen(p.c_str(), "w");
        if (!f) { lastError = std::string(what) + ": cannot open (" + strerror(errno) + ")"; return false; }
        bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
        if (!ok) lastError = std::string(what) + ": write failed (" + strerror(errno) + ")";
        if (fclose(f) != 0 && ok) { ok = false; lastError = std::string(what) + ": close failed (" + strerror(errno) + ")"; }
        return ok;
    };
    if (write(tmp, "new file")) {
        remove(path.c_str());
        if (rename(tmp.c_str(), path.c_str()) == 0) { lastError.clear(); return true; }
        lastError = std::string("rename failed (") + strerror(errno) + ")";
    }
    remove(tmp.c_str());
    if (write(path, "settings file")) { lastError.clear(); return true; }
    return false;
}

std::string adifBand(uint32_t hz) {
    struct B { uint32_t lo, hi; const char* name; };
    static const B bands[] = {{144000000, 148000000, "2m"},   {219000000, 225000000, "1.25m"},
                              {420000000, 450000000, "70cm"}, {902000000, 928000000, "33cm"},
                              {1240000000u, 1300000000u, "23cm"}};
    for (const B& b : bands)
        if (hz >= b.lo && hz <= b.hi) return b.name;
    return "";
}
