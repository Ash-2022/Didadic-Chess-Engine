#pragma once
#ifndef EVALUATION_H
#define EVALUATION_H

#include "position.h"
#include "attacks.h"

static constexpr int pieceValue[6] = { 100, 500, 320, 330, 900, 10000 };

void initEvaluation();
int evaluate(const Position& pos);

int evaluatePawnStructure(const Position& pos, Color side);
int evaluateKingSafety(const Position& pos, Color side);
int evaluateMobility(const Position& pos, Color side);
int evaluatePieces(const Position& pos, Color side);
int evaluatePassedPawns(const Position& pos, Color side);
int evaluateThreats(const Position& pos, Color side);

#endif // EVALUATION_H
