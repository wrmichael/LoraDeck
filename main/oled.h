// SSD1306 128x64 OLED on the Heltec V3 (I2C SDA 17, SCL 18, reset 21, powered by Vext = GPIO 36 low).
#pragma once
namespace oled {
bool begin();
void clear();
void text(int col, int row, const char* s, bool invert = false);   // 21 columns x 8 rows of 6x8 characters
void bigText(int col, int row, const char* s);                     // double size: 10 columns, 2 rows high
void show();                                                       // send the frame to the display
void power(bool on);
}
