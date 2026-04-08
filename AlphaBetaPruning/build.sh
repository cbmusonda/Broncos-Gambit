#!/bin/bash
set -e
clang -O2 -std=c11 -Wall -Wextra -o AlphaBetaMAC AlphaBeta_Trial2.c
echo "Built ./AlphaBetaMAC"
