// SSD1306 128x64 OLED on the Heltec V3 (I2C SDA 17, SCL 18, reset 21, powered by Vext = GPIO 36 low).
#pragma once
#include <cstdint>
namespace oled {
bool begin();
void clear();
void text(int col, int row, const char* s, bool invert = false);   // 21 columns x 8 rows of 6x8 characters
void bigText(int col, int row, const char* s);                     // double size: 10 columns, 2 rows high
void show();                                                       // send the frame to the display
void power(bool on);
// Cardputer: draw a whole 240x135 frame; fill(y, line, ctx) fills the 240 pixels of row y (RGB565, high byte
// first). Not used on the Heltec.
void frame(void (*fill)(int y, uint16_t* line, void* ctx), void* ctx);
}
