#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

// Many captures for both sides, so quiescence alone visits a large tree
const char* tacticalPosition =
    "r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1";

bool sameBoardState(const Board& a, const Board& b) {
    for (int piece = 0; piece < PIECE_COUNT; ++piece) {
        if (a.getPieceBitboard(static_cast<Piece>(piece)) !=
            b.getPieceBitboard(static_cast<Piece>(piece))) {
            return false;
        }
    }

    return a.getWhitePieces() == b.getWhitePieces() && a.getBlackPieces() == b.getBlackPieces() &&
           a.getOccupied() == b.getOccupied() && a.isWhiteToMove() == b.isWhiteToMove() &&
           a.getHalfmoveClock() == b.getHalfmoveClock() &&
           a.getFullmoveCounter() == b.getFullmoveCounter() &&
           a.getCastlingRights() == b.getCastlingRights() &&
           a.getEnPassantSquare() == b.getEnPassantSquare() && a.getHash() == b.getHash() &&
           a.getHistory() == b.getHistory();
}

bool isLegalMove(Board& board, const Move& move) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i].toUCI() == move.toUCI()) {
            return true;
        }
    }

    return false;
}

// Primes the counter so that the stop condition is first read at the given search call
Search::Control stopAtCall(const std::atomic<bool>& stopRequested, std::uint64_t call) {
    Search::Control control;
    control.stopRequested = &stopRequested;
    control.checkCounter = Search::stopCheckInterval - call;
    return control;
}

// Every early interruption point, then a spread of later ones to keep the sweeps fast
std::uint64_t nextCall(std::uint64_t call) { return call < 128 ? call + 1 : call + 37; }

struct SearchResult {
    std::optional<Move> bestMove;
    std::vector<Search::IterationInfo> iterations;
};

SearchResult runSearch(Board& board, const Search::Limits& limits,
                       const std::atomic<bool>* stopRequested) {
    SearchResult result;
    result.bestMove =
        Search::findBestMove(board, limits, stopRequested,
                             [&result](const auto& info) { result.iterations.push_back(info); });
    return result;
}

} // namespace

TEST_CASE("Interrupting quiescence at any node restores the board") {
    Board board;
    board.loadFEN(tacticalPosition);
    const Board original = board;

    std::uint64_t fullQNodes = 0;
    const int fullScore = Search::negamax(board, 0, 0, -1000000, 1000000, nullptr, &fullQNodes);

    // Depth 0 goes straight to quiescence, so every interruption point lies inside it
    EXPECT_TRUE(fullQNodes > 100);

    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool allStopped = true;

    for (std::uint64_t call = 1; call <= fullQNodes; call = nextCall(call)) {
        Search::Control control = stopAtCall(stopRequested, call);
        std::uint64_t qNodes = 0;

        Search::negamax(board, 0, 0, -1000000, 1000000, nullptr, &qNodes, nullptr, nullptr,
                        &control);

        allRestored = allRestored && sameBoardState(original, board);
        allStopped = allStopped && control.stopped && qNodes < call;
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(allStopped);

    // A stop condition first read after the search ends has no effect
    Search::Control lateControl = stopAtCall(stopRequested, fullQNodes + 1);
    const int lateScore = Search::negamax(board, 0, 0, -1000000, 1000000, nullptr, nullptr, nullptr,
                                          nullptr, &lateControl);

    EXPECT_FALSE(lateControl.stopped);
    EXPECT_EQ(fullScore, lateScore);
}

TEST_CASE("Interrupting negamax at any node restores the board and skips the root store") {
    Board board;
    board.loadFEN(kiwipete);
    const Board original = board;

    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool allStopped = true;
    bool rootNeverStored = true;

    // The first calls are negamax nodes along the leftmost line, the rest mix in quiescence
    for (std::uint64_t call = 1; call <= Search::stopCheckInterval; call = nextCall(call)) {
        TranspositionTable tt(1);
        Search::Control control = stopAtCall(stopRequested, call);

        Search::negamax(board, 3, 0, -1000000, 1000000, nullptr, nullptr, &tt, nullptr, &control);

        allRestored = allRestored && sameBoardState(original, board);
        allStopped = allStopped && control.stopped;

        // Hits count every full key match, whatever the stored bound
        const std::uint64_t hitsBefore = tt.getHits();
        int score = 0;
        Move move{};
        tt.probe(original.getHash(), 0, -1000000, 1000000, 0, score, move);
        rootNeverStored = rootNeverStored && tt.getHits() == hitsBefore;
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(allStopped);
    EXPECT_TRUE(rootNeverStored);
}

TEST_CASE("A search without a stop condition is unchanged by the control") {
    Board board;
    board.loadFEN(kiwipete);

    std::uint64_t nodes = 0;
    std::uint64_t qNodes = 0;
    const int score = Search::negamax(board, 3, 0, -1000000, 1000000, &nodes, &qNodes);

    Search::Control control;
    std::uint64_t controlledNodes = 0;
    std::uint64_t controlledQNodes = 0;
    const int controlledScore = Search::negamax(board, 3, 0, -1000000, 1000000, &controlledNodes,
                                                &controlledQNodes, nullptr, nullptr, &control);

    EXPECT_FALSE(control.stopped);
    EXPECT_EQ(score, controlledScore);
    EXPECT_EQ(nodes, controlledNodes);
    EXPECT_EQ(qNodes, controlledQNodes);
}

TEST_CASE("Stopped before searching returns a legal fallback move") {
    Board board;
    board.loadFEN(kiwipete);
    const Board original = board;

    const std::atomic<bool> stopRequested = true;
    Search::Limits limits;
    SearchResult result = runSearch(board, limits, &stopRequested);

    EXPECT_TRUE(result.bestMove.has_value());
    EXPECT_TRUE(result.iterations.empty());
    EXPECT_TRUE(sameBoardState(original, board));

    if (result.bestMove.has_value()) {
        EXPECT_TRUE(isLegalMove(board, *result.bestMove));
    }
}

TEST_CASE("An expired hard deadline returns a legal fallback move") {
    Board board;
    board.setStartingPosition();
    const Board original = board;

    Search::Limits limits;
    limits.hardDeadline = Search::Clock::now() - std::chrono::seconds(1);
    SearchResult result = runSearch(board, limits, nullptr);

    EXPECT_TRUE(result.bestMove.has_value());
    EXPECT_TRUE(result.iterations.empty());
    EXPECT_TRUE(sameBoardState(original, board));

    if (result.bestMove.has_value()) {
        EXPECT_TRUE(isLegalMove(board, *result.bestMove));
    }
}

TEST_CASE("An expired soft deadline still completes the first iteration") {
    Board board;
    board.loadFEN("4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1");

    Search::Limits limits;
    limits.softDeadline = Search::Clock::now() - std::chrono::seconds(1);
    SearchResult result = runSearch(board, limits, nullptr);

    EXPECT_EQ(std::size_t{1}, result.iterations.size());

    if (result.bestMove.has_value()) {
        EXPECT_EQ(std::string("e2e4"), result.bestMove->toUCI());
    }
}

TEST_CASE("A stop between iterations keeps the last completed iteration") {
    Board board;
    board.loadFEN(kiwipete);

    std::atomic<bool> stopRequested = false;
    std::vector<Search::IterationInfo> iterations;

    Search::Limits limits;
    limits.depth = 6;

    auto bestMove =
        Search::findBestMove(board, limits, &stopRequested, [&](const Search::IterationInfo& info) {
            iterations.push_back(info);

            if (info.depth == 2) {
                stopRequested = true;
            }
        });

    EXPECT_EQ(std::size_t{2}, iterations.size());
    EXPECT_TRUE(bestMove.has_value());

    if (bestMove.has_value() && !iterations.empty()) {
        EXPECT_EQ(iterations.back().bestMove.toUCI(), bestMove->toUCI());
    }
}

TEST_CASE("Timed searches only return completed iterations or the fallback") {
    Board board;
    board.loadFEN(kiwipete);
    const Board original = board;

    Board fallbackBoard = board;
    const std::atomic<bool> stopRequested = true;
    const std::string fallback = runSearch(fallbackBoard, {}, &stopRequested).bestMove->toUCI();

    bool allConsistent = true;
    bool allRestored = true;

    for (int timeMs = 0; timeMs <= 40; timeMs += 2) {
        Search::Limits limits;
        limits.hardDeadline = Search::Clock::now() + std::chrono::milliseconds(timeMs);
        SearchResult result = runSearch(board, limits, nullptr);

        const std::string expected =
            result.iterations.empty() ? fallback : result.iterations.back().bestMove.toUCI();

        allConsistent =
            allConsistent && result.bestMove.has_value() && result.bestMove->toUCI() == expected;
        allRestored = allRestored && sameBoardState(original, board);
    }

    EXPECT_TRUE(allConsistent);
    EXPECT_TRUE(allRestored);
}

TEST_CASE("Positions without legal moves return no move even when stopped") {
    const std::atomic<bool> stopRequested = true;

    Board checkmate;
    checkmate.loadFEN("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    EXPECT_FALSE(runSearch(checkmate, {}, &stopRequested).bestMove.has_value());
    EXPECT_FALSE(runSearch(checkmate, {}, nullptr).bestMove.has_value());

    Board stalemate;
    stalemate.loadFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    EXPECT_FALSE(runSearch(stalemate, {}, &stopRequested).bestMove.has_value());
}

TEST_CASE("Depth limits behave like fixed-depth searches") {
    Board board;
    board.loadFEN(kiwipete);

    std::vector<Search::IterationInfo> fixed;
    auto fixedMove = Search::findBestMove(
        board, 3, [&fixed](const Search::IterationInfo& info) { fixed.push_back(info); });

    std::atomic<bool> stopRequested = false;
    Search::Limits limits;
    limits.depth = 3;
    limits.softDeadline = Search::Clock::now() + std::chrono::hours(1);
    limits.hardDeadline = Search::Clock::now() + std::chrono::hours(1);
    SearchResult limited = runSearch(board, limits, &stopRequested);

    EXPECT_EQ(fixed.size(), limited.iterations.size());
    EXPECT_TRUE(fixedMove.has_value() && limited.bestMove.has_value());

    if (fixedMove.has_value() && limited.bestMove.has_value()) {
        EXPECT_EQ(fixedMove->toUCI(), limited.bestMove->toUCI());
    }

    for (std::size_t i = 0; i < fixed.size() && i < limited.iterations.size(); ++i) {
        EXPECT_EQ(fixed[i].score, limited.iterations[i].score);
        EXPECT_EQ(fixed[i].nodes, limited.iterations[i].nodes);
    }
}

TEST_CASE("An interrupted search does not affect the next search") {
    Board board;
    board.loadFEN(kiwipete);

    auto before = Search::findBestMove(board, 3);

    const std::atomic<bool> stopRequested = true;
    for (int i = 0; i < 5; ++i) {
        runSearch(board, {}, &stopRequested);
    }

    Search::Limits limits;
    limits.hardDeadline = Search::Clock::now() + std::chrono::milliseconds(5);
    runSearch(board, limits, nullptr);

    auto after = Search::findBestMove(board, 3);

    EXPECT_TRUE(before.has_value() && after.has_value());

    if (before.has_value() && after.has_value()) {
        EXPECT_EQ(before->toUCI(), after->toUCI());
    }
}

TEST_CASE("Time allocation splits the clock and keeps a safety margin") {
    // Remaining time spread over 30 moves by default
    EXPECT_EQ(std::int64_t{2000}, Search::allocateTimeMs(60000, 0, 0));

    // Three quarters of the increment is added
    EXPECT_EQ(std::int64_t{2750}, Search::allocateTimeMs(60000, 1000, 0));

    // movestogo replaces the default
    EXPECT_EQ(std::int64_t{6000}, Search::allocateTimeMs(60000, 0, 10));

    // The last move before a time control may not use the overhead margin
    EXPECT_EQ(std::int64_t{1000 - Search::moveOverheadMs}, Search::allocateTimeMs(1000, 0, 1));

    // A large increment cannot exceed the remaining time
    EXPECT_EQ(std::int64_t{100 - Search::moveOverheadMs}, Search::allocateTimeMs(100, 5000, 0));

    // Tiny, empty and negative clocks still give a positive time
    EXPECT_EQ(std::int64_t{1}, Search::allocateTimeMs(10, 0, 0));
    EXPECT_EQ(std::int64_t{1}, Search::allocateTimeMs(0, 0, 0));
    EXPECT_EQ(std::int64_t{1}, Search::allocateTimeMs(-500, -10, 0));
}
