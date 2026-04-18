@echo off
:: build.bat — compile AlphaBetaNew on Windows
:: Requires gcc (MinGW). Download from https://winlibs.com if not installed.

set DIR=%~dp0
gcc -O2 -o "%DIR%AlphaBetaNew.exe" "%DIR%AlphaBetaNew.c"
if %ERRORLEVEL% == 0 (
    echo Build successful: %DIR%AlphaBetaNew.exe
) else (
    echo Build failed. Make sure gcc is installed and on your PATH.
)
