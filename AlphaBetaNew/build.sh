#!/bin/bash
# build.sh — compile AlphaBetaNew
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
gcc -O2 -o "$DIR/AlphaBetaNew" "$DIR/AlphaBetaNew.c"
echo "Build successful: $DIR/AlphaBetaNew"
