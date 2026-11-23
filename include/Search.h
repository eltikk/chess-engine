#pragma once

#include "Board.h"
#include "Move.h"
#include "TranspositionTable.h"
#include <array>
#include <atomic>
#include <chrono>
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
        std::uint64_t ttHits;
        std::uint64_t ttCutoffs;
    };

    using IterationCallback = std::function<void(const IterationInfo&)>;
    using Clock = std::chrono::steady_clock;

    static constexpr int maxSearchDepth = 64;
    static constexpr std::int64_t moveOverheadMs = 30;
    static constexpr std::uint64_t stopCheckInterval = 2048;

    // No new iteration starts after the soft deadline; the hard deadline interrupts one in progress
    struct Limits {
        int depth = maxSearchDepth;
        Clock::time_point softDeadline = Clock::time_point::max();
        Clock::time_point hardDeadline = Clock::time_point::max();
    };

    // Once stopped is set, every search function returns at once and its score must be ignored
    struct Control {
        const std::atomic<bool>* stopRequested = nullptr;
        Clock::time_point hardDeadline = Clock::time_point::max();
        std::uint64_t checkCounter = 0;
        bool stopped = false;
    };

    // Quiet moves that caused a beta cutoff, two per ply, the most recent first
    using KillerTable = std::array<std::array<Move, 2>, maxSearchDepth>;

    // Scores of quiet moves that caused beta cutoffs, by side to move (1 for white), origin and
    // destination square
    using HistoryTable = std::array<std::array<std::array<int, 64>, 64>, 2>;

    // Quiet move ordering learned during one search and kept across its iterations
    struct MoveOrdering {
        // Below every capture and promotion score, so history only orders quiet moves
        static constexpr int maxHistory = 8192;

        KillerTable killers{};
        HistoryTable history{};

        // Rewards a quiet move that caused a beta cutoff, keeping the score within maxHistory
        void updateHistory(bool whiteToMove, const Move& move, int depth);
    };

    // If given, historyPly is lowered to the ply of the earliest position a repetition draw in this
    // search relied on (negative for positions before the root).
    static int negamax(Board& board, int depth, int ply = 0, int alpha = -1000000,
                       int beta = 1000000, std::uint64_t* nodes = nullptr,
                       std::uint64_t* qNodes = nullptr, TranspositionTable* tt = nullptr,
                       int* historyPly = nullptr, Control* control = nullptr,
                       MoveOrdering* ordering = nullptr);
    static std::optional<Move> findBestMove(Board& board, int maxDepth,
                                            const IterationCallback& onIteration = {});
    // Returns the best move of the last completed iteration, or a legal fallback move if none
    // completed. Returns nothing only when there are no legal moves. A given table keeps its
    // entries for later searches; otherwise the search uses a table of its own.
    static std::optional<Move> findBestMove(Board& board, const Limits& limits,
                                            const std::atomic<bool>* stopRequested,
                                            const IterationCallback& onIteration = {},
                                            TranspositionTable* tt = nullptr);

    // Time to spend on one move from the side to move's clock, always at least 1 ms
    static std::int64_t allocateTimeMs(std::int64_t timeMs, std::int64_t incMs,
                                       std::int64_t movesToGo);
};
