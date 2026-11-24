#include "clock.h"
#include <sys/time.h>
#include <time.h>

namespace {
int64_t (*s_utc)() = nullptr;
int64_t (*s_mono)() = nullptr;
bool s_valid = false;
clk::Source s_src = clk::SRC_NONE;
int64_t s_setMono = 0;
}

namespace clk {
int64_t utcMs() {
    if (s_utc) return s_utc();
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
int64_t monoMs() {
    if (s_mono) return s_mono();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void markSet(Source src) {
    s_valid = true;
    s_src = src;
    s_setMono = monoMs();
}
void setUtcMs(int64_t ms, Source src) {
    markSet(src);
    if (s_utc) return;
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    settimeofday(&tv, nullptr);
}
bool utcValid() { return s_valid || s_utc || utcMs() > 1735689600000LL; }   // after 2025-01-01: set by someone
Source source() { return s_src; }
int64_t sinceSetMs() { return s_src == SRC_NONE ? INT64_C(1) << 40 : monoMs() - s_setMono; }
const char* sourceName(Source s) {
    switch (s) {
    case SRC_BROWSER: return "browser";
    case SRC_GPS: return "GPS";
    case SRC_NTP: return "NTP";
    case SRC_GPS_PPS: return "GPS+PPS";
    default: return "not set";
    }
}
bool mayOverride(Source src) { return src >= s_src || sinceSetMs() > 30LL * 60000; }
void simulate(int64_t (*utc)(), int64_t (*mono)()) { s_utc = utc; s_mono = mono; s_valid = true; }
}
