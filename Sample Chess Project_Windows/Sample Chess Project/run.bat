@echo off
REM Run tests for Sample Chess Project

if not exist build (
    call build.bat
)

ctest --test-dir build --verbose
