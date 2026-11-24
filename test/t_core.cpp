// Host tests: FT-LoRa message packing, and two complete LoRaDecks (two App instances) joined by a
// simulated LoRa channel on a simulated clock. A calls CQ automatically, B hunts; they must complete
// a QSO, send only in their own slots, and both log it.
#include "app.h"
#include "nmea.h"
#include "morse.h"
#include "mopp.h"
#include <cmath>
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

struct Air { int from; uint8_t d[FTL_PACKET]; int64_t startMs, endMs; };
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
            Air a; a.from = idx; memcpy(a.d, d, FTL_PACKET); a.startMs = simMs; a.endMs = simMs + 200;
            air.push_back(a);
            txAt.push_back(simUtc());
            return true;
        };
        app.onLogged = [this](const std::string& r) { logged.push_back(r); };
        std::string r = app.command("set", {{"call", call}, {"grid", grid}, {"opMode", std::to_string(op)}, {"watchdogMin", "0"}});
        CHECK(r.find("\"ok\":true") != std::string::npos);
    }
};

// ---- FT-MESH, chat and CW with N stations; links[i][j]: j hears i. Half duplex, collisions lose both packets.
struct Node {
    App app;
    int idx = 0;
    std::vector<std::string> logged, cw, chat;
    std::vector<std::pair<int64_t, int64_t>> on;             // when it was transmitting (sim ms)
    int relays = 0;
};
struct MAir { int from; std::vector<uint8_t> d; int64_t start, end; bool cw; };
static std::vector<MAir> mair;
static int airOf(int len) { return 150 + len * 5; }

static void meshTests() {
    const int N = 3;
    static Node n[N];
    bool links[N][N] = {{0, 1, 0}, {1, 0, 1}, {0, 1, 0}};    // A - R - B: A and B cannot hear each other
    const char* calls[N] = {"KC9AAA", "KC9RRR", "KC9BBB"};
    for (int i = 0; i < N; i++) {
        Node& x = n[i];
        x.idx = i;
        std::string dir = "/tmp/ld_m" + std::to_string(i);
        mkdir(dir.c_str(), 0755);
        unlink((dir + "/settings.txt").c_str());
        unlink((dir + "/" + calls[i] + "_log.adi").c_str());
        x.app.begin(dir);
        x.app.airtime = airOf;
        x.app.radioSend = [&x](const uint8_t* d, int len) {
            MAir a{x.idx, std::vector<uint8_t>(d, d + len), simMs, simMs + airOf(len), false};
            mair.push_back(a);
            x.on.push_back({a.start, a.end});
            if (d[0] == FTM_MAGIC && (d[1] & 15) > 0) x.relays++;
            return true;
        };
        x.app.radioCw = [&x](const std::string& t, int wpm) {
            MAir a{x.idx, {}, simMs, simMs + morseMs(t, wpm), true};
            mair.push_back(a);
            x.on.push_back({a.start, a.end});
            x.cw.push_back(t);
            return true;
        };
        x.app.onLogged = [&x](const std::string& r) { x.logged.push_back(r); };
        std::string r = x.app.command("set", {{"call", calls[i]}, {"grid", "EN61"}, {"watchdogMin", "0"}, {"ftMode", "1"},
                                              {"meshSlotSec", "15"}, {"meshHops", "3"}, {"opMode", i == 0 ? "1" : i == 2 ? "2" : "0"},
                                              {"meshRelay", i == 1 ? "2" : "1"}, {"cwIdMin", "10"}});
        CHECK(r.find("\"ok\":true") != std::string::npos);
    }
    CHECK(n[0].app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    CHECK(n[2].app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    auto step = [&]() {
        for (auto it = mair.begin(); it != mair.end();) {
            if (it->end > simMs) { ++it; continue; }
            n[it->from].app.onTxDone();
            if (!it->cw) {
                for (int j = 0; j < N; j++) {
                    if (!links[it->from][j]) continue;
                    bool lost = false;
                    for (auto& w : n[j].on) if (w.first < it->end && w.second > it->start) lost = true;   // was sending
                    for (const MAir& o : mair)                    // another signal it hears at the same time
                        if (&o != &*it && links[o.from][j] && o.start < it->end && o.end > it->start) lost = true;
                    if (!lost) n[j].app.onPacket(it->d.data(), (int)it->d.size(), -100, -5.0f + 5 * j);
                }
            }
            it = mair.erase(it);
        }
        for (Node& x : n) x.app.tick();
        simMs += 10;
    };
    int64_t start = simMs;
    while (simMs < start + 400000 && !(n[0].logged.size() && n[2].logged.size())) step();
    CHECK(n[0].logged.size() == 1 && n[2].logged.size() == 1);
    CHECK(n[1].logged.empty() && n[1].relays > 0);
    if (!n[0].logged.empty()) {
        CHECK(n[0].logged[0].find("<CALL:6>KC9BBB") != std::string::npos);
        CHECK(n[0].logged[0].find("<MODE:7>FT-MESH") != std::string::npos);
    }
    printf("FT-MESH QSO through a relay: %s in %.0f s, %d relays sent\n",
           n[0].logged.size() && n[2].logged.size() ? "logged" : "FAILED", (simMs - start) / 1000.0, n[1].relays);
    // the CW ID went out once each, in the quiet end of a slot
    CHECK(n[0].cw.size() == 1 && n[0].cw[0] == "DE KC9AAA");
    CHECK(n[1].cw.size() == 1 && n[1].cw[0] == "DE KC9RRR");      // relays transmit too: they identify

    // chat through the relay, "<to> DE <me>" added; B learns who called
    std::string r = n[0].app.command("chat", {{"text", "GM es tnx"}, {"to", "kc9bbb"}});
    CHECK(r.find("GM es tnx KC9BBB DE KC9AAA") != std::string::npos);
    for (int i = 0; i < 4000 && n[2].app.stateJson(0, false).find("\"lastDe\":\"KC9AAA\"") == std::string::npos; i++) step();
    CHECK(n[2].app.stateJson(0, false).find("\"lastDe\":\"KC9AAA\"") != std::string::npos);
    CHECK(n[2].app.stateJson(0, false).find("GM es tnx KC9BBB DE KC9AAA") != std::string::npos);
    // macros and CW
    r = n[2].app.command("cw", {{"text", "{TO} DE {MY} R TU 73 SK"}, {"to", "KC9AAA"}});
    CHECK(r.find("KC9AAA DE KC9BBB R TU 73 SK") != std::string::npos);
    size_t cwBefore = n[2].cw.size();
    for (int i = 0; i < 4000 && n[2].cw.size() == cwBefore; i++) step();
    CHECK(n[2].cw.size() == cwBefore + 1 && n[2].cw.back() == "KC9AAA DE KC9BBB R TU 73 SK");
    // log the Terminal contact
    r = n[2].app.command("logchat", {{"call", "kc9aaa"}, {"rstS", "599"}, {"rstR", "579"}, {"name", "Al"}, {"mode", "chat"}});
    CHECK(r.find("\"ok\":true") != std::string::npos && n[2].logged.size() == 2);
    if (n[2].logged.size() == 2) {
        CHECK(n[2].logged[1].find("<MODE:9>LORA-CHAT") != std::string::npos);
        CHECK(n[2].logged[1].find("<NAME:2>Al") != std::string::npos && n[2].logged[1].find("<RST_RCVD:3>579") != std::string::npos);
    }
    CHECK(n[2].app.command("logchat", {{"call", "no call"}}).find("false") != std::string::npos);
    // an FT-LoRa station does not take FT-MESH packets (and the other way round)
    {
        uint8_t ftl[FTL_PACKET];
        CHECK(ftlPack("CQ KC9ZZZ EN61", ftl));
        n[1].app.onPacket(ftl, FTL_PACKET, -90, 5);
        CHECK(n[1].app.stateJson(0, false).find("KC9ZZZ") == std::string::npos);
    }
}

// ---- FT-CW: FT messages in Morse (MOPP packets with RigDeck's check group); CW over LoRa on the Terminal
static void ftcwTests() {
    {
        uint8_t ex[] = {0x5B, 0x41, 0xA4, 0x61, 0x91, 0x45, 0x70};   // the MOPP specification's example
        std::vector<uint8_t> v = moppEncode("PARIS", 16, 27);
        CHECK(v.size() == 7 && memcmp(v.data(), ex, 7) == 0);
        std::string t; int w = 0, sn = 0;
        CHECK(moppDecode(ex, 7, t, w, sn) && t == "PARIS" && w == 16 && sn == 27);
        std::string m;
        CHECK(cwUnframe(cwFrame("CQ AC9HP EM69"), m) && m == "CQ AC9HP EM69");
        CHECK(!cwUnframe("CQ AC9HP EM69 ZZ", m));
    }
    const int N = 2;
    static Node n[N];
    const char* calls[N] = {"KC9CWA", "KC9CWB"};
    mair.clear();
    for (int i = 0; i < N; i++) {
        Node& x = n[i];
        x.idx = i;
        std::string dir = "/tmp/ld_c" + std::to_string(i);
        mkdir(dir.c_str(), 0755);
        unlink((dir + "/settings.txt").c_str());
        unlink((dir + "/" + calls[i] + "_log.adi").c_str());
        x.app.begin(dir);
        x.app.airtime = airOf;
        x.app.radioSend = [&x](const uint8_t* d, int len) {
            if (len > 0 && (d[0] >> 6) == 1) x.cw.push_back("mopp");
            MAir a{x.idx, std::vector<uint8_t>(d, d + len), simMs, simMs + airOf(len), false};
            mair.push_back(a);
            x.on.push_back({a.start, a.end});
            return true;
        };
        x.app.radioCw = [](const std::string&, int) { return false; };
        x.app.onLogged = [&x](const std::string& r) { x.logged.push_back(r); };
        CHECK(x.app.command("set", {{"call", calls[i]}, {"grid", "EN61"}, {"watchdogMin", "0"}, {"ftMode", "2"},
                                    {"opMode", i == 0 ? "1" : "2"}, {"cwIdMin", "0"}}).find("\"ok\":true") != std::string::npos);
        CHECK(x.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    }
    auto step = [&]() {
        for (auto it = mair.begin(); it != mair.end();) {
            if (it->end > simMs) { ++it; continue; }
            n[it->from].app.onTxDone();
            if (!it->cw) n[1 - it->from].app.onPacket(it->d.data(), (int)it->d.size(), -95, 3.0f);
            it = mair.erase(it);
        }
        for (Node& x : n) x.app.tick();
        simMs += 10;
    };
    int64_t start = simMs;
    while (simMs < start + 120000 && !(n[0].logged.size() && n[1].logged.size())) step();
    CHECK(n[0].logged.size() == 1 && n[1].logged.size() == 1);
    CHECK(!n[0].cw.empty());                                  // it went out as MOPP packets
    if (!n[0].logged.empty()) CHECK(n[0].logged[0].find("<MODE:5>FT-CW") != std::string::npos);
    printf("FT-CW QSO: %s in %.1f s\n", n[0].logged.size() && n[1].logged.size() ? "logged" : "FAILED", (simMs - start) / 1000.0);
    // CW over LoRa from the Terminal: shown with the sender's speed, the call learned
    std::string r = n[0].app.command("lcw", {{"text", "GM OM UR 599"}, {"to", "KC9CWB"}});
    CHECK(r.find("GM OM UR 599 KC9CWB DE KC9CWA") != std::string::npos);
    for (int i = 0; i < 2000 && n[1].app.stateJson(0, false).find("\"lastDe\":\"KC9CWA\"") == std::string::npos; i++) step();
    std::string js = n[1].app.stateJson(0, false);
    CHECK(js.find("\"k\":4,\"h\":0,\"w\":20,\"text\":\"GM OM UR 599 KC9CWB DE KC9CWA\"") != std::string::npos);
    // the same message twice is refused; waiting messages are listed and can be cancelled
    n[0].app.command("halt", {});
    r = n[0].app.command("lcw", {{"text", "TEST ONE"}});
    CHECK(r.find("true") != std::string::npos);
    CHECK(n[0].app.command("lcw", {{"text", "TEST ONE"}}).find("already waiting") != std::string::npos);
    CHECK(n[0].app.command("chat", {{"text", "hello"}}).find("true") != std::string::npos);
    js = n[0].app.stateJson(0, false);
    CHECK(js.find("\"waiting\":[") != std::string::npos && js.find("TEST ONE DE KC9CWA") != std::string::npos);
    CHECK(n[0].app.command("cancel", {{"k", "lcw"}, {"t", "TEST ONE DE KC9CWA"}}).find("true") != std::string::npos);
    js = n[0].app.stateJson(0, false);
    CHECK(js.find("\"t\":\"TEST ONE DE KC9CWA\"") == std::string::npos && js.find("\"t\":\"hello DE KC9CWA\"") != std::string::npos);
    CHECK(n[0].app.command("cancel", {{"k", "all"}}).find("true") != std::string::npos);
    CHECK(n[0].app.stateJson(0, false).find("\"waiting\":[]") != std::string::npos);
    CHECK(n[0].app.command("cancel", {{"k", "all"}}).find("Already sent") != std::string::npos);
    r = n[1].app.command("logchat", {{"call", "KC9CWA"}, {"mode", "lcw"}, {"rstS", "599"}});
    CHECK(r.find("true") != std::string::npos && n[1].logged.size() == 2 && n[1].logged.back().find("<MODE:7>LORA-CW") != std::string::npos);
}

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

    // GPS: NMEA time, position, locator; bad checksums ignored
    {
        Nmea n;
        auto feed = [&](const char* line) { bool got = false; for (const char* c = line; *c; c++) got |= n.feed(*c); got |= n.feed('\r'); n.feed('\n'); return got; };
        CHECK(feed("$GPRMC,235959.00,A,3953.1234,N,08611.5678,W,0.0,0.0,231126,,,A*42"));
        CHECK(n.lastWasRmc() && n.fix.timeValid && n.fix.utcMs == 1795478399000LL);
        CHECK(n.fix.posValid && fabs(n.fix.lat - 39.88539) < 1e-4 && fabs(n.fix.lon + 86.19280) < 1e-4);
        CHECK(maidenhead(n.fix.lat, n.fix.lon) == "EM69vv");
        CHECK(feed("$GNGGA,235959.00,3953.1234,N,08611.5678,W,1,08,1.0,250.0,M,-33.0,M,,*48"));
        CHECK(n.fix.quality == 1 && n.fix.sats == 8);
        CHECK(!feed("$GPRMC,235959.00,A,3953.1234,N,08611.5678,W,0.0,0.0,231126,,,A*43"));   // bad checksum
        CHECK(feed("$GPRMC,000001.00,V,,,,,,,241126,,,N*7E"));
        CHECK(!n.fix.timeValid);                            // no fix yet: not used for the clock
        CHECK(maidenhead(51.4778, -0.0015) == "IO91xl" || maidenhead(51.4778, -0.0015) == "JO01al");
        CHECK(maidenhead(-33.8688, 151.2093) == "QF56od");
    }

    // two stations
    clk::simulate(simUtc, simMono);
    static Station A, B;
    A.init(0, "/tmp/ld_a", "AC9HP", "EM69", OP_AUTO_CQ);
    B.init(1, "/tmp/ld_b", "AC9HP/P", "EM69", OP_AUTO_HUNT);   // same operator, portable second station
    A.app.airMs = B.app.airMs = 200;                          // the simulated air below takes 200 ms
    CHECK(A.app.command("set", {{"operator", "ac9hp"}}).find("true") != std::string::npos);
    CHECK(A.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    CHECK(B.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
    Station* st[2] = {&A, &B};
    auto run = [&](int64_t limitMs) {
        int64_t start = simMs;
        for (; simMs < start + limitMs; simMs += 10) {
            for (auto it = air.begin(); it != air.end();) {     // deliver finished packets
                if (it->endMs > simMs) { ++it; continue; }
                st[it->from]->app.onTxDone();
                // half duplex: a station that was sending meanwhile does not hear it
                bool deaf = false;
                for (const Air& o : air)
                    if (o.from != it->from && o.startMs < it->endMs && o.endMs > it->startMs) deaf = true;
                for (int64_t t : st[1 - it->from]->txAt)
                    if (t - T0 < it->endMs && t - T0 + 200 > it->startMs) deaf = true;
                if (!deaf) st[1 - it->from]->app.onPacket(it->d, FTL_PACKET, -95, 6.0f);
                it = air.erase(it);
            }
            A.app.tick();
            B.app.tick();
            if (!A.logged.empty() && !B.logged.empty()) break;
        }
        return simMs - start;
    };
    run(120000);
    CHECK(A.logged.size() == 1 && B.logged.size() == 1);
    if (!A.logged.empty()) {
        CHECK(A.logged[0].find("<CALL:7>AC9HP/P") != std::string::npos);
        CHECK(A.logged[0].find("<MODE:7>FT-LORA") != std::string::npos);
        CHECK(A.logged[0].find("<BAND:4>33cm") != std::string::npos);
        CHECK(A.logged[0].find("<RST_RCVD:") != std::string::npos);
        CHECK(A.logged[0].find("<OPERATOR:") == std::string::npos);   // operator = station call: left out
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
        CHECK(again.cfg.operatorCall == "AC9HP");
        App b2;                                             // "AC9HP/P" logs to AC9HP_P_log.adi
        b2.begin("/tmp/ld_b");
        CHECK(b2.logCount() == 1);
        CHECK(b2.logPath() == "/tmp/ld_b/AC9HP_P_log.adi");
    }
    // a club station call with the operator's own call: OPERATOR in the log
    {
        Settings c;
        CHECK(c.set("call", "w9aim") && c.set("operator", "AC9HP"));
        CHECK(!c.set("operator", "AC9 HP"));
        CHECK(!c.set("gpsRx", "9") && !c.set("gpsRx", "26") && c.set("gpsRx", "48") && c.set("gpsTx", "-1"));
    }
    // clocks that disagree: the second station 2 s behind, then 2 s ahead (half a slot is the limit)
    const char* calls[] = {"W9AIM", "K9ABC", "N9XYZ", "W9XYZ", "KD9AAA"};
    int round = 0;
    for (int64_t skew : {0LL, -400LL, -2000LL, 800LL, 2000LL}) {
        std::string bc = calls[round++];
        for (const char* dir : {"/tmp/ld_b"}) unlink((std::string(dir) + "/" + bc + "_log.adi").c_str());
        CHECK(B.app.command("set", {{"call", bc}}).find("true") != std::string::npos);   // not worked before
        A.logged.clear();
        B.logged.clear();
        air.clear();
        A.app.command("clear", {});
        B.app.command("clear", {});
        A.app.command("halt", {});
        B.app.command("halt", {});
        B.app.clockSkewMs = skew;
        simMs += 60000;
        A.app.command("set", {{"opMode", "1"}});
        B.app.command("set", {{"opMode", "2"}});
        CHECK(A.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
        CHECK(B.app.command("txen", {{"on", "1"}}).find("true") != std::string::npos);
        A.txAt.clear();
        B.txAt.clear();
        int64_t took = run(120000);
        bool ok = A.logged.size() == 1 && B.logged.size() == 1;
        // each station keeps to its own slots by its own clock: A even, B odd
        for (int64_t t : A.txAt) if (((t / 5000) & 1) != 0) ok = false;
        for (int64_t t : B.txAt) if ((((t + skew) / 5000) & 1) != 1) ok = false;
        CHECK(ok);
        printf("QSO with the second clock %+lld ms off: %s in %.1f s\n", (long long)skew, ok ? "logged" : "FAILED", took / 1000.0);
    }
    B.app.clockSkewMs = 0;
    {                                                       // a second station with the same call is reported
        uint8_t pk[FTL_PACKET];
        CHECK(ftlPack("CQ AC9HP EM69", pk));
        A.app.onPacket(pk, FTL_PACKET, -90, 5.0f);
        CHECK(A.app.status().find("same") != std::string::npos || A.app.status().find("your callsign") != std::string::npos);
    }
    std::string js = A.app.stateJson(0, true);
    CHECK(js.find("\"call\":\"AC9HP\"") != std::string::npos && js.find("\"cfg\":{") != std::string::npos);
    CHECK(js.find("apPass") == std::string::npos);        // no passwords to the page
    // ---- Morse, IDs
    CHECK(morseUnits("PARIS") == 46 && morseUnits("PARIS PARIS") == 96);
    CHECK(morseMs("PARIS PARIS", 20) == 96 * 60);
    CHECK(morseClean(" de  ac9hp/p ! ") == "DE AC9HP/P");
    CHECK(A.app.withId("TNX FER QSO", "W9AIM") == "TNX FER QSO W9AIM DE AC9HP");
    CHECK(A.app.withId("hello", "") == "hello DE AC9HP");
    CHECK(A.app.withId("W9AIM DE AC9HP K", "W9AIM") == "W9AIM DE AC9HP K");   // already identified

    meshTests();
    ftcwTests();
    printf("t_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
