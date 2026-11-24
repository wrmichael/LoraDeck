// Console log in the board's flash, like RigDeck's console log to the card: everything LoRaDeck prints
// (ESP_LOG) also goes to /data/logs/console.txt. Kept small: when the file passes 128 KB it becomes
// console.old.txt (the previous old file is dropped), so at most about 256 KB and the oldest lines
// vanish first.
#pragma once
#include <string>

void conlog_start();                       // after /data is mounted
std::string conlog_read(size_t tailBytes); // 0 = all (old + current file)
void conlog_clear();
