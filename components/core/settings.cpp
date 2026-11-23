#include "settings.h"
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
bool callOk(const std::string& s) {
    if (s.empty() || s.size() > 11) return false;
    for (char c : s) if (!isalnum((unsigned char)c) && c != '/') return false;
    return true;
}
bool gridOk(const std::string& s) {
    if (s.size() != 4 && s.size() != 6) return false;
    if (!isalpha((unsigned char)s[0]) || !isalpha((unsigned char)s[1]) || !isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3]))
        return false;
    if (s.size() == 6 && (!isalpha((unsigned char)s[4]) || !isalpha((unsigned char)s[5]))) return false;
    return true;
}
const char* const kKeys[] = {"call", "grid", "cqTag", "opMode", "maxRetries", "useRR73", "confirmQso", "cqParity",
                             "slotSec", "watchdogMin", "freqHz", "sf", "bwKhz", "cr", "powerDbm", "apSsid", "apPass",
                             "staSsid", "staPass", "adifMode", "logHost", "logPort"};
bool secret(const std::string& k) { return k == "apPass" || k == "staPass"; }
}

bool Settings::set(const std::string& k, const std::string& v) {
    long n = 0;
    if (k == "call") { std::string u = upper(v); if (!u.empty() && !callOk(u)) return false; call = u; return true; }
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
    return false;
}

std::string Settings::get(const std::string& k) const {
    if (k == "call") return call;
    if (k == "grid") return grid;
    if (k == "cqTag") return cqTag;
    if (k == "opMode") return std::to_string(opMode);
    if (k == "maxRetries") return std::to_string(maxRetries);
    if (k == "useRR73") return useRR73 ? "1" : "0";
    if (k == "confirmQso") return confirmQso ? "1" : "0";
    if (k == "cqParity") return std::to_string(cqParity);
    if (k == "slotSec") return std::to_string(slotSec);
    if (k == "watchdogMin") return std::to_string(watchdogMin);
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

bool Settings::save(const std::string& path) const {
    std::string tmp = path + ".new";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return false;
    std::string s = serialize(true);
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    ok = fclose(f) == 0 && ok;
    if (!ok) return false;
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
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
