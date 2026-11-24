#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int infinity = 1000000;
constexpr int mateScore = 100000;

struct Scenario {
    const char* fen;
    int depth;
};

const Scenario scenarios[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1", 4},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4},
    {"4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1", 4},
};

// White mates in 2: three plies from the root
const char* mateInTwo = "7k/8/5K2/8/8/8/8/R7 w - - 0 1";

// Kxd1 is the only legal move, after which Black mates in 2: four plies from the root
const char* matedInTwo = "1k1r4/pp1b1R2/6pp/4p3/2B5/4Q3/PPP2B2/2Kq4 w - - 1 2";

// Material values used by M11's capture ordering
int orderingValue(char piece) {
    switch (piece) {
    case 'P':
    case 'p':
        return 100;
    case 'N':
    case 'n':
        return 320;
    case 'B':
    case 'b':
        return 330;
    case 'R':
    case 'r':
        return 500;
    case 'Q':
    case 'q':
        return 900;
    default:
        return 0;
    }
}

// M11's ordering without a table, killers or history: promotions and captures by MVV-LVA, then
// quiet moves. Sorting the same scores with the same comparator gives the engine's exact order.
void orderLikeM11(const Board& board, MoveList& moves) {
    constexpr int promotionValues[6] = {100, 320, 330, 500, 900, 0};

    struct ScoredMove {
        int score;
        Move move;
    };

    ScoredMove scored[MoveList::capacity];

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        int score = 0;

        switch (move.type) {
        case MoveType::PromotionCapture:
            score = 30000 + orderingValue(board.getPieceAt(move.to)) * 10 +
                    promotionValues[move.promotionPiece % 6];
            break;
        case MoveType::Promotion:
            score = 20000 + promotionValues[move.promotionPiece % 6];
            break;
        case MoveType::Capture:
            score = 10000 + orderingValue(board.getPieceAt(move.to)) * 10 -
                    orderingValue(board.getPieceAt(move.from));
            break;
        case MoveType::EnPassant:
            score = 10900;
            break;
        default:
            break;
        }

        scored[i] = ScoredMove{score, move};
    }

    std::sort(scored, scored + moves.count,
              [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

    for (int i = 0; i < moves.count; ++i) {
        moves.moves[i] = scored[i].move;
    }
}

// The search principal variation search replaced: M11's negamax without a table, plain fail-soft
// alpha-beta that searches every move with the full window, with the engine's draw rules, move
// ordering, node counting and quiescence search
int fullWindowAlphaBeta(Board& board, int depth, int ply, int alpha, int beta,
                        std::uint64_t* nodes = nullptr, std::uint64_t* qNodes = nullptr) {
    if (depth <= 0) {
        return Search::negamax(board, 0, ply, alpha, beta, nullptr, qNodes);
    }

    if (nodes != nullptr) {
        ++(*nodes);
    }

    if (ply > 0 && (board.repetitionDistance(ply) != 0 || board.isInsufficientMaterial())) {
        return 0;
    }

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        return MoveGenerator::isKingInCheck(board, board.isWhiteToMove()) ? -mateScore + ply : 0;
    }

    if (ply > 0 && board.isFiftyMoveRule()) {
        return 0;
    }

    orderLikeM11(board, moves);

    int bestScore = std::numeric_limits<int>::min();

    for (int i = 0; i < moves.count && alpha < beta; ++i) {
        UndoState undo;
        board.makeMove(moves.moves[i], undo);
        const int score =
            -fullWindowAlphaBeta(board, depth - 1, ply + 1, -beta, -alpha, nodes, qNodes);
        board.unmakeMove(moves.moves[i], undo);

        bestScore = std::max(bestScore, score);
        alpha = std::max(alpha, score);
    }

    return bestScore;
}

int exactScore(Board& board, int depth) {
    return fullWindowAlphaBeta(board, depth, 0, -infinity, infinity);
}

// A fail-soft result outside the window is a bound on the exact score; inside it, the exact score
bool consistentWithExact(int result, int alpha, int beta, int exact) {
    if (result <= alpha) {
        return exact <= result;
    }

    if (result >= beta) {
        return exact >= result;
    }

    return result == exact;
}

// Windows around the exact score, most of them a single point wide
std::vector<std::pair<int, int>> windowsAround(int exact) {
    std::vector<std::pair<int, int>> windows;

    for (int offset : {-300, -51, -50, -2, -1, 0, 1, 2, 49, 50, 300}) {
        windows.emplace_back(exact + offset - 1, exact + offset);
    }

    windows.emplace_back(exact - 1, exact + 1);
    windows.emplace_back(exact - 50, exact + 50);
    windows.emplace_back(exact, exact + 50);
    windows.emplace_back(exact - 50, exact);

    return windows;
}

bool sameBoardState(const Board& a, const Board& b) {
    for (int piece = 0; piece < PIECE_COUNT; ++piece) {
        if (a.getPieceBitboard(static_cast<Piece>(piece)) !=
            b.getPieceBitboard(static_cast<Piece>(piece))) {
            return false;
        }
    }

    return a.isWhiteToMove() == b.isWhiteToMove() && a.getHash() == b.getHash() &&
           a.getCastlingRights() == b.getCastlingRights() &&
           a.getEnPassantSquare() == b.getEnPassantSquare() &&
           a.getHalfmoveClock() == b.getHalfmoveClock() &&
           a.getFullmoveCounter() == b.getFullmoveCounter() && a.getHistory() == b.getHistory();
}

} // namespace

TEST_CASE("PVS returns the full-window alpha-beta score") {
    for (const Scenario& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        Search::MoveOrdering ordering;

        for (int depth = 1; depth <= scenario.depth; ++depth) {
            const int exact = exactScore(board, depth);

            EXPECT_EQ(exact, Search::negamax(board, depth));

            // Killers and history change which moves get a null window, not the score
            EXPECT_EQ(exact, Search::negamax(board, depth, 0, -infinity, infinity, nullptr, nullptr,
                                             nullptr, nullptr, nullptr, &ordering));
        }
    }
}

TEST_CASE("PVS searches fewer nodes than M11's full-window search") {
    // Without a table, killers or history, both searches order moves identically, so the node
    // counts differ only by PVS. Here PVS searched about 60% fewer nodes when this was written.
    Board board;
    board.loadFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    constexpr int depth = 5;

    std::uint64_t fullWindowNodes = 0;
    std::uint64_t fullWindowQNodes = 0;
    const int fullWindowScore = fullWindowAlphaBeta(board, depth, 0, -infinity, infinity,
                                                    &fullWindowNodes, &fullWindowQNodes);

    std::uint64_t pvsNodes = 0;
    std::uint64_t pvsQNodes = 0;
    const int pvsScore =
        Search::negamax(board, depth, 0, -infinity, infinity, &pvsNodes, &pvsQNodes);

    EXPECT_EQ(fullWindowScore, pvsScore);

    // At least a quarter fewer nodes, leaving a wide margin while still catching a PVS that is
    // disabled or re-searches far more than it should
    const std::uint64_t fullWindowTotal = fullWindowNodes + fullWindowQNodes;
    const std::uint64_t pvsTotal = pvsNodes + pvsQNodes;
    EXPECT_TRUE(pvsTotal * 4 <= fullWindowTotal * 3);
}

TEST_CASE("PVS results in narrow windows are bounds on the exact score") {
    for (const Scenario& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        const int depth = scenario.depth - 1;
        const int exact = exactScore(board, depth);

        // Shared by every window, as when a position is searched again with a different window
        TranspositionTable sharedTable(1);

        bool allConsistent = true;

        for (const auto& [alpha, beta] : windowsAround(exact)) {
            const int plain = Search::negamax(board, depth, 0, alpha, beta);

            TranspositionTable freshTable(1);
            const int fresh =
                Search::negamax(board, depth, 0, alpha, beta, nullptr, nullptr, &freshTable);

            const int shared =
                Search::negamax(board, depth, 0, alpha, beta, nullptr, nullptr, &sharedTable);

            allConsistent = allConsistent && consistentWithExact(plain, alpha, beta, exact) &&
                            consistentWithExact(fresh, alpha, beta, exact) &&
                            consistentWithExact(shared, alpha, beta, exact);
        }

        EXPECT_TRUE(allConsistent);

        // The shared table still gives the exact score to a full-window search
        EXPECT_EQ(exact, Search::negamax(board, depth, 0, -infinity, infinity, nullptr, nullptr,
                                         &sharedTable));
    }
}

TEST_CASE("Null windows at the exact score fail in the right direction and store bounds") {
    Board board;
    board.loadFEN(scenarios[2].fen);

    constexpr int depth = 3;
    const int exact = exactScore(board, depth);

    TranspositionTable tt(1);
    int score = 0;
    Move move{};

    // Proving the score is at least the exact score fails high and stores a lower bound
    const int high = Search::negamax(board, depth, 0, exact - 1, exact, nullptr, nullptr, &tt);
    EXPECT_TRUE(high >= exact);
    EXPECT_FALSE(tt.probe(board.getHash(), depth, -infinity, infinity, 0, score, move));
    EXPECT_FALSE(tt.probe(board.getHash(), depth, high, high + 1, 0, score, move));
    EXPECT_TRUE(tt.probe(board.getHash(), depth, high - 1, high, 0, score, move));
    EXPECT_EQ(high, score);

    // Proving it is at most the exact score fails low and replaces it with an upper bound
    const int low = Search::negamax(board, depth, 0, exact, exact + 1, nullptr, nullptr, &tt);
    EXPECT_TRUE(low <= exact);
    EXPECT_FALSE(tt.probe(board.getHash(), depth, -infinity, infinity, 0, score, move));
    EXPECT_FALSE(tt.probe(board.getHash(), depth, low - 1, low, 0, score, move));
    EXPECT_TRUE(tt.probe(board.getHash(), depth, low, low + 1, 0, score, move));
    EXPECT_EQ(low, score);

    // A full window stores the exact score, which every window can use
    EXPECT_EQ(exact, Search::negamax(board, depth, 0, -infinity, infinity, nullptr, nullptr, &tt));
    EXPECT_TRUE(tt.probe(board.getHash(), depth, -infinity, infinity, 0, score, move));
    EXPECT_EQ(exact, score);
}

TEST_CASE("PVS keeps mate scores and mate distances") {
    struct MateScenario {
        const char* fen;
        int depth;
        int expected;
    };

    const MateScenario mates[] = {
        {mateInTwo, 3, mateScore - 3},
        {mateInTwo, 5, mateScore - 3},
        {matedInTwo, 4, -(mateScore - 4)},
        {matedInTwo, 6, -(mateScore - 4)},
    };

    for (const MateScenario& mate : mates) {
        Board board;
        board.loadFEN(mate.fen);

        EXPECT_EQ(mate.expected, exactScore(board, mate.depth));
        EXPECT_EQ(mate.expected, Search::negamax(board, mate.depth));

        TranspositionTable tt(1);
        EXPECT_EQ(mate.expected, Search::negamax(board, mate.depth, 0, -infinity, infinity, nullptr,
                                                 nullptr, &tt));

        // Null and narrow windows around the mate score, as in the test above.
        bool allConsistent = true;

        for (const auto& [alpha, beta] : windowsAround(mate.expected)) {
            const int result =
                Search::negamax(board, mate.depth, 0, alpha, beta, nullptr, nullptr, &tt);
            allConsistent =
                allConsistent && consistentWithExact(result, alpha, beta, mate.expected);
        }

        EXPECT_TRUE(allConsistent);
    }

    // Found through iterative deepening as well; only mateInTwo exercises PVS at the root.
    for (const MateScenario& mate : {mates[1], mates[3]}) {
        Board board;
        board.loadFEN(mate.fen);

        int finalScore = 0;
        Search::findBestMove(board, mate.depth, [&finalScore](const Search::IterationInfo& info) {
            finalScore = info.score;
        });

        EXPECT_EQ(mate.expected, finalScore);
    }
}

TEST_CASE("PVS at the root reports exact scores when a later root move becomes best") {
    struct RootScenario {
        const char* fen;
        int depth;
        const char* finalBestMove;
    };

    // In the first two, the move searched first at the last depth is overtaken by a later one,
    // which only a full-window re-search at the root can score exactly. The third keeps d7c8q at
    // every depth while its score alternates between 493 and 438, so it checks exact scores
    // across iterations instead.
    const RootScenario roots[] = {
        {"2r3k1/1p2q1pp/2b1pr2/p1pp4/6Q1/1P1PP1R1/P1PN2PP/5RK1 w - - 0 1", 4, "g4g7"},
        {"7k/2p5/2p1r1p1/p6p/q2p1Q1P/5PP1/1R2P1K1/8 b - - 0 1", 4, "a4c4"},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, "d7c8q"},
    };

    for (const RootScenario& root : roots) {
        Board board;
        board.loadFEN(root.fen);

        std::vector<Search::IterationInfo> iterations;
        const auto bestMove = Search::findBestMove(
            board, root.depth,
            [&iterations](const Search::IterationInfo& info) { iterations.push_back(info); });

        EXPECT_EQ(static_cast<std::size_t>(root.depth), iterations.size());

        for (const Search::IterationInfo& info : iterations) {
            EXPECT_EQ(exactScore(board, info.depth), info.score);

            // The reported move achieves the reported score
            UndoState undo;
            board.makeMove(info.bestMove, undo);
            const int moveScore =
                -fullWindowAlphaBeta(board, info.depth - 1, 1, -infinity, infinity);
            board.unmakeMove(info.bestMove, undo);

            EXPECT_EQ(info.score, moveScore);
        }

        EXPECT_TRUE(bestMove.has_value());

        if (bestMove.has_value()) {
            EXPECT_EQ(std::string(root.finalBestMove), bestMove->toUCI());
        }
    }
}

TEST_CASE("Interrupting PVS in narrow windows restores the board and leaves a usable table") {
    Board board;
    board.loadFEN(scenarios[2].fen);
    const Board original = board;

    constexpr int depth = 3;
    const int exact = exactScore(board, depth);

    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool allStopped = true;
    bool allExactAfterwards = true;

    // A null window at the exact score stays a null window at every node, so it only interrupts
    // null-window searches. The wider windows also re-search later moves with a full window, so
    // their interruptions land in re-searches as well.
    for (const auto& [alpha, beta] : {std::pair{exact - 1, exact}, std::pair{exact - 1, exact + 50},
                                      std::pair{-infinity, infinity}}) {
        // Every node reads the stop condition once, so in a search set up exactly like the
        // interrupted ones below, these calls all fall inside the search
        std::uint64_t nodes = 0;
        std::uint64_t qNodes = 0;
        TranspositionTable sizingTable(1);
        Search::MoveOrdering sizingOrdering;
        Search::negamax(board, depth, 0, alpha, beta, &nodes, &qNodes, &sizingTable, nullptr,
                        nullptr, &sizingOrdering);
        const std::uint64_t lastCall = std::min(nodes + qNodes, Search::stopCheckInterval);

        for (std::uint64_t call = 1; call <= lastCall; call += 23) {
            TranspositionTable tt(1);
            Search::MoveOrdering ordering;

            Search::Control control;
            control.stopRequested = &stopRequested;
            control.checkCounter = Search::stopCheckInterval - call;

            Search::negamax(board, depth, 0, alpha, beta, nullptr, nullptr, &tt, nullptr, &control,
                            &ordering);

            allRestored = allRestored && sameBoardState(original, board);
            allStopped = allStopped && control.stopped;

            // Entries left by the interrupted search do not change a later complete search
            allExactAfterwards =
                allExactAfterwards &&
                Search::negamax(board, depth, 0, -infinity, infinity, nullptr, nullptr, &tt,
                                nullptr, nullptr, &ordering) == exact;
        }
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(allStopped);
    EXPECT_TRUE(allExactAfterwards);
}
