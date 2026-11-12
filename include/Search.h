#pragma once

#include "Board.h"
#include "Move.h"
#include <cstdint>
#include <optional>

class Search {
  public:
    static int negamax(Board& board, int depth, int ply = 0, int alpha = -1000000,
                       int beta = 1000000, std::uint64_t* nodes = nullptr);
    static std::optional<Move> findBestMove(Board& board, int depth);
};