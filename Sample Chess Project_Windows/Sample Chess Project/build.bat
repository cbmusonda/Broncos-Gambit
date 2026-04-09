@echo off
REM Build script for Sample Chess Project test_engine

if not exist build (
    mkdir build
)

cd build
cmake ..
cmake --build .
cd ..

echo Build complete. To run tests:
echo   ctest --test-dir build --verbose
