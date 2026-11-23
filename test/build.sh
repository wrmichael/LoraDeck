#!/bin/sh
# Host tests (no ESP-IDF needed): sh test/build.sh
set -e
mkdir -p test/out
gcc -O1 -c components/ft8msg/ft8/message.c -Icomponents/ft8msg -o test/out/message.o
gcc -O1 -c components/ft8msg/ft8/text.c -Icomponents/ft8msg -o test/out/text.o
g++ -std=c++17 -O1 -Wall -Icomponents/core -Icomponents/ft8msg test/t_core.cpp components/core/*.cpp \
    test/out/message.o test/out/text.o -lpthread -o test/out/t_core
./test/out/t_core
