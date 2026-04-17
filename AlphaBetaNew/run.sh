#!/bin/bash
# run.sh — launch AlphaBetaNew (build first if binary is missing)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "$DIR/AlphaBetaNew" ]; then
    echo "Binary not found — building first..." >&2
    bash "$DIR/build.sh"
fi
exec "$DIR/AlphaBetaNew"
