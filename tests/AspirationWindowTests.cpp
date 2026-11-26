#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int infinity = 1000000;
constexpr int mateScore = 100000;
constexpr int mateThreshold = mateScore - 1000;

struct Scenario {
    const char* fen;
    int depth;
};

const Scenario scenarios[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1", 4},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
    {"4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1", 4},
};

// White mates in 2: three plies from the root
const char* mateInTwo = "7k/8/5K2/8/8/8/8/R7 w - - 0 1";

// Kxd1 is the only legal move, after which Black mates in 2: four plies from the root
const char* matedInTwo = "1k1r4/pp1b1R2/6pp/4p3/2B5/4Q3/PPP2B2/2Kq4 w - - 1 2";

// d7c8q scores 438 at even depths and 493 or 503 at odd ones, so from aspirationMinDepth on, the
// previous score always lies more than aspirationWindow from the next one
const char* oscillating = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";

// Winning for White, but from the fifth ply on every position is a fifty-move draw
const char* fiftyMoveCliff = "7k/8/8/8/8/8/1Q6/K7 w - - 95 80";

// The most fails one root search can have. Each fail doubles the margin before moving a bound by
// it; once the margin reaches twice the mate score, a fail opens its side fully whatever the
// score, so at most one fail per side follows the fails with a smaller margin.
int maxFails() {
    int narrowFails = 0;

    for (long long delta = 2LL * Search::aspirationWindow; delta < 2LL * mateScore; delta *= 2) {
        ++narrowFails;
    }

    return narrowFails + 2;
}

MoveList legalMoves(Board& board) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);
    return moves;
}

// Every legal move appears in moves exactly once
bool sameMoves(Board& board, const MoveList& moves) {
    const MoveList legal = legalMoves(board);

    if (legal.count != moves.count) {
        return false;
    }

    for (int i = 0; i < legal.count; ++i) {
        int matches = 0;

        for (int j = 0; j < moves.count; ++j) {
            matches += moves.moves[j].toUCI() == legal.moves[i].toUCI();
        }

        if (matches != 1) {
            return false;
        }
    }

    return true;
}

// The exact score at this depth, from a full-window search without a table
int exactScore(Board& board, int depth) { return Search::negamax(board, depth); }

// The exact score of a root move, searched as the root search does
int exactMoveScore(Board& board, const Move& move, int depth) {
    UndoState undo;
    board.makeMove(move, undo);
    const int score = -Search::negamax(board, depth - 1, 1);
    board.unmakeMove(move, undo);
    return score;
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

bool sameIteration(const Search::IterationInfo& a, const Search::IterationInfo& b) {
    return a.depth == b.depth && a.bestMove.toUCI() == b.bestMove.toUCI() && a.score == b.score &&
           a.nodes == b.nodes && a.aspiration == b.aspiration && a.failHighs == b.failHighs &&
           a.failLows == b.failLows;
}

std::vector<Search::IterationInfo> iterate(Board& board, int depth) {
    std::vector<Search::IterationInfo> iterations;
    Search::findBestMove(board, depth, [&iterations](const Search::IterationInfo& info) {
        iterations.push_back(info);
    });
    return iterations;
}

// Plays moves given in UCI notation, which must be legal
void playMoves(Board& board, std::initializer_list<const char*> moves) {
    for (const char* uci : moves) {
        const MoveList legal = legalMoves(board);
        bool found = false;

        for (int i = 0; i < legal.count && !found; ++i) {
            if (legal.moves[i].toUCI() == uci) {
                UndoState undo;
                board.makeMove(legal.moves[i], undo);
                found = true;
            }
        }

        EXPECT_TRUE(found);
    }
}

} // namespace

TEST_CASE("A window around the exact score is searched once") {
    for (const Scenario& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);
        const Board original = board;

        const int exact = exactScore(board, scenario.depth);

        for (int offset : {-(Search::aspirationWindow - 1), 0, Search::aspirationWindow - 1}) {
            MoveList moves = legalMoves(board);
            const auto result = Search::searchRoot(board, moves, scenario.depth, exact + offset);

            EXPECT_TRUE(result.has_value());

            if (!result.has_value()) {
                continue;
            }

            EXPECT_TRUE(result->aspiration);
            EXPECT_EQ(0, result->failHighs);
            EXPECT_EQ(0, result->failLows);
            EXPECT_EQ(exact, result->score);
            EXPECT_EQ(exact, exactMoveScore(board, result->bestMove, scenario.depth));
            EXPECT_EQ(result->bestMove.toUCI(), moves.moves[0].toUCI());
        }

        EXPECT_TRUE(sameBoardState(original, board));
    }
}

TEST_CASE("Scores outside the window fail high or low until the window holds them") {
    int mostFails = 0;

    for (const Scenario& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);
        const Board original = board;

        const int depth = scenario.depth - 1;
        const int exact = exactScore(board, depth);

        // A guess aspirationWindow away puts the exact score on a bound, which counts as a fail
        for (int offset : {Search::aspirationWindow, 300, 5000}) {
            for (int sign : {-1, 1}) {
                const int guess = exact + sign * offset;
                MoveList moves = legalMoves(board);
                const auto result = Search::searchRoot(board, moves, depth, guess);

                EXPECT_TRUE(result.has_value());

                if (!result.has_value()) {
                    continue;
                }

                // Without a table every fail-soft bound holds, so a guess below the score only
                // fails high and one above it only fails low
                const int fails = sign < 0 ? result->failHighs : result->failLows;
                EXPECT_TRUE(result->aspiration);
                EXPECT_TRUE(fails >= 1);
                EXPECT_EQ(0, sign < 0 ? result->failLows : result->failHighs);
                EXPECT_EQ(exact, result->score);
                EXPECT_EQ(exact, exactMoveScore(board, result->bestMove, depth));
                EXPECT_TRUE(sameMoves(board, moves));

                mostFails = std::max(mostFails, fails);
            }
        }

        EXPECT_TRUE(sameBoardState(original, board));
    }

    // Distant guesses widen the window several times in a row
    EXPECT_TRUE(mostFails >= 3);
    EXPECT_TRUE(mostFails <= maxFails());
}

TEST_CASE("Aspiration search with a table matches the full-window score") {
    for (const Scenario& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        const int exact = exactScore(board, scenario.depth);

        // Bounds left by failed windows do not change the score, even when the table and move
        // ordering are shared across guesses
        TranspositionTable sharedTable(1);
        Search::MoveOrdering sharedOrdering;
        bool allExact = true;

        for (int offset : {-1000, -25, 0, 100}) {
            TranspositionTable freshTable(1);
            Search::MoveOrdering freshOrdering;

            MoveList moves = legalMoves(board);
            const auto fresh =
                Search::searchRoot(board, moves, scenario.depth, exact + offset, nullptr, nullptr,
                                   &freshTable, nullptr, &freshOrdering);

            const auto shared =
                Search::searchRoot(board, moves, scenario.depth, exact + offset, nullptr, nullptr,
                                   &sharedTable, nullptr, &sharedOrdering);

            allExact = allExact && fresh.has_value() && shared.has_value() &&
                       fresh->score == exact && shared->score == exact;
        }

        EXPECT_TRUE(allExact);
    }
}

TEST_CASE("Mate scores are searched with that side of the window fully open") {
    // An ordinary guess fails high into a mate found at this depth
    Board mating;
    mating.loadFEN(mateInTwo);
    MoveList moves = legalMoves(mating);
    auto result = Search::searchRoot(mating, moves, 3, 0);

    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_TRUE(result->aspiration);
        EXPECT_TRUE(result->failHighs >= 1);
        EXPECT_EQ(0, result->failLows);
        EXPECT_EQ(mateScore - 3, result->score);
        EXPECT_EQ(mateScore - 3, exactMoveScore(mating, result->bestMove, 3));
    }

    // An ordinary guess fails low into being mated
    Board mated;
    mated.loadFEN(matedInTwo);
    moves = legalMoves(mated);
    result = Search::searchRoot(mated, moves, 4, 0);

    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_TRUE(result->aspiration);
        EXPECT_EQ(0, result->failHighs);
        EXPECT_TRUE(result->failLows >= 1);
        EXPECT_EQ(-(mateScore - 4), result->score);
    }

    // A mate guess starts from the full window, whether or not the mate is still found
    Board ordinary;
    ordinary.loadFEN(scenarios[2].fen);
    const int ordinaryExact = exactScore(ordinary, 3);

    for (int guess : {mateScore - 3, -(mateScore - 4), mateThreshold, -mateThreshold}) {
        moves = legalMoves(mating);
        result = Search::searchRoot(mating, moves, 3, guess);
        EXPECT_TRUE(result.has_value() && !result->aspiration && result->score == mateScore - 3);

        moves = legalMoves(ordinary);
        result = Search::searchRoot(ordinary, moves, 3, guess);
        EXPECT_TRUE(result.has_value() && !result->aspiration && result->score == ordinaryExact);
    }

    // Narrow bounds stay strictly between the mate scores; guesses at the edge of that range
    // still converge
    const int edge = mateThreshold - Search::aspirationWindow;

    for (int guess : {edge - 1, edge, -(edge - 1), -edge}) {
        moves = legalMoves(ordinary);
        result = Search::searchRoot(ordinary, moves, 3, guess);

        EXPECT_TRUE(result.has_value());

        if (result.has_value()) {
            EXPECT_EQ(std::abs(guess) < edge, result->aspiration);
            EXPECT_EQ(ordinaryExact, result->score);
        }
    }
}

TEST_CASE("Extreme guesses and empty move lists are handled without overflow") {
    Board board;
    board.loadFEN(scenarios[4].fen);
    const int exact = exactScore(board, 3);

    // Guesses at the limits of int get the full window rather than overflowing its bounds
    for (int guess :
         {std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), -infinity, infinity}) {
        MoveList moves = legalMoves(board);
        const auto result = Search::searchRoot(board, moves, 3, guess);

        EXPECT_TRUE(result.has_value() && !result->aspiration && result->score == exact);
    }

    MoveList empty;
    EXPECT_FALSE(Search::searchRoot(board, empty, 3, 0).has_value());
    EXPECT_FALSE(Search::searchRoot(board, empty, 3, std::nullopt).has_value());
}

TEST_CASE("Iterative deepening moves between ordinary and mate scores") {
    Board board;
    board.loadFEN(matedInTwo);

    const auto iterations = iterate(board, 6);
    EXPECT_EQ(std::size_t{6}, iterations.size());

    for (const Search::IterationInfo& info : iterations) {
        EXPECT_EQ(exactScore(board, info.depth), info.score);

        // The first aspiration iteration fails low into the mate; the next ones start from a
        // mate score, which gets the full window
        EXPECT_EQ(info.depth == Search::aspirationMinDepth, info.aspiration);
    }

    if (iterations.size() == 6) {
        EXPECT_TRUE(iterations[3].failLows >= 1);
        EXPECT_EQ(-(mateScore - 4), iterations[5].score);
    }
}

TEST_CASE("Iterative deepening reports each depth once with full-window scores") {
    std::vector<Scenario> all(std::begin(scenarios), std::end(scenarios));
    all.push_back({oscillating, 5});

    int aspirationIterations = 0;
    int fails = 0;

    for (const Scenario& scenario : all) {
        Board board;
        board.loadFEN(scenario.fen);
        const Board original = board;

        const auto iterations = iterate(board, scenario.depth);
        EXPECT_EQ(static_cast<std::size_t>(scenario.depth), iterations.size());

        // The same iterations searched with the full window only, sharing a table and move
        // ordering as findBestMove does
        MoveList moves = legalMoves(board);
        TranspositionTable tt;
        Search::MoveOrdering ordering;

        for (std::size_t i = 0; i < iterations.size(); ++i) {
            const Search::IterationInfo& info = iterations[i];
            const int depth = static_cast<int>(i) + 1;

            const auto fullWindow = Search::searchRoot(board, moves, depth, std::nullopt, nullptr,
                                                       nullptr, &tt, nullptr, &ordering);

            EXPECT_EQ(depth, info.depth);
            EXPECT_EQ(depth >= Search::aspirationMinDepth, info.aspiration);
            EXPECT_EQ(exactScore(board, depth), info.score);
            EXPECT_TRUE(fullWindow.has_value() && fullWindow->score == info.score);
            EXPECT_EQ(info.score, exactMoveScore(board, info.bestMove, depth));

            aspirationIterations += info.aspiration;
            fails += info.failHighs + info.failLows;
        }

        EXPECT_TRUE(sameBoardState(original, board));
    }

    EXPECT_TRUE(aspirationIterations > 0);
    EXPECT_TRUE(fails > 0);
}

TEST_CASE("Every aspiration iteration of an oscillating score re-searches") {
    Board board;
    board.loadFEN(oscillating);

    const auto iterations = iterate(board, 6);
    EXPECT_EQ(std::size_t{6}, iterations.size());

    for (const Search::IterationInfo& info : iterations) {
        if (info.depth < Search::aspirationMinDepth) {
            continue;
        }

        // Rising to an odd depth fails high, falling to an even one fails low
        EXPECT_TRUE(info.aspiration);
        EXPECT_EQ(info.depth % 2 == 1 ? 1 : 0, info.failHighs);
        EXPECT_EQ(info.depth % 2 == 0 ? 1 : 0, info.failLows);
        EXPECT_EQ(std::string("d7c8q"), info.bestMove.toUCI());
    }
}

TEST_CASE("Interrupting an aspiration re-search keeps the board, moves and table usable") {
    Board board;
    board.loadFEN(scenarios[4].fen);
    const Board original = board;

    constexpr int depth = 3;
    const int exact = exactScore(board, depth);

    // Guesses this far off fail at least twice, so the sweep interrupts repeated re-searches
    for (int guess : {exact - 400, exact + 400}) {
        // Every node reads the stop condition once, so an uninterrupted search counts the nodes
        TranspositionTable sizingTable(1);
        Search::MoveOrdering sizingOrdering;
        Search::Control sizingControl;
        MoveList moves = legalMoves(board);
        const auto complete = Search::searchRoot(board, moves, depth, guess, nullptr, nullptr,
                                                 &sizingTable, &sizingControl, &sizingOrdering);
        const std::uint64_t calls = sizingControl.checkCounter;

        // The sweep below reaches every node, so it covers each re-search
        EXPECT_TRUE(complete.has_value());
        EXPECT_TRUE(calls < Search::stopCheckInterval);
        EXPECT_TRUE(complete.has_value() && complete->failHighs + complete->failLows >= 2);

        const std::atomic<bool> stopRequested = true;
        bool allStopped = true;
        bool allRestored = true;
        bool allExactAfterwards = true;

        for (std::uint64_t call = 1; call <= calls; ++call) {
            TranspositionTable tt(1);
            Search::MoveOrdering ordering;

            Search::Control control;
            control.stopRequested = &stopRequested;
            control.checkCounter = Search::stopCheckInterval - call;

            moves = legalMoves(board);
            const auto result = Search::searchRoot(board, moves, depth, guess, nullptr, nullptr,
                                                   &tt, &control, &ordering);

            allStopped = allStopped && !result.has_value() && control.stopped;
            allRestored = allRestored && sameBoardState(original, board) && sameMoves(board, moves);

            // What the interrupted search left behind does not change a complete search
            const auto after = Search::searchRoot(board, moves, depth, guess, nullptr, nullptr, &tt,
                                                  nullptr, &ordering);
            allExactAfterwards = allExactAfterwards && after.has_value() && after->score == exact;
        }

        EXPECT_TRUE(allStopped);
        EXPECT_TRUE(allRestored);
        EXPECT_TRUE(allExactAfterwards);

        // A stop condition first read after the search ends has no effect
        TranspositionTable tt(1);
        Search::MoveOrdering ordering;
        Search::Control control;
        control.stopRequested = &stopRequested;
        control.checkCounter = Search::stopCheckInterval - (calls + 1);
        moves = legalMoves(board);
        const auto late = Search::searchRoot(board, moves, depth, guess, nullptr, nullptr, &tt,
                                             &control, &ordering);

        EXPECT_FALSE(control.stopped);
        EXPECT_TRUE(late.has_value() && complete.has_value() && late->score == complete->score &&
                    late->failHighs == complete->failHighs && late->failLows == complete->failLows);
    }
}

TEST_CASE("A timed search interrupted in aspiration iterations reports only completed ones") {
    Board board;
    board.loadFEN(oscillating);
    const Board original = board;

    // Every aspiration iteration here re-searches, so some interruptions fall in re-searches.
    // Timing cannot show which; the searchRoot sweep above covers them node by node.
    constexpr int depth = 5;
    const auto start = Search::Clock::now();
    const auto reference = iterate(board, depth);
    const auto referenceTime = Search::Clock::now() - start;

    bool allPrefixes = true;
    bool allRestored = true;
    bool anyInAspiration = false;

    for (int step = 1; step <= 12; ++step) {
        Search::Limits limits;
        limits.depth = depth;
        limits.hardDeadline = Search::Clock::now() + referenceTime * step / 12;

        std::vector<Search::IterationInfo> iterations;
        const auto bestMove = Search::findBestMove(
            board, limits, nullptr,
            [&iterations](const Search::IterationInfo& info) { iterations.push_back(info); });

        // Reported iterations match the uninterrupted search, and the last one gives the move
        bool prefix = iterations.size() <= reference.size();

        for (std::size_t i = 0; prefix && i < iterations.size(); ++i) {
            prefix = sameIteration(reference[i], iterations[i]);
        }

        if (!iterations.empty()) {
            prefix = prefix && bestMove.has_value() &&
                     bestMove->toUCI() == iterations.back().bestMove.toUCI();
        }

        allPrefixes = allPrefixes && prefix;
        allRestored = allRestored && sameBoardState(original, board);

        const int interruptedDepth = static_cast<int>(iterations.size()) + 1;
        anyInAspiration = anyInAspiration || (interruptedDepth >= Search::aspirationMinDepth &&
                                              interruptedDepth <= depth);
    }

    EXPECT_TRUE(allPrefixes);
    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(anyInAspiration);
}

TEST_CASE("A fifty-move draw beyond the previous depth fails low to the draw score") {
    Board board;
    board.loadFEN(fiftyMoveCliff);

    const auto iterations = iterate(board, 6);
    EXPECT_EQ(std::size_t{6}, iterations.size());

    for (const Search::IterationInfo& info : iterations) {
        EXPECT_EQ(exactScore(board, info.depth), info.score);
    }

    if (iterations.size() == 6) {
        // Clearly winning up to depth 4, then every line reaches the fifty-move limit
        EXPECT_TRUE(iterations[3].score > 500);
        EXPECT_EQ(0, iterations[4].score);
        EXPECT_TRUE(iterations[4].aspiration);
        EXPECT_TRUE(iterations[4].failLows >= 1);
        EXPECT_EQ(0, iterations[4].failHighs);
    }
}

TEST_CASE("Aspiration windows keep threefold repetition draws") {
    // Black's king and queen against White's king, after both kings shuffled twice. Repeating
    // with g1h1 draws by threefold repetition; without the history the position is lost.
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/q7/7K w - - 0 1");
    playMoves(board,
              {"h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8"});
    const Board original = board;

    const auto iterations = iterate(board, 5);
    EXPECT_EQ(std::size_t{5}, iterations.size());

    for (const Search::IterationInfo& info : iterations) {
        EXPECT_EQ(0, info.score);
        EXPECT_EQ(std::string("g1h1"), info.bestMove.toUCI());
    }

    // Guesses far from the draw score converge to it, with or without a table
    for (int guess : {-900, -100, 100, 900}) {
        TranspositionTable tt(1);
        MoveList moves = legalMoves(board);
        const auto result = Search::searchRoot(board, moves, 5, guess, nullptr, nullptr, &tt);

        EXPECT_TRUE(result.has_value() && result->score == 0 && result->bestMove.toUCI() == "g1h1");
    }

    EXPECT_TRUE(sameBoardState(original, board));
}
