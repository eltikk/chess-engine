#pragma once

#include "Board.h"
#include "Move.h"
#include <optional>

class Search {
public:
    static int negamax(Board& board, int depth, int ply = 0);
    static std::optional<Move> findBestMove(Board& board, int depth);
};