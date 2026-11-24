// Time for the slot schedule. On the board: the system clock (set by GPS, NTP or the browser); in the
// host tests: a simulated clock.
#pragma once
#include <cstdint>

namespace clk {
// Where the time came from, worst to best (GPS with a PPS pulse is the best).
enum Source { SRC_NONE = 0, SRC_BROWSER = 1, SRC_GPS = 2, SRC_NTP = 3, SRC_GPS_PPS = 4 };

int64_t utcMs();               // milliseconds since 1970 (UTC)
int64_t monoMs();              // milliseconds since start, never jumps
void setUtcMs(int64_t ms, Source src = SRC_BROWSER);   // set the clock
void markSet(Source src);      // the clock was set by someone else (SNTP sets it itself)
bool utcValid();               // the clock has been set since start
Source source();               // who set it last
int64_t sinceSetMs();          // how long ago (monotonic); large if never
const char* sourceName(Source s);
// May a source of this quality set the clock now? A better source set within the last 30 minutes wins.
bool mayOverride(Source src);
// host tests: replace both clocks
void simulate(int64_t (*utc)(), int64_t (*mono)());
}
