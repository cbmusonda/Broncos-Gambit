@echo off
cd /d "%~dp0"
gcc -O3 src/AlphaBetaV4.c -o engine.exe
pause