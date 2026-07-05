#pragma once
#ifndef DATAGEN_H
#define DATAGEN_H

#include <string>

// Self-play training data generation for NNUE.
// Plays `games` self-play games (random opening plies, then fixed-depth
// searches) and appends filtered positions to `outPath` as lines of
//   <fen> | <white-POV score cp> | <white-POV result 1|0.5|0>
void runDatagen(const std::string& outPath, int games, int depth, unsigned seed);

#endif // DATAGEN_H
