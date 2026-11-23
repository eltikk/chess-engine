#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include <atomic>
#include <climits>
#include <cstdint>
#include <vector>

namespace {

const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
const char* italian = "r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1";

// Only quiet moves are legal in these
const char* rookEnding = "4k3/8/8/8/8/8/8/R3K3 w - - 0 1";
const char* rookEndingBlack = "4k3/8/8/8/8/8/8/R3K3 b - - 0 1";

struct Scenario {
    const char* fen;
    int depth;
};

const Scenario scenarios[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {kiwipete, 3},
    {italian, 4},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
    {"4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1", 4},
};

Move quietMove(const char* from, const char* to) {
    return Move{static_cast<std::uint8_t>(Test::square(from)),
                static_cast<std::uint8_t>(Test::square(to)), MoveType::Quiet};
}

bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.promotionPiece == b.promotionPiece;
}

int historyTotal(const Search::HistoryTable& history) {
    int total = 0;

    for (const auto& side : history) {
        for (const auto& from : side) {
            for (const int score : from) {
                total += score;
            }
        }
    }

    return total;
}

bool withinBounds(const Search::HistoryTable& history) {
    for (const auto& side : history) {
        for (const auto& from : side) {
            for (const int score : from) {
                if (score < 0 || score > Search::MoveOrdering::maxHistory) {
                    return false;
                }
            }
        }
    }

    return true;
}

// Every move fails high against this window, so only the first move in the ordering is searched
void cutOffAtRoot(Board& board, int depth, Search::MoveOrdering& ordering) {
    Search::negamax(board, depth, 0, -1000000, -999999, nullptr, nullptr, nullptr, nullptr, nullptr,
                    &ordering);
}

std::vector<Search::IterationInfo> searchIterations(const char* fen, int depth) {
    Board board;
    board.loadFEN(fen);

    std::vector<Search::IterationInfo> iterations;
    Search::findBestMove(board, depth, [&iterations](const Search::IterationInfo& info) {
        iterations.push_back(info);
    });

    return iterations;
}

void expectSameIterations(const std::vector<Search::IterationInfo>& expected,
                          const std::vector<Search::IterationInfo>& actual) {
    EXPECT_EQ(expected.size(), actual.size());

    for (std::size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        EXPECT_EQ(expected[i].score, actual[i].score);
        EXPECT_EQ(expected[i].nodes, actual[i].nodes);
        EXPECT_EQ(expected[i].bestMove.toUCI(), actual[i].bestMove.toUCI());
    }
}

} // namespace

TEST_CASE("History scores stay bounded however often a move is rewarded") {
    constexpr int maxHistory = Search::MoveOrdering::maxHistory;
    const Move move = quietMove("e2", "e4");

    Search::MoveOrdering ordering;
    ordering.updateHistory(true, move, 3);
    EXPECT_EQ(9, ordering.history[1][move.from][move.to]);

    for (const int depth : {0, -5, 1, 7, Search::maxSearchDepth, 1000, INT_MAX}) {
        Search::MoveOrdering repeated;
        int previous = 0;
        bool monotonic = true;

        for (int i = 0; i < 20000; ++i) {
            repeated.updateHistory(false, move, depth);

            const int score = repeated.history[0][move.from][move.to];
            monotonic = monotonic && score >= previous;
            previous = score;
        }

        EXPECT_TRUE(monotonic);
        EXPECT_TRUE(withinBounds(repeated.history));
        EXPECT_EQ(previous, historyTotal(repeated.history));
    }

    Search::MoveOrdering saturated;
    saturated.history[1][move.from][move.to] = maxHistory;
    saturated.updateHistory(true, move, Search::maxSearchDepth);
    EXPECT_EQ(maxHistory, saturated.history[1][move.from][move.to]);
}

TEST_CASE("A quiet beta cutoff rewards the move for the side to move") {
    for (const char* fen : {rookEnding, rookEndingBlack}) {
        Board board;
        board.loadFEN(fen);

        Search::MoveOrdering ordering;
        cutOffAtRoot(board, 2, ordering);

        const Move rewarded = ordering.killers[0][0];
        EXPECT_FALSE(rewarded.from == rewarded.to);
        EXPECT_EQ(4, ordering.history[board.isWhiteToMove()][rewarded.from][rewarded.to]);
        EXPECT_EQ(4, historyTotal(ordering.history));
    }
}

TEST_CASE("A capture causing a beta cutoff is not rewarded") {
    Board board;
    board.loadFEN(kiwipete);

    Search::MoveOrdering ordering;
    ordering.history[1][Test::square("e1")][Test::square("f1")] = 5000;
    cutOffAtRoot(board, 2, ordering);

    // The capture is searched first despite the quiet move's history
    EXPECT_EQ(5000, historyTotal(ordering.history));
    EXPECT_TRUE(ordering.killers[0][0].from == ordering.killers[0][0].to);
}

TEST_CASE("History orders quiet moves after killer moves") {
    Board board;
    board.loadFEN(rookEnding);

    const Move preferred = quietMove("a1", "a7");
    const Move killer = quietMove("e1", "d2");

    Search::MoveOrdering byHistory;
    byHistory.history[1][preferred.from][preferred.to] = 100;
    cutOffAtRoot(board, 1, byHistory);

    EXPECT_TRUE(sameMove(preferred, byHistory.killers[0][0]));
    EXPECT_EQ(101, byHistory.history[1][preferred.from][preferred.to]);

    Search::MoveOrdering byKiller;
    byKiller.history[1][preferred.from][preferred.to] = 100;
    byKiller.killers[0][0] = killer;
    cutOffAtRoot(board, 1, byKiller);

    EXPECT_EQ(100, byKiller.history[1][preferred.from][preferred.to]);
    EXPECT_EQ(1, byKiller.history[1][killer.from][killer.to]);
}

TEST_CASE("History does not change the alpha-beta score") {
    bool anyHistory = false;

    for (const auto& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        // Learned across depths, as in iterative deepening
        Search::MoveOrdering learned;

        // Arbitrary scores for every move, as if left by unrelated positions
        Search::MoveOrdering arbitrary;
        std::uint32_t seed = 12345;

        for (auto& side : arbitrary.history) {
            for (auto& from : side) {
                for (int& score : from) {
                    seed = seed * 1664525 + 1013904223;
                    score = static_cast<int>(seed >> 8) % (Search::MoveOrdering::maxHistory + 1);
                }
            }
        }

        for (int depth = 1; depth <= scenario.depth; ++depth) {
            const int plainScore = Search::negamax(board, depth);

            EXPECT_EQ(plainScore, Search::negamax(board, depth, 0, -1000000, 1000000, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr, &learned));
            EXPECT_EQ(plainScore, Search::negamax(board, depth, 0, -1000000, 1000000, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr, &arbitrary));
        }

        anyHistory = anyHistory || historyTotal(learned.history) > 0;
        EXPECT_TRUE(withinBounds(learned.history));
        EXPECT_TRUE(withinBounds(arbitrary.history));
    }

    EXPECT_TRUE(anyHistory);
}

TEST_CASE("Searches with history are deterministic and independent") {
    const auto first = searchIterations(italian, 4);

    // History left by an unrelated search would change the second search's ordering
    searchIterations(kiwipete, 3);
    const auto second = searchIterations(italian, 4);

    expectSameIterations(first, second);
}

TEST_CASE("Interrupting a search restores the board and only records completed cutoffs") {
    Board board;
    board.loadFEN(italian);
    const Board original = board;
    constexpr int depth = 4;

    TranspositionTable completeTable(1);
    Search::MoveOrdering complete;
    Search::negamax(board, depth, 0, -1000000, 1000000, nullptr, nullptr, &completeTable, nullptr,
                    nullptr, &complete);

    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool allPrefixes = true;
    bool anyHistory = false;

    for (std::uint64_t call = 1; call <= Search::stopCheckInterval; call += 37) {
        TranspositionTable tt(1);
        Search::MoveOrdering interrupted;

        Search::Control control;
        control.stopRequested = &stopRequested;
        control.checkCounter = Search::stopCheckInterval - call;

        Search::negamax(board, depth, 0, -1000000, 1000000, nullptr, nullptr, &tt, nullptr,
                        &control, &interrupted);

        allRestored = allRestored && board.getHash() == original.getHash() &&
                      board.getHalfmoveClock() == original.getHalfmoveClock() &&
                      board.getHistory() == original.getHistory();

        // The interrupted search follows the complete one until it stops, so it can only have
        // made a subset of its updates; rewarding a cutoff from a stopped subtree would break this
        for (int side = 0; side < 2; ++side) {
            for (int from = 0; from < 64; ++from) {
                for (int to = 0; to < 64; ++to) {
                    allPrefixes = allPrefixes && interrupted.history[side][from][to] <=
                                                     complete.history[side][from][to];
                }
            }
        }

        anyHistory = anyHistory || historyTotal(interrupted.history) > 0;
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(allPrefixes);
    EXPECT_TRUE(anyHistory);
}
