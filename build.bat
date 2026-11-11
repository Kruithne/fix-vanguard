@echo off
gcc -std=gnu11 -O2 -s -static -municode -Wall -Wextra -o fix-vanguard.exe fix-vanguard.c -lshell32 -ladvapi32
