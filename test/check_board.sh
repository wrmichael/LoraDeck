#!/bin/sh
# Compile the board files that have stubs (no ESP-IDF here) as strictly as the real build:
# warnings as errors, newest C++, so -Werror surprises show up before a release on Wayne's PC.
# The stubs (test/idf_stubs) only declare what these files use; they are not ESP-IDF.
cd "$(dirname "$0")/.."
STD=gnu++23
for s in gnu++26 gnu++2c; do g++ -std=$s -fsyntax-only -x c++ /dev/null 2>/dev/null && STD=$s && break; done
F="-std=$STD -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-error=unused-function
   -Wno-error=unused-variable -Wvolatile -Wformat-truncation=1 -O2 -Itest/idf_stubs -Icomponents/core -Icomponents/ft8msg -Imain"
rc=0
for board in heltec_v3 cardputer_adv; do
    D=""; [ $board = cardputer_adv ] && D="-DLORADECK_CARDPUTER=1"
    for f in main/main.cpp main/sx1262.cpp main/conlog.cpp main/gps.cpp main/oled.cpp main/board_hw.cpp main/cardui.cpp main/net.cpp components/core/settings.cpp; do
        if g++ $F $D "$f"; then echo "ok   $board $f"; else echo "FAIL $board $f"; rc=1; fi
    done
done
exit $rc
