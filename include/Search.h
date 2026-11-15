#pragma once

#include "Board.h"
#include "Move.h"
#include <cstdint>
#include <functional>
#include <optional>

class Search {
  public:
    struct IterationInfo {
        int depth;
        Move bestMove;
        int score;
        std::uint64_t nodes;
        std::uint64_t timeMs;
        std::uint64_t nps;
    };

    using IterationCallback = std::function<void(const IterationInfo&)>;

    static int negamax(Board& board, int depth, int ply = 0, int alpha = -1000000,
                       int beta = 1000000, std::uint64_t* nodes = nullptr,
                       std::uint64_t* qNodes = nullptr);
    static std::optional<Move> findBestMove(Board& board, int maxDepth,
                                            const IterationCallback& onIteration = {});
};