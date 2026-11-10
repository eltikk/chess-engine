#pragma once

#include <cstdint>
#include <vector>
#include "Move.h"


class Board;

struct PerftEntry{
    Move move; 
    std::uint64_t nodes;
};

class Perft{
public:
    static std::uint64_t run(Board& board, int depth);
    static std::vector<PerftEntry> divide(Board& board, int depth);
};