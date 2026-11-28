#include "Evaluation.h"
#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr int infinity = 1000000;
constexpr int mateScore = 100000;

const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

// Ten captures and promotions, ordered before every quiet move, and quiet moves of which only Nd6
// gives check
const char* manyTactics = "r3k3/1P6/8/3p1p2/2N1B3/8/5PPP/R3K2R w KQq - 0 1";

// After 1.Rf1 the g1 rook is pinned and Black, with no spare piece moves, is in zugzwang
const char* rookZugzwang = "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1";

// A null window no move reaches, so a node searches every move and every reduced search fails low
constexpr int unreachable = 5000;

// The children of a node searched at this depth are too shallow to reduce, so only that node
// reduces and a full-depth search of one of its moves is exact
constexpr int nodeOnlyDepth = Search::lateMoveReductionMinDepth;

bool isQuietType(const Move& move) {
    return move.type == MoveType::Quiet || move.type == MoveType::DoublePawnPush ||
           move.type == MoveType::KingCastle || move.type == MoveType::QueenCastle;
}

bool givesCheck(Board& board, const Move& move) {
    UndoState undo;
    board.makeMove(move, undo);
    const bool check = MoveGenerator::isKingInCheck(board, board.isWhiteToMove());
    board.unmakeMove(move, undo);
    return check;
}

struct MoveCounts {
    int tactical = 0;
    int quietChecks = 0;
    int reducible = 0;
};

MoveCounts countMoves(Board& board) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);
    MoveCounts counts;

    for (int i = 0; i < moves.count; ++i) {
        if (!isQuietType(moves.moves[i])) {
            ++counts.tactical;
        } else if (givesCheck(board, moves.moves[i])) {
            ++counts.quietChecks;
        } else {
            ++counts.reducible;
        }
    }

    return counts;
}

Move findMove(Board& board, const char* uci) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i].toUCI() == uci) {
            return moves.moves[i];
        }
    }

    EXPECT_TRUE(false);
    return Move{};
}

// Plays moves given in UCI notation, which must be legal
void playMoves(Board& board, std::initializer_list<const char*> moves) {
    for (const char* uci : moves) {
        const Move move = findMove(board, uci);
        UndoState undo;
        board.makeMove(move, undo);
    }
}

bool sameBoardState(const Board& a, const Board& b) {
    for (int piece = 0; piece < PIECE_COUNT; ++piece) {
        if (a.getPieceBitboard(static_cast<Piece>(piece)) !=
            b.getPieceBitboard(static_cast<Piece>(piece))) {
            return false;
        }
    }

    return a.getOccupied() == b.getOccupied() && a.isWhiteToMove() == b.isWhiteToMove() &&
           a.getHash() == b.getHash() && a.getCastlingRights() == b.getCastlingRights() &&
           a.getEnPassantSquare() == b.getEnPassantSquare() &&
           a.getHalfmoveClock() == b.getHalfmoveClock() &&
           a.getFullmoveCounter() == b.getFullmoveCounter() && a.getHistory() == b.getHistory();
}

Search::Control reductionControl() {
    Search::Control control;
    control.lateMoveReductions = true;
    return control;
}

struct NodeSearch {
    int score;
    Search::Control control;
};

NodeSearch searchNode(Board& board, int depth, int alpha, int beta,
                      TranspositionTable* tt = nullptr, Search::MoveOrdering* ordering = nullptr) {
    NodeSearch result{0, reductionControl()};
    result.score = Search::negamax(board, depth, 0, alpha, beta, nullptr, nullptr, tt, nullptr,
                                   &result.control, ordering);
    return result;
}

// Reduced searches of the node's moves, in a null window every move fails low against
std::uint64_t reducedAtNode(Board& board, TranspositionTable* tt = nullptr,
                            Search::MoveOrdering* ordering = nullptr) {
    return searchNode(board, nodeOnlyDepth, unreachable - 1, unreachable, tt, ordering)
        .control.reducedSearches;
}

std::vector<Search::IterationInfo> iterate(Board& board, int depth, bool lateMoveReductions,
                                           bool nullMovePruning) {
    std::vector<Search::IterationInfo> iterations;
    Search::Limits limits;
    limits.depth = depth;
    limits.lateMoveReductions = lateMoveReductions;
    limits.nullMovePruning = nullMovePruning;
    Search::findBestMove(board, limits, nullptr, [&iterations](const Search::IterationInfo& info) {
        iterations.push_back(info);
    });
    return iterations;
}

bool sameIteration(const Search::IterationInfo& a, const Search::IterationInfo& b) {
    return a.depth == b.depth && a.bestMove.toUCI() == b.bestMove.toUCI() && a.score == b.score &&
           a.nodes == b.nodes && a.failHighs == b.failHighs && a.failLows == b.failLows &&
           a.nullMoveTries == b.nullMoveTries && a.reducedSearches == b.reducedSearches &&
           a.reducedResearches == b.reducedResearches;
}

} // namespace

TEST_CASE("Late move reductions grow slowly with depth and move number") {
    constexpr int minDepth = Search::lateMoveReductionMinDepth;
    constexpr int firstLateMove = Search::lateMoveReductionMinMoves + 1;

    // Shallow nodes and the first moves are never reduced
    for (int moveNumber = 1; moveNumber <= 100; ++moveNumber) {
        EXPECT_EQ(0, Search::lateMoveReduction(minDepth - 1, moveNumber, false));
    }

    for (int depth = 1; depth <= Search::maxSearchDepth; ++depth) {
        EXPECT_EQ(0, Search::lateMoveReduction(depth, firstLateMove - 1, false));
    }

    // One ply as soon as both allow it, except at PV nodes, which reduce one ply less
    EXPECT_EQ(1, Search::lateMoveReduction(minDepth, firstLateMove, false));
    EXPECT_EQ(0, Search::lateMoveReduction(minDepth, firstLateMove, true));

    // Most late moves lose a single ply at the depths reached in play
    EXPECT_EQ(1, Search::lateMoveReduction(6, 10, false));
    EXPECT_EQ(1, Search::lateMoveReduction(8, 8, false));
    EXPECT_EQ(2, Search::lateMoveReduction(8, 10, false));
    EXPECT_EQ(2, Search::lateMoveReduction(10, 20, false));
    EXPECT_EQ(3, Search::lateMoveReduction(10, 30, false));

    bool monotonic = true;
    bool pvOneLess = true;
    bool keepsOnePly = true;
    bool atLeastOne = true;
    int largest = 0;

    for (int depth = 1; depth <= Search::maxSearchDepth; ++depth) {
        for (int moveNumber = 1; moveNumber <= MoveList::capacity; ++moveNumber) {
            const int reduction = Search::lateMoveReduction(depth, moveNumber, false);
            const int pvReduction = Search::lateMoveReduction(depth, moveNumber, true);

            monotonic = monotonic &&
                        reduction >= Search::lateMoveReduction(depth - 1, moveNumber, false) &&
                        reduction >= Search::lateMoveReduction(depth, moveNumber - 1, false);
            pvOneLess = pvOneLess && pvReduction == std::max(reduction - 1, 0);
            keepsOnePly = keepsOnePly && (reduction == 0 || depth - 1 - reduction >= 1);

            if (depth >= minDepth && moveNumber >= firstLateMove) {
                atLeastOne = atLeastOne && reduction >= 1;
            }

            largest = std::max(largest, reduction);
        }
    }

    EXPECT_TRUE(monotonic);
    EXPECT_TRUE(pvOneLess);
    EXPECT_TRUE(keepsOnePly);
    EXPECT_TRUE(atLeastOne);
    EXPECT_TRUE(largest <= 6);

    // Out-of-range arguments are clamped
    EXPECT_EQ(Search::lateMoveReduction(63, 63, false),
              Search::lateMoveReduction(1000, 1000, false));
}

TEST_CASE("Only late quiet moves that do not give check are reduced") {
    Board board;
    board.loadFEN(manyTactics);

    // Every tactical move sorts before the quiet ones, so all quiet moves are late, and several
    // captures and promotions are late too
    const MoveCounts counts = countMoves(board);
    EXPECT_TRUE(counts.tactical > Search::lateMoveReductionMinMoves);
    EXPECT_EQ(1, counts.quietChecks);
    EXPECT_TRUE(counts.reducible > 10);

    EXPECT_EQ(static_cast<std::uint64_t>(counts.reducible), reducedAtNode(board));

    // None at a PV node of this depth, nor at a node one ply shallower
    EXPECT_EQ(std::uint64_t{0},
              searchNode(board, nodeOnlyDepth, -infinity, infinity).control.reducedSearches);
    EXPECT_EQ(
        std::uint64_t{0},
        searchNode(board, nodeOnlyDepth - 1, unreachable - 1, unreachable).control.reducedSearches);

    // Nor without reductions enabled
    Search::Control disabled;
    Search::negamax(board, nodeOnlyDepth, 0, unreachable - 1, unreachable, nullptr, nullptr,
                    nullptr, nullptr, &disabled);
    EXPECT_EQ(std::uint64_t{0}, disabled.reducedSearches);
}

TEST_CASE("Moves are not reduced in check") {
    // Every evasion is a quiet king move, the fourth late enough to be reduced
    Board inCheck;
    inCheck.loadFEN("4r1k1/8/8/8/8/8/PPP5/R3K2R w KQ - 0 1");
    EXPECT_TRUE(MoveGenerator::isKingInCheck(inCheck, true));

    const MoveCounts evasions = countMoves(inCheck);
    EXPECT_EQ(0, evasions.tactical);
    EXPECT_TRUE(evasions.reducible > Search::lateMoveReductionMinMoves);
    EXPECT_EQ(std::uint64_t{0}, reducedAtNode(inCheck));

    // With the rook off the e-file, the same pieces reduce their late moves
    Board free;
    free.loadFEN("3r2k1/8/8/8/8/8/PPP5/R3K2R w KQ - 0 1");
    EXPECT_TRUE(reducedAtNode(free) > 0);
}

TEST_CASE("The table move and killers are searched at full depth") {
    Board board;
    board.loadFEN(manyTactics);
    const std::uint64_t plain = reducedAtNode(board);

    const Move rookMove = findMove(board, "a1a7");
    const Move kingMove = findMove(board, "e1d2");
    const Move pawnMove = findMove(board, "g2g4");
    EXPECT_FALSE(givesCheck(board, rookMove));
    EXPECT_FALSE(givesCheck(board, kingMove));
    EXPECT_FALSE(givesCheck(board, pawnMove));

    // An entry too shallow to answer the search still gives its move, which is searched first
    TranspositionTable tt(1);
    tt.store(board.getHash(), 1, 0, TranspositionTable::Bound::Exact, rookMove, 0);
    EXPECT_EQ(plain - 1, reducedAtNode(board, &tt));

    // Killers follow the tactical moves, so they are late, but not reduced
    Search::MoveOrdering ordering;
    ordering.killers[0] = {kingMove, pawnMove};
    EXPECT_EQ(plain - 2, reducedAtNode(board, nullptr, &ordering));

    // Killers stored for another ply do not count
    Search::MoveOrdering otherPly;
    otherPly.killers[1] = {kingMove, pawnMove};
    EXPECT_EQ(plain, reducedAtNode(board, nullptr, &otherPly));
}

TEST_CASE("A reduced search that beats alpha is searched again at full depth") {
    // With only the node reducing, any score at or above beta comes from a full-depth search, so it
    // is a true lower bound. A score at or below alpha may come from a reduced search and is only
    // as good as the reduction.
    bool allBoundsHold = true;
    bool anyResearch = false;
    bool anyResearchFailedLow = false;
    bool researchesBounded = true;

    // Positions where full-depth re-searches happen at this depth, some of them refuting the
    // reduced search
    for (const char* fen : {kiwipete, "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - 0 1",
                            "2r1k2r/p1q4p/2p2b2/2B1pQp1/B7/8/PP3PPP/3R2K1 b k - 3 29",
                            "rnb1kbnr/pp2pppp/8/8/2pNP3/8/PP3PPP/RNB1KB1R b KQkq - 0 1"}) {
        Board board;
        board.loadFEN(fen);
        const int exact = Search::negamax(board, nodeOnlyDepth);

        for (int offset = -400; offset <= 400; offset += 10) {
            const int beta = exact + offset;
            const NodeSearch reduced = searchNode(board, nodeOnlyDepth, beta - 1, beta);
            const std::uint64_t researches = reduced.control.reducedResearches;

            allBoundsHold = allBoundsHold && (reduced.score < beta || exact >= reduced.score);
            researchesBounded = researchesBounded && researches <= reduced.control.reducedSearches;
            anyResearch = anyResearch || researches > 0;

            // A confirmed re-search would have cut the node off, so one was refuted
            anyResearchFailedLow = anyResearchFailedLow || (researches > 0 && reduced.score < beta);
        }
    }

    EXPECT_TRUE(allBoundsHold);
    EXPECT_TRUE(researchesBounded);
    EXPECT_TRUE(anyResearch);
    EXPECT_TRUE(anyResearchFailedLow);
}

TEST_CASE("Reduced searches are stored at the depth they searched") {
    Board board;
    board.loadFEN(manyTactics);

    TranspositionTable tt(1);
    const NodeSearch node = searchNode(board, nodeOnlyDepth, unreachable - 1, unreachable, &tt);
    EXPECT_TRUE(node.control.reducedSearches > 0);
    EXPECT_EQ(std::uint64_t{0}, node.control.reducedResearches);

    // The late quiet move was searched one ply short of the full depth - 1 and failed low, which
    // its position stores as a lower bound for Black
    playMoves(board, {"a1a7"});
    int score = 0;
    Move move{};
    const int fullDepth = nodeOnlyDepth - 1;
    EXPECT_TRUE(
        tt.probe(board.getHash(), fullDepth - 1, -unreachable, -unreachable + 1, 1, score, move));
    EXPECT_FALSE(
        tt.probe(board.getHash(), fullDepth, -unreachable, -unreachable + 1, 1, score, move));
}

TEST_CASE("Late move reductions keep mate scores and distances") {
    struct MateScenario {
        const char* fen;
        int depth;
        int expected;
    };

    const MateScenario mates[] = {
        {"k7/8/1K6/8/8/8/8/N6R w - - 0 1", 6, mateScore - 1},
        {"7k/8/5K2/8/8/8/8/R7 w - - 0 1", 6, mateScore - 3},
        {"1k1r4/pp1b1R2/6pp/4p3/2B5/4Q3/PPP2B2/2Kq4 w - - 1 2", 6, -(mateScore - 4)},
        {"2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1", 6, mateScore - 3},
    };

    for (const MateScenario& mate : mates) {
        Board board;
        board.loadFEN(mate.fen);

        // With null move pruning as well, as findBestMove searches by default
        const auto reduced = iterate(board, mate.depth, true, true);
        const auto exact = iterate(board, mate.depth, false, false);
        EXPECT_EQ(static_cast<std::size_t>(mate.depth), reduced.size());
        EXPECT_EQ(exact.size(), reduced.size());

        // The mate and its distance appear at the same depth as without reductions
        for (std::size_t i = 0; i < reduced.size() && i < exact.size(); ++i) {
            EXPECT_EQ(exact[i].score, reduced[i].score);
        }

        if (!reduced.empty()) {
            EXPECT_EQ(mate.expected, reduced.back().score);
            EXPECT_TRUE(reduced.back().reducedSearches > 0);
        }
    }
}

TEST_CASE("Late move reductions keep the results of zugzwang endgames") {
    struct Ending {
        const char* fen;
        int depth;
    };

    // Fine 70 needs a zugzwang-based king walk, and the opposition decides the king and pawn
    // endings: reductions only save nodes in them
    const Ending endings[] = {
        {"8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1", 14},
        {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", 11},
        {"8/8/8/3k4/8/3K4/3P4/8 w - - 0 1", 11},
    };

    for (const Ending& ending : endings) {
        Board board;
        board.loadFEN(ending.fen);

        const auto reduced = iterate(board, ending.depth, true, true);
        const auto exact = iterate(board, ending.depth, false, false);
        EXPECT_EQ(exact.size(), reduced.size());

        for (std::size_t i = 0; i < reduced.size() && i < exact.size(); ++i) {
            EXPECT_EQ(exact[i].score, reduced[i].score);
            EXPECT_EQ(exact[i].bestMove.toUCI(), reduced[i].bestMove.toUCI());
        }

        if (!reduced.empty()) {
            EXPECT_TRUE(reduced.back().reducedSearches > 0);
            EXPECT_TRUE(reduced.back().nodes < exact.back().nodes);
        }
    }

    // Rf1, a quiet move that wins only through zugzwang, is found one ply later than the exact
    // search finds it at depth 8, and kept
    Board board;
    board.loadFEN(rookZugzwang);
    const auto reduced = iterate(board, 10, true, true);
    EXPECT_EQ(std::size_t{10}, reduced.size());

    for (std::size_t i = 8; i < reduced.size(); ++i) {
        EXPECT_EQ(std::string("e1f1"), reduced[i].bestMove.toUCI());
    }
}

TEST_CASE("Late move reductions keep repetition and fifty-move draws") {
    // Losing, White draws by repeating with g1h1
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/q7/7K w - - 0 1");
    playMoves(board,
              {"h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8"});

    for (const Search::IterationInfo& info : iterate(board, 7, true, true)) {
        EXPECT_EQ(0, info.score);
        EXPECT_EQ(std::string("g1h1"), info.bestMove.toUCI());
    }

    // From the fifth ply every position is a fifty-move draw. Reduced searches stop short of it and
    // see the extra queen, so they beat alpha and the full-depth re-searches find the draw.
    Board cliff;
    cliff.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 95 80");
    std::uint64_t researches = 0;

    for (int depth = 5; depth <= 7; ++depth) {
        const NodeSearch result = searchNode(cliff, depth, -infinity, infinity);
        EXPECT_EQ(0, result.score);
        EXPECT_TRUE(result.control.reducedSearches > 0);
        researches += result.control.reducedResearches;
    }

    EXPECT_TRUE(researches > 0);

    // Resetting the clock is still found
    Board reset;
    reset.loadFEN("7k/8/8/8/8/8/1Q4p1/K7 w - - 99 80");
    const auto iterations = iterate(reset, 6, true, true);

    if (!iterations.empty()) {
        EXPECT_EQ(std::string("b2g2"), iterations.back().bestMove.toUCI());
        EXPECT_TRUE(iterations.back().score > 500);
    }
}

TEST_CASE("Interrupting reduced searches and their re-searches restores the board") {
    // A blocked ending small enough to sweep, whose search re-searches a reduced move
    Board board;
    board.loadFEN("8/p3rk2/P1p1p1b1/2PpPpp1/1p6/1P3PP1/4PKB1/2R5 b - - 0 48");
    const Board original = board;

    constexpr int depth = 5;
    const int eval = Evaluation::evaluate(board);
    const int beta = (board.isWhiteToMove() ? eval : -eval) - 50;

    // Every node reads the stop condition once, so an uninterrupted search counts the nodes
    TranspositionTable sizingTable(1);
    Search::Control sizing = reductionControl();
    const int complete = Search::negamax(board, depth, 0, beta - 1, beta, nullptr, nullptr,
                                         &sizingTable, nullptr, &sizing);
    const std::uint64_t calls = sizing.checkCounter;

    // The sweep reaches reduced searches and full-depth re-searches
    EXPECT_TRUE(sizing.reducedSearches > sizing.reducedResearches);
    EXPECT_TRUE(sizing.reducedResearches > 0);
    EXPECT_TRUE(calls < Search::stopCheckInterval);

    const std::atomic<bool> stopRequested = true;
    bool allStopped = true;
    bool allRestored = true;

    for (std::uint64_t call = 1; call <= calls; ++call) {
        TranspositionTable tt(1);
        Search::Control control = reductionControl();
        control.stopRequested = &stopRequested;
        control.checkCounter = Search::stopCheckInterval - call;

        Search::negamax(board, depth, 0, beta - 1, beta, nullptr, nullptr, &tt, nullptr, &control);

        allStopped = allStopped && control.stopped;
        allRestored = allRestored && sameBoardState(original, board);
    }

    EXPECT_TRUE(allStopped);
    EXPECT_TRUE(allRestored);

    // A stop condition first read after the search ends has no effect
    TranspositionTable tt(1);
    Search::Control late = reductionControl();
    late.stopRequested = &stopRequested;
    late.checkCounter = Search::stopCheckInterval - (calls + 1);
    const int score =
        Search::negamax(board, depth, 0, beta - 1, beta, nullptr, nullptr, &tt, nullptr, &late);

    EXPECT_FALSE(late.stopped);
    EXPECT_EQ(complete, score);
    EXPECT_EQ(sizing.reducedSearches, late.reducedSearches);
    EXPECT_EQ(sizing.reducedResearches, late.reducedResearches);
}

TEST_CASE("A timed search with reductions reports only completed iterations") {
    Board board;
    board.loadFEN(kiwipete);
    const Board original = board;

    // Aspiration windows start at depth 4, so interruptions also land in their re-searches
    constexpr int depth = 6;
    const auto start = Search::Clock::now();
    const auto reference = iterate(board, depth, true, true);
    const auto referenceTime = Search::Clock::now() - start;

    // Deterministic, so a repeated search reproduces every iteration
    const auto repeated = iterate(board, depth, true, true);
    EXPECT_EQ(reference.size(), repeated.size());

    for (std::size_t i = 0; i < reference.size() && i < repeated.size(); ++i) {
        EXPECT_TRUE(sameIteration(reference[i], repeated[i]));
    }

    bool allPrefixes = true;
    bool allRestored = true;

    for (int step = 1; step <= 8; ++step) {
        Search::Limits limits;
        limits.depth = depth;
        limits.hardDeadline = Search::Clock::now() + referenceTime * step / 8;

        std::vector<Search::IterationInfo> iterations;
        const auto bestMove = Search::findBestMove(
            board, limits, nullptr,
            [&iterations](const Search::IterationInfo& info) { iterations.push_back(info); });

        bool prefix = iterations.size() <= reference.size() && bestMove.has_value();

        for (std::size_t i = 0; prefix && i < iterations.size(); ++i) {
            prefix = sameIteration(reference[i], iterations[i]);
        }

        if (!iterations.empty()) {
            prefix = prefix && bestMove->toUCI() == iterations.back().bestMove.toUCI();
        }

        allPrefixes = allPrefixes && prefix;
        allRestored = allRestored && sameBoardState(original, board);
    }

    EXPECT_TRUE(allPrefixes);
    EXPECT_TRUE(allRestored);
}

TEST_CASE("Iterative deepening reports reduction counts and can disable reductions") {
    Board board;
    board.loadFEN(kiwipete);

    const auto reduced = iterate(board, 5, true, false);
    const auto combined = iterate(board, 5, true, true);
    const auto exact = iterate(board, 5, false, false);

    EXPECT_EQ(std::size_t{5}, reduced.size());
    EXPECT_EQ(std::size_t{5}, combined.size());
    EXPECT_EQ(std::size_t{5}, exact.size());

    if (reduced.size() == 5 && combined.size() == 5 && exact.size() == 5) {
        EXPECT_TRUE(reduced.back().reducedSearches > 0);
        EXPECT_TRUE(reduced.back().reducedResearches <= reduced.back().reducedSearches);
        EXPECT_TRUE(reduced.back().nodes < exact.back().nodes);
        EXPECT_EQ(std::uint64_t{0}, reduced.back().nullMoveTries);

        // Counts accumulate over the iterations of one search
        EXPECT_TRUE(reduced[3].reducedSearches <= reduced[4].reducedSearches);

        // Both selective searches work together
        EXPECT_TRUE(combined.back().reducedSearches > 0);
        EXPECT_TRUE(combined.back().nullMoveCutoffs > 0);

        // Disabled reductions reproduce the full-window score
        EXPECT_EQ(std::uint64_t{0}, exact.back().reducedSearches);
        EXPECT_EQ(std::uint64_t{0}, exact.back().reducedResearches);
        EXPECT_EQ(Search::negamax(board, 5), exact.back().score);
    }
}
