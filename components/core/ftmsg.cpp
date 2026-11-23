#include "ftmsg.h"
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

bool ftlPack(const std::string& text, uint8_t out[FTL_PACKET]) {
    ftx_message_t msg;
    if (ftx_message_encode(&msg, &g_hashIf, text.c_str()) != FTX_MESSAGE_RC_OK) return false;
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
