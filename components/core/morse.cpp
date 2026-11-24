#include "morse.h"
#include <cctype>

const char* morseOf(char c) {
    static const char* const letters[26] = {".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
                                            "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
                                            "..-", "...-", ".--", "-..-", "-.--", "--.."};
    static const char* const digits[10] = {"-----", ".----", "..---", "...--", "....-",
                                           ".....", "-....", "--...", "---..", "----."};
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    switch (c) {
    case '/': return "-..-.";
    case '?': return "..--..";
    case '.': return ".-.-.-";
    case ',': return "--..--";
    case '=': return "-...-";      // BT
    case '+': return ".-.-.";      // AR
    case '-': return "-....-";
    default: return nullptr;
    }
}

char morseChar(const std::string& code) {
    static const char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?.,=+-";
    for (const char* c = set; *c; c++)
        if (code == morseOf(*c)) return *c;
    return 0;
}

std::string morseClean(const std::string& text) {
    std::string o;
    for (char c : text) {
        char u = (char)toupper((unsigned char)c);
        if (u == ' ' || u == '\t' || u == '\n') {
            if (!o.empty() && o.back() != ' ') o += ' ';
        } else if (morseOf(u)) {
            o += u;
        }
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

int morseUnits(const std::string& text) {
    std::string t = morseClean(text);
    int u = 0;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == ' ') { u += 4; continue; }             // 3 after the letter already counted + 4 = 7
        for (const char* p = morseOf(t[i]); *p; p++) u += (*p == '.' ? 1 : 3) + 1;
        u += 2;                                            // letter gap: 1 + 2 = 3
    }
    return u;
}
