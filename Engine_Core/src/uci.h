#pragma once
#ifndef UCI_H
#define UCI_H

#include <string>
#include <sstream>
#include <iostream>
#include <chrono>

// Forward declarations to avoid circular includes
struct Position;
struct Move;

namespace UCI {
    std::string moveToUCI(const Move& move);
    Move parseUCIMove(const Position& pos, const std::string& uciMove);
    void loop();
}

#endif // UCI_H
