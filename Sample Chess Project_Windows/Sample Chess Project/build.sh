#!/bin/bash
# Build script for Sample Chess Project test_engine

if [ ! -d build ]; then
    mkdir build
fi

cd build
cmake ..
cmake --build .
cd ..

echo "Build complete. To run tests:"
echo "  ctest --test-dir build --verbose"
