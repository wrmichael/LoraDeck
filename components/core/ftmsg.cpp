#include "ftmsg.h"
#include <vector>
#include <cctype>
#include <cstring>
#include <mutex>
extern "C" {
#include "ft8/message.h"
}

// ---- callsign hash table (resolves <...> hashed callsigns), as in RigDeck --------------------------
namespace {
const int HT = 256;
struct HEnt { char call[12]; uint32_t hash; };
HEnt g_ht[HT];
int g_htNext = 0;
std::mutex g_htMtx;

void htSave(const char* callsign, uint32_t n22) {
    std::lock_guard<std::mutex> lk(g_htMtx);
    for (int i = 0; i < HT; i++)
        if (g_ht[i].call[0] && g_ht[i].hash == n22 && !strcmp(g_ht[i].call, callsign)) return;
    HEnt& e = g_ht[g_htNext];
    g_htNext = (g_htNext + 1) % HT;
    strncpy(e.call, callsign, 11);
    e.call[11] = 0;
    e.hash = n22;
}
bool htLookup(ftx_callsign_hash_type_t type, uint32_t hash, char* callsign) {
    int shift = (type == FTX_CALLSIGN_HASH_10_BITS) ? 12 : (type == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0);
    std::lock_guard<std::mutex> lk(g_htMtx);
    for (int i = 0; i < HT; i++)
        if (g_ht[i].call[0] && (g_ht[i].hash >> shift) == hash) { strcpy(callsign, g_ht[i].call); return true; }
    callsign[0] = 0;
    return false;
}
ftx_callsign_hash_interface_t g_hashIf = {htLookup, htSave};
}

// Our own call in the table from the start, so a message to our hashed call is recognised before we have sent.
void ftlRemember(const std::string& call) {
    if (call.empty()) return;
    uint8_t b[FTL_PACKET];
    ftlPack("CQ " + call, b);                                 // packing a call saves its hash
}
void ftlForget() { std::lock_guard<std::mutex> lk(g_htMtx); for (HEnt& e : g_ht) e = HEnt(); g_htNext = 0; }

// A call FT8 carries whole in every message (up to 6 characters, /P or /R allowed). Longer ones (AC9HP/LORA,
// VE3/AC9HP) are sent whole only in some messages and as a hash in the others: a station that has not heard
// the call whole sees <...>, and no grid goes with it.
bool ftlStandardCall(const std::string& call) {
    ftx_message_t msg;
    memset(&msg, 0, sizeof msg);
    if (call.empty() || ftx_message_encode_std(&msg, &g_hashIf, "W1AW", call.c_str(), "-12") != FTX_MESSAGE_RC_OK) return false;
    char text[FTX_MAX_MESSAGE_LENGTH];
    ftx_message_offsets_t off;
    if (ftx_message_decode(&msg, &g_hashIf, text, &off) != FTX_MESSAGE_RC_OK) return false;
    return std::string(text) == "W1AW " + call + " -12";      // a hashed call comes back as <CALL>
}

// With a long call of our own (not standard, see above), the first answer ("<to> <me> <grid>" or "<to> <me>") and
// the endings (RRR, RR73, 73) go as FT8 type 4: our call whole, theirs hashed, no grid, as WSJT-X does.
// Otherwise the station we call would only ever see our call as a hash, <...>, and could not answer.
// Reports stay standard (our call hashed): by then the other station has heard it whole.
static bool packType4(const std::string& text, ftx_message_t& msg) {
    memset(&msg, 0, sizeof msg);
    std::vector<std::string> t;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') i++;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') j++;
        if (j > i) t.push_back(text.substr(i, j - i));
        i = j;
    }
    if (t.size() < 2 || t.size() > 3 || t[0] == "CQ") return false;
    std::string x = t.size() == 3 ? t[2] : "";
    bool grid = x.size() == 4 && isupper((unsigned char)x[0]) && isupper((unsigned char)x[1]) &&
                isdigit((unsigned char)x[2]) && isdigit((unsigned char)x[3]) && x != "RR73";
    if (!(x.empty() || grid || x == "RRR" || x == "RR73" || x == "73")) return false;
    if (t[0][0] == '<' || t[1][0] == '<' || ftlStandardCall(t[1])) return false;   // theirs may be long too
    return ftx_message_encode_nonstd(&msg, &g_hashIf, t[0].c_str(), t[1].c_str(), grid ? "" : x.c_str()) == FTX_MESSAGE_RC_OK;
}

bool ftlPack(const std::string& text, uint8_t out[FTL_PACKET]) {
    ftx_message_t msg;
    if (!packType4(text, msg) && ftx_message_encode(&msg, &g_hashIf, text.c_str()) != FTX_MESSAGE_RC_OK) return false;
    out[0] = FTL_MAGIC;
    memcpy(out + 1, msg.payload, 10);
    return true;
}

bool ftlUnpack(const uint8_t* in, int len, std::string& text) {
    if (len != FTL_PACKET || in[0] != FTL_MAGIC) return false;
    ftx_message_t msg;
    memset(&msg, 0, sizeof msg);
    memcpy(msg.payload, in + 1, 10);
    msg.payload[9] &= 0xF8;                       // only 77 bits are used
    char buf[40];
    ftx_message_offsets_t off;
    if (ftx_message_decode(&msg, &g_hashIf, buf, &off) != FTX_MESSAGE_RC_OK) return false;
    text = buf;
    return !text.empty();
}
