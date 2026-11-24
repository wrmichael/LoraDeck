#include "nmea.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <ctime>

namespace {
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> f;
    size_t p = 0;
    for (;;) {
        size_t c = s.find(',', p);
        f.push_back(s.substr(p, c == std::string::npos ? std::string::npos : c - p));
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return f;
}
int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
bool coord(const std::string& v, const std::string& hemi, int degDigits, double& out) {
    if (v.size() < (size_t)degDigits + 2) return false;
    double deg = atof(v.substr(0, degDigits).c_str()), min = atof(v.substr(degDigits).c_str());
    out = deg + min / 60.0;
    if (hemi == "S" || hemi == "W") out = -out;
    return true;
}
int64_t daysFromCivil(int y, unsigned m, unsigned d) {          // days since 1970-01-01 (H. Hinnant)
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}
}

bool Nmea::feed(char c) {
    if (c == '$') { buf_.clear(); in_ = true; return false; }
    if (!in_) return false;
    if (c == '\r' || c == '\n') {
        in_ = false;
        size_t star = buf_.find('*');
        if (star == std::string::npos || star + 3 > buf_.size()) return false;
        uint8_t sum = 0;
        for (size_t i = 0; i < star; i++) sum ^= (uint8_t)buf_[i];
        int h = hexv(buf_[star + 1]), l = hexv(buf_[star + 2]);
        if (h < 0 || l < 0 || sum != (uint8_t)(h * 16 + l)) return false;
        line_ = buf_.substr(0, star);
        parse(line_);
        return true;
    }
    if (buf_.size() < 120) buf_ += c;
    else in_ = false;
    return false;
}

void Nmea::parse(const std::string& s) {
    rmc_ = false;
    if (s.size() < 6) return;
    std::string type = s.substr(2, 3);                          // GP, GN, GL, BD ... talkers alike
    std::vector<std::string> f = split(s);
    if (type == "RMC" && f.size() >= 10) {
        rmc_ = true;
        const std::string &t = f[1], &st = f[2], &d = f[9];
        fix.timeValid = false;
        if (st == "A" && t.size() >= 6 && d.size() == 6) {
            int hh = atoi(t.substr(0, 2).c_str()), mm = atoi(t.substr(2, 2).c_str());
            double ss = atof(t.substr(4).c_str());
            int dd = atoi(d.substr(0, 2).c_str()), mo = atoi(d.substr(2, 2).c_str()), yy = atoi(d.substr(4, 2).c_str());
            if (mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && hh < 24 && mm < 60 && ss < 61) {
                int64_t days = daysFromCivil(2000 + yy, (unsigned)mo, (unsigned)dd);
                fix.utcMs = ((days * 24 + hh) * 60 + mm) * 60000LL + (int64_t)llround(ss * 1000);
                fix.timeValid = true;
            }
            double la, lo;
            if (coord(f[3], f[4], 2, la) && coord(f[5], f[6], 3, lo)) { fix.lat = la; fix.lon = lo; fix.posValid = true; }
        } else {
            fix.posValid = false;
        }
    } else if (type == "GGA" && f.size() >= 8) {
        fix.quality = atoi(f[6].c_str());
        fix.sats = atoi(f[7].c_str());
        double la, lo;
        if (fix.quality > 0 && coord(f[2], f[3], 2, la) && coord(f[4], f[5], 3, lo)) { fix.lat = la; fix.lon = lo; fix.posValid = true; }
    }
}

std::string maidenhead(double lat, double lon) {
    double x = lon + 180.0, y = lat + 90.0;
    if (x < 0) x = 0;
    if (x >= 360) x = 359.9999;
    if (y < 0) y = 0;
    if (y >= 180) y = 179.9999;
    char g[7];
    g[0] = (char)('A' + (int)(x / 20));
    g[1] = (char)('A' + (int)(y / 10));
    g[2] = (char)('0' + (int)(fmod(x, 20) / 2));
    g[3] = (char)('0' + (int)fmod(y, 10));
    g[4] = (char)('a' + (int)(fmod(x, 2) * 12));
    g[5] = (char)('a' + (int)(fmod(y, 1) * 24));
    g[6] = 0;
    return g;
}
