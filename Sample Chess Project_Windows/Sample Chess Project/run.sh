#!/bin/bash
# Run tests for Sample Chess Project

if [ ! -d build ]; then
    chmod +x build.sh
    ./build.sh
fi

ctest --test-dir build --verbose
