#pragma once
#include "movegen.h"
#include <iostream>

uint64_t perft(Position& pos, int depth);
void perftDivide(Position& pos, int depth);
void benchmarkPerft(Position& pos, int depth, int runs = 3);
