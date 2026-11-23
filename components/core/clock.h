// Time for the slot schedule. On the board: the system clock (set by NTP or by the browser); in the
// host tests: a simulated clock.
#pragma once
#include <cstdint>

namespace clk {
int64_t utcMs();               // milliseconds since 1970 (UTC)
int64_t monoMs();              // milliseconds since start, never jumps
void setUtcMs(int64_t ms);     // set the clock (browser, NTP)
bool utcValid();               // the clock has been set since start
// host tests: replace both clocks
void simulate(int64_t (*utc)(), int64_t (*mono)());
}
