// Cardputer-Adv: its keyboard and the screens it drives (menu, Terminal, Wi-Fi, keys). The Heltec has no
// keyboard: there these do nothing.
#pragma once
#include "app.h"

void cardui_start(App& app);     // finds the keyboard (TCA8418 at I2C 0x34) and starts the screens' task
bool cardui_owns_screen();       // a screen other than Status is shown: the status task does not draw
bool cardui_has_keyboard();
