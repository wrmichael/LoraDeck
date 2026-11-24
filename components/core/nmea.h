// NMEA 0183 from a GPS module: time (RMC) and position / fix (RMC, GGA), and the Maidenhead locator.
#pragma once
#include <cstdint>
#include <string>

struct GpsFix {
    bool timeValid = false;     // RMC status A with a date
    int64_t utcMs = 0;          // the time in the last RMC (the second it starts)
    bool posValid = false;
    double lat = 0, lon = 0;    // degrees, north / east positive
    int quality = 0, sats = 0;  // from GGA (0 = no fix)
};

class Nmea {
public:
    // Feed bytes from the module; returns true when a sentence with a good checksum was complete.
    bool feed(char c);
    const std::string& last() const { return line_; }    // that sentence, without $ and checksum
    bool lastWasRmc() const { return rmc_; }
    GpsFix fix;
private:
    std::string buf_, line_;
    bool in_ = false, rmc_ = false;
    void parse(const std::string& s);
};

// 6-character locator (e.g. EN61fx) for a position.
std::string maidenhead(double lat, double lon);
