#include "clock.h"
#include <sys/time.h>
#include <time.h>

namespace {
int64_t (*s_utc)() = nullptr;
int64_t (*s_mono)() = nullptr;
bool s_valid = false;
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
void setUtcMs(int64_t ms) {
    s_valid = true;
    if (s_utc) return;
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    settimeofday(&tv, nullptr);
}
bool utcValid() { return s_valid || s_utc || utcMs() > 1735689600000LL; }   // after 2025-01-01: set by someone
void simulate(int64_t (*utc)(), int64_t (*mono)()) { s_utc = utc; s_mono = mono; s_valid = true; }
}
