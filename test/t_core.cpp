// Host tests: FT-LoRa message packing, and two complete LoRaDecks (two App instances) joined by a
// simulated LoRa channel on a simulated clock. A calls CQ automatically, B hunts; they must complete
// a QSO, send only in their own slots, and both log it.
#include "app.h"
#include "clock.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int64_t simMs = 0;
static int64_t T0 = 1794328740000LL;                    // 2026-11-10 16:39:00 UTC
static int64_t simUtc() { return T0 + simMs; }
static int64_t simMono() { return simMs; }

struct Air { int from; uint8_t d[FTL_PACKET]; int64_t endMs; };
static std::vector<Air> air;

struct Station {
    App app;
    int idx = 0;
    std::vector<int64_t> txAt;
    std::vector<std::string> logged;
    void init(int i, const char* dir, const char* call, const char* grid, int op) {
        idx = i;
        mkdir(dir, 0755);
        unlink((std::string(dir) + "/settings.txt").c_str());
        std::string file = call;
        for (char& c : file) if (c == '/') c = '_';
        unlink((std::string(dir) + "/" + file + "_log.adi").c_str());
        app.begin(dir);
        app.radioSend = [this](const uint8_t* d, int n) {
            CHECK(n == FTL_PACKET);
            Air a; a.from = idx; memcpy(a.d, d, FTL_PACKET); a.endMs = simMs + 200;
            air.push_back(a);
            txAt.push_back(simUtc());
            return true;
        };
        app.onLogged = [this](const std::string& r) { logged.push_back(r); };
        std::string r = app.command("set", {{"call", call}, {"grid", grid}, {"opMode", std::to_string(op)}, {"watchdogMin", "0"}});
        CHECK(r.find("\"ok\":true") != std::string::npos);
    }
};

int main() {
    // packing
    const char* msgs[] = {"CQ AC9HP EM69", "W1AW AC9HP -12", "W1AW AC9HP R-05", "W1AW AC9HP RR73", "CQ POTA AC9HP EM69", "AC9HP W1AW FN31"};
    for (const char* m : msgs) {
        uint8_t p[FTL_PACKET];
        std::string back;
        CHECK(ftlPack(m, p));
        CHECK(ftlUnpack(p, FTL_PACKET, back));
        CHECK(back == m);
        if (back != m) printf("  '%s' -> '%s'\n", m, back.c_str());
    }
    {
        uint8_t p[FTL_PACKET] = {0x12, 1, 2, 3};
        std::string t;
        CHECK(!ftlUnpack(p, FTL_PACKET, t));               // not ours
        CHECK(!ftlUnpack(p, 5, t));
    }
    CHECK(adifBand(915000000) == "33cm" && adifBand(433000000) == "70cm" && adifBand(868000000).empty());

    // two stations
    clk::simulate(simUtc, simMono);
    static Station A, B;
    A.init(0, "/tmp/ld_a", "AC9HP", "EM69", OP_AUTO_CQ);
    B.init(1, "/tmp/ld_b", "AC9HP/P", "EM69", OP_AUTO_HUNT);   // same operator, portable second station
    CHECK(A.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    CHECK(B.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    Station* st[2] = {&A, &B};
    for (simMs = 0; simMs < 120000; simMs += 10) {
        for (auto it = air.begin(); it != air.end();) {     // deliver finished packets
            if (it->endMs > simMs) { ++it; continue; }
            st[it->from]->app.onTxDone();
            st[1 - it->from]->app.onPacket(it->d, FTL_PACKET, -95, 6.0f);
            it = air.erase(it);
        }
        A.app.tick();
        B.app.tick();
        if (!A.logged.empty() && !B.logged.empty()) break;
    }
    CHECK(A.logged.size() == 1 && B.logged.size() == 1);
    if (!A.logged.empty()) {
        CHECK(A.logged[0].find("<CALL:7>AC9HP/P") != std::string::npos);
        CHECK(A.logged[0].find("<MODE:7>FT-LORA") != std::string::npos);
        CHECK(A.logged[0].find("<BAND:4>33cm") != std::string::npos);
        CHECK(A.logged[0].find("<RST_RCVD:") != std::string::npos);
    }
    if (!B.logged.empty()) CHECK(B.logged[0].find("<CALL:5>AC9HP") != std::string::npos);
    printf("QSO took %.1f s\n", simMs / 1000.0);
    CHECK(simMs < 60000);                                   // 5 s slots: well under a minute
    // A sends in even slots (cqParity 0), B in odd slots
    for (int64_t t : A.txAt) CHECK(((t / 5000) & 1) == 0);
    for (int64_t t : B.txAt) CHECK(((t / 5000) & 1) == 1);
    CHECK(A.app.logCount() == 1);
    // the log survives a restart
    {
        App again;
        again.begin("/tmp/ld_a");
        CHECK(again.logCount() == 1);
        CHECK(again.cfg.call == "AC9HP");
        App b2;                                             // "AC9HP/P" logs to AC9HP_P_log.adi
        b2.begin("/tmp/ld_b");
        CHECK(b2.logCount() == 1);
        CHECK(b2.logPath() == "/tmp/ld_b/AC9HP_P_log.adi");
    }
    {                                                       // a second station with the same call is reported
        uint8_t pk[FTL_PACKET];
        CHECK(ftlPack("CQ AC9HP EM69", pk));
        A.app.onPacket(pk, FTL_PACKET, -90, 5.0f);
        CHECK(A.app.status().find("same") != std::string::npos || A.app.status().find("your callsign") != std::string::npos);
    }
    std::string js = A.app.stateJson(0, true);
    CHECK(js.find("\"call\":\"AC9HP\"") != std::string::npos && js.find("\"cfg\":{") != std::string::npos);
    CHECK(js.find("apPass") == std::string::npos);        // no passwords to the page
    printf("t_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
