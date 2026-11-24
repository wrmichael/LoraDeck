#include "mopp.h"
#include "morse.h"

namespace {
struct Bits {
    std::vector<uint8_t> b;
    int n = 0;
    void put(unsigned v, int w) {
        for (int i = w - 1; i >= 0; i--) {
            if (n % 8 == 0) b.push_back(0);
            if ((v >> i) & 1) b.back() |= (uint8_t)(0x80 >> (n % 8));
            n++;
        }
    }
};
}

std::vector<uint8_t> moppEncode(const std::string& text, int wpm, int serial) {
    std::string t = morseClean(text);
    Bits o;
    o.put(1, 2);
    o.put((unsigned)serial & 63, 6);
    o.put((unsigned)(wpm < 5 ? 5 : wpm > 60 ? 60 : wpm), 6);
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == ' ') continue;
        for (const char* e = morseOf(t[i]); *e; e++) o.put(*e == '.' ? 1 : 2, 2);
        bool lastInWord = i + 1 >= t.size() || t[i + 1] == ' ';
        o.put(lastInWord ? 3 : 0, 2);
    }
    return o.b;
}

bool moppDecode(const uint8_t* d, int len, std::string& text, int& wpm, int& serial) {
    if (!looksMopp(d, len)) return false;
    auto bit = [&](int i) { return (d[i / 8] >> (7 - i % 8)) & 1; };
    auto get = [&](int at, int w) { unsigned v = 0; for (int i = 0; i < w; i++) v = v << 1 | bit(at + i); return v; };
    serial = (int)get(2, 6);
    wpm = (int)get(8, 6);
    if (wpm < 5 || wpm > 60) return false;
    text.clear();
    std::string code;
    int total = len * 8;
    for (int i = 14; i + 1 < total; i += 2) {
        unsigned s = get(i, 2);
        if (s == 1 || s == 2) { code += s == 1 ? '.' : '-'; if (code.size() > 7) return false; continue; }
        if (code.empty()) {                                      // 00 00 ...: the padding at the end
            if (s == 0) continue;
            if (!text.empty() && text.back() != ' ') text += ' ';
            continue;
        }
        char c = morseChar(code);
        if (!c) return false;
        text += c;
        code.clear();
        if (s == 3) text += ' ';
    }
    if (!code.empty()) {                                         // ended without a marker
        char c = morseChar(code);
        if (!c) return false;
        text += c;
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return !text.empty();
}

std::string cwCheck(const std::string& text) {
    uint16_t crc = 0xFFFF;                                       // CRC-16/CCITT over the message text (RigDeck)
    for (unsigned char ch : text) {
        crc ^= (uint16_t)ch << 8;
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    static const char a[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    unsigned v = crc % 1296;
    return std::string() + a[v / 36] + a[v % 36];
}

std::string cwFrame(const std::string& text) { return text + " " + cwCheck(text); }

bool cwUnframe(const std::string& framed, std::string& text) {
    if (framed.size() < 5 || framed[framed.size() - 3] != ' ') return false;
    text = framed.substr(0, framed.size() - 3);
    return cwCheck(text) == framed.substr(framed.size() - 2);
}
