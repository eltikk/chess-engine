#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

const char* startPosition = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
const char* italian = "r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1";

struct Scenario {
    const char* fen;
    int depth;
};

const Scenario scenarios[] = {
    {startPosition, 4},
    {kiwipete, 3},
    {italian, 4},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
    {"4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1", 4},
};

bool isEmpty(const Move& move) { return move.from == move.to; }

bool isQuiet(const Move& move) {
    return move.type == MoveType::Quiet || move.type == MoveType::DoublePawnPush ||
           move.type == MoveType::KingCastle || move.type == MoveType::QueenCastle;
}

bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.promotionPiece == b.promotionPiece;
}

bool validKillers(const Search::KillerTable& killers) {
    for (const auto& plyKillers : killers) {
        for (const Move& killer : plyKillers) {
            if (!isEmpty(killer) && !isQuiet(killer)) {
                return false;
            }
        }

        if (!isEmpty(plyKillers[0]) && sameMove(plyKillers[0], plyKillers[1])) {
            return false;
        }
    }

    return true;
}

bool hasKiller(const Search::KillerTable& killers) {
    for (const auto& plyKillers : killers) {
        if (!isEmpty(plyKillers[0])) {
            return true;
        }
    }

    return false;
}

std::optional<MoveType> legalMoveType(Board& board, const Move& move) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    for (int i = 0; i < moves.count; ++i) {
        if (sameMove(moves.moves[i], move)) {
            return moves.moves[i].type;
        }
    }

    return std::nullopt;
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

} // namespace

TEST_CASE("Killer moves do not change the alpha-beta score") {
    for (const auto& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        // Shared across depths, as in iterative deepening
        Search::MoveOrdering ordering;

        for (int depth = 1; depth <= scenario.depth; ++depth) {
            const int plainScore = Search::negamax(board, depth);
            const int killerScore = Search::negamax(board, depth, 0, -1000000, 1000000, nullptr,
                                                    nullptr, nullptr, nullptr, nullptr, &ordering);

            EXPECT_EQ(plainScore, killerScore);
        }
    }
}

TEST_CASE("Search records only distinct quiet killer moves") {
    bool anyKiller = false;

    for (const auto& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        TranspositionTable tt(1);
        Search::MoveOrdering ordering;

        for (int depth = 1; depth <= scenario.depth; ++depth) {
            Search::negamax(board, depth, 0, -1000000, 1000000, nullptr, nullptr, &tt, nullptr,
                            nullptr, &ordering);
        }

        anyKiller = anyKiller || hasKiller(ordering.killers);
        EXPECT_TRUE(validKillers(ordering.killers));
    }

    EXPECT_TRUE(anyKiller);
}

TEST_CASE("Killers that are not quiet moves in the position are ignored") {
    Board board;
    board.loadFEN(kiwipete);

    // A killer from a position where e5f7 was quiet, and one that is illegal here
    const Move capture{static_cast<std::uint8_t>(Test::square("e5")),
                       static_cast<std::uint8_t>(Test::square("f7")), MoveType::Quiet};
    const Move illegal{static_cast<std::uint8_t>(Test::square("a1")),
                       static_cast<std::uint8_t>(Test::square("a8")), MoveType::Quiet};

    EXPECT_TRUE(legalMoveType(board, capture) == MoveType::Capture);
    EXPECT_FALSE(legalMoveType(board, illegal).has_value());

    constexpr int depth = 3;

    Search::MoveOrdering emptyOrdering;
    std::uint64_t emptyNodes = 0;
    const int emptyScore = Search::negamax(board, depth, 0, -1000000, 1000000, &emptyNodes, nullptr,
                                           nullptr, nullptr, nullptr, &emptyOrdering);

    // The root never fails high with a full window, so its killers are never replaced
    Search::MoveOrdering ordering;
    ordering.killers[0] = {capture, illegal};
    std::uint64_t nodes = 0;
    const int score = Search::negamax(board, depth, 0, -1000000, 1000000, &nodes, nullptr, nullptr,
                                      nullptr, nullptr, &ordering);

    EXPECT_EQ(emptyScore, score);
    EXPECT_EQ(emptyNodes, nodes);
}

TEST_CASE("Killer moves do not carry over between searches") {
    // Killers left by the first search would be the most relevant to the second
    const auto first = searchIterations(italian, 4);
    const auto second = searchIterations(italian, 4);

    EXPECT_EQ(first.size(), second.size());

    for (std::size_t i = 0; i < first.size() && i < second.size(); ++i) {
        EXPECT_EQ(first[i].score, second[i].score);
        EXPECT_EQ(first[i].nodes, second[i].nodes);
        EXPECT_EQ(first[i].bestMove.toUCI(), second[i].bestMove.toUCI());
    }
}

TEST_CASE("Interrupting a search keeps the board and killer moves valid") {
    Board board;
    board.loadFEN(italian);
    const Board original = board;

    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool allValid = true;
    bool anyKiller = false;

    for (std::uint64_t call = 1; call <= Search::stopCheckInterval; call += 37) {
        TranspositionTable tt(1);
        Search::MoveOrdering ordering;

        Search::Control control;
        control.stopRequested = &stopRequested;
        control.checkCounter = Search::stopCheckInterval - call;

        Search::negamax(board, 4, 0, -1000000, 1000000, nullptr, nullptr, &tt, nullptr, &control,
                        &ordering);

        allRestored = allRestored && board.getHash() == original.getHash() &&
                      board.getHalfmoveClock() == original.getHalfmoveClock() &&
                      board.getHistory() == original.getHistory();
        allValid = allValid && validKillers(ordering.killers);
        anyKiller = anyKiller || hasKiller(ordering.killers);
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(allValid);
    EXPECT_TRUE(anyKiller);
}
