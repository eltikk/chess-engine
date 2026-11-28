#include "Board.h"
#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"

#include <atomic>
#include <limits>
#include <string>

// The halfmove clock is not part of the hash. A score that relied on a fifty-move draw must only be
// reused at the clock it was searched with, and a mate score must not be reused where the
// fifty-move rule would end the game first.

namespace {

using Bound = TranspositionTable::Bound;

constexpr int mateScore = 100000;
constexpr int mateThreshold = mateScore - 1000;
constexpr int infinity = 1000000;
constexpr int noHistoryPly = std::numeric_limits<int>::max();

std::string withClock(const std::string& placement, int halfmoveClock) {
    return placement + " - - " + std::to_string(halfmoveClock) + " 1";
}

// White mates in three with only king and rook moves, so the mating line never resets the clock
std::string rookMateFen(int halfmoveClock) {
    return withClock("1k6/8/8/2K5/8/8/8/7R w", halfmoveClock);
}

// Black to move is mated in three moves by king and rook, again without a clock reset
std::string rookMatedFen(int halfmoveClock) {
    return withClock("k7/8/2K5/8/8/8/8/1R6 b", halfmoveClock);
}

// White, a queen down, forks king and queen with Nf7+. Black must answer with a king move, and
// only then can white take the queen. From clock 98 that king move reaches the limit inside
// quiescence, so the fork is a fifty-move draw; from clock 0 it wins the queen.
std::string forkFen(int halfmoveClock) {
    return withClock("3q3k/p7/8/4N3/8/8/P7/1K6 w", halfmoveClock);
}

// White's king is boxed in, so every white move is a pawn move or capture
std::string pawnMovesOnlyFen(int halfmoveClock) {
    return withClock("7k/8/8/8/8/2n5/PP6/K7 w", halfmoveClock);
}

// King and queen against king: every line keeps the clock counting
std::string kingQueenFen(int halfmoveClock) {
    return withClock("7k/8/8/8/8/8/1Q6/K7 w", halfmoveClock);
}

// White's only legal move is Ka1-b1, which keeps the clock counting: a2 is covered by the b3 pawn
// and the b2 pawn is blocked
std::string forcedKingFen(int halfmoveClock) {
    return withClock("7k/8/8/8/8/1p6/1P6/K7 w", halfmoveClock);
}

// The position after Ka1-b1, black to move
std::string afterForcedKingFen(int halfmoveClock) {
    return withClock("7k/8/8/8/8/1p6/1P6/1K6 b", halfmoveClock);
}

// Black's Kg8-h8 reaches forcedKingFen
std::string beforeForcedKingFen(int halfmoveClock) {
    return withClock("6k1/8/8/8/8/1p6/1P6/K7 b", halfmoveClock);
}

// White's king on b1 has two moves, Ka1 and Kc1, both keeping the clock counting
std::string twoKingMovesFen(int halfmoveClock) {
    return withClock("7k/8/8/8/8/1p6/1P6/1K6 w", halfmoveClock);
}

Board boardFrom(const std::string& fen) {
    Board board;
    board.loadFEN(fen);
    return board;
}

std::uint64_t hashOf(const std::string& fen) { return boardFrom(fen).getHash(); }

int search(const std::string& fen, int depth, TranspositionTable* tt) {
    Board board = boardFrom(fen);
    return Search::negamax(board, depth, 0, -infinity, infinity, nullptr, nullptr, tt);
}

int finalScore(const std::string& fen, int depth, TranspositionTable* tt) {
    Board board = boardFrom(fen);
    Search::Limits limits;
    limits.depth = depth;

    int score = 0;
    Search::findBestMove(
        board, limits, nullptr, [&score](const Search::IterationInfo& info) { score = info.score; },
        tt);

    return score;
}

bool isMateScore(int score) { return score >= mateThreshold || score <= -mateThreshold; }

struct ClockSearch {
    int score;
    int historyPly;
    int clockPly;
};

ClockSearch searchWithClock(const std::string& fen, int depth, int alpha, int beta,
                            TranspositionTable* tt) {
    Board board = boardFrom(fen);
    ClockSearch result{0, noHistoryPly, noHistoryPly};
    result.score = Search::negamax(board, depth, 0, alpha, beta, nullptr, nullptr, tt,
                                   &result.historyPly, nullptr, nullptr, &result.clockPly);
    return result;
}

bool usableAt(TranspositionTable& tt, const std::string& fen, int depth, int alpha, int beta,
              int halfmoveClock) {
    int score = 0;
    Move move{};
    return tt.probe(hashOf(fen), depth, alpha, beta, 0, score, move, halfmoveClock);
}

Move findMove(Board& board, const std::string& uci) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i].toUCI() == uci) {
            return moves.moves[i];
        }
    }

    EXPECT_TRUE(false);
    std::cerr << "    illegal test move: " << uci << '\n';
    return Move{};
}

std::uint64_t hashAfter(const std::string& fen, const std::string& uci) {
    Board board = boardFrom(fen);
    UndoState undo;
    board.makeMove(findMove(board, uci), undo);
    return board.getHash();
}

} // namespace

// Mate distances at the fifty-move limit

TEST_CASE("Deep table scores are not reused where the fifty-move rule cuts their lines short") {
    TranspositionTable tt(1);
    EXPECT_EQ(mateScore - 5, search(rookMateFen(0), 7, &tt));

    // From clock 96 the fourth move reaches the limit before the mate, so no search of this
    // position can prove a mate, however deep
    for (const int clock : {96, 97, 98}) {
        TranspositionTable warm = tt;
        const int cold = search(rookMateFen(clock), 2, nullptr);
        const int score = search(rookMateFen(clock), 2, &warm);

        EXPECT_FALSE(isMateScore(cold));
        EXPECT_FALSE(isMateScore(score));
        EXPECT_EQ(cold, score);
    }

    // Where the clock leaves room the deeper entry is still used
    TranspositionTable warm = tt;
    EXPECT_EQ(mateScore - 5, search(rookMateFen(0), 2, &warm));
}

TEST_CASE("A reused table does not report a mate the fifty-move rule prevents") {
    TranspositionTable tt(16);
    EXPECT_EQ(-(mateScore - 6), finalScore(rookMatedFen(0), 8, &tt));

    // From clock 95 white's fifth halfmove reaches the limit with black still to be mated
    EXPECT_FALSE(isMateScore(finalScore(rookMatedFen(95), 4, nullptr)));
    EXPECT_FALSE(isMateScore(finalScore(rookMatedFen(95), 4, &tt)));
}

TEST_CASE("Mate on the hundredth halfmove takes precedence with a reused table") {
    TranspositionTable tt(16);
    finalScore(rookMatedFen(0), 8, &tt);

    EXPECT_EQ(-(mateScore - 6), finalScore(rookMatedFen(94), 6, nullptr));
    EXPECT_EQ(-(mateScore - 6), finalScore(rookMatedFen(94), 6, &tt));
}

TEST_CASE("Mate scores are reused near the limit when the mate lands in time") {
    TranspositionTable tt(1);
    EXPECT_EQ(mateScore - 5, search(rookMateFen(0), 7, &tt));

    // From clock 95 the mate lands on halfmove 100. A depth-5 search of anything else would
    // reach the limit, yet the stored mate holds and is returned without searching.
    Board board = boardFrom(rookMateFen(95));
    std::uint64_t nodes = 0;
    const std::uint64_t cutoffsBefore = tt.getCutoffs();

    EXPECT_EQ(mateScore - 5,
              Search::negamax(board, 5, 0, -infinity, infinity, &nodes, nullptr, &tt));
    EXPECT_EQ(1ULL, nodes);
    EXPECT_EQ(cutoffsBefore + 1, tt.getCutoffs());
}

TEST_CASE("Mate bounds are reused up to the halfmove where the mate lands") {
    const std::uint64_t key = 0x0F1E2D3C4B5A6978ULL;
    int score = 0;
    Move move{};

    // At least mate in five plies from this position
    TranspositionTable lower(1);
    lower.store(key, 6, mateScore - 5, Bound::Lower, Move{}, 0);
    EXPECT_TRUE(lower.probe(key, 6, 0, 1000, 0, score, move, 95));
    EXPECT_EQ(mateScore - 5, score);
    EXPECT_FALSE(lower.probe(key, 6, 0, 1000, 0, score, move, 96));

    // Mated in at most five plies
    TranspositionTable upper(1);
    upper.store(key, 6, -(mateScore - 5), Bound::Upper, Move{}, 0);
    EXPECT_TRUE(upper.probe(key, 6, -1000, 0, 0, score, move, 95));
    EXPECT_EQ(-(mateScore - 5), score);
    EXPECT_FALSE(upper.probe(key, 6, -1000, 0, 0, score, move, 96));

    // Stored three plies from the root, the same mate counts five plies from the position
    TranspositionTable deeper(1);
    deeper.store(key, 6, mateScore - 8, Bound::Lower, Move{}, 3);
    EXPECT_TRUE(deeper.probe(key, 6, 0, 1000, 1, score, move, 95));
    EXPECT_EQ(mateScore - 6, score);
    EXPECT_FALSE(deeper.probe(key, 6, 0, 1000, 1, score, move, 96));
}

TEST_CASE("The mate-distance condition is conservative for mates that reset the clock") {
    // 1. Rxa8+ Nf8 2. Rxf8# captures on every white move, so the mate holds from any clock
    const std::string placement = "r6k/3n2pp/8/8/8/8/8/RR4K1 w";
    TranspositionTable tt(1);
    EXPECT_EQ(mateScore - 3, search(withClock(placement, 0), 4, &tt));
    EXPECT_EQ(mateScore - 3, search(withClock(placement, 99), 4, nullptr));

    // The table cannot tell, so it refuses the stored mate, and the search finds it again
    EXPECT_FALSE(usableAt(tt, withClock(placement, 99), 4, -infinity, infinity, 99));
    EXPECT_EQ(mateScore - 3, search(withClock(placement, 99), 4, &tt));
}

// Fifty-move draws and other scores near the limit

TEST_CASE("Fifty-move draws reached in quiescence are only reused at the same clock") {
    const int drawn = search(forkFen(98), 1, nullptr);
    const int winning = search(forkFen(0), 1, nullptr);

    EXPECT_EQ(0, drawn);
    EXPECT_TRUE(winning > 200);

    TranspositionTable tt(1);
    EXPECT_EQ(drawn, search(forkFen(98), 1, &tt));

    EXPECT_FALSE(usableAt(tt, forkFen(0), 1, -infinity, infinity, 0));
    EXPECT_FALSE(usableAt(tt, forkFen(97), 1, -infinity, infinity, 97));

    // At the clock it was searched with, the score is reused without searching
    Board sameClock = boardFrom(forkFen(98));
    std::uint64_t nodes = 0;
    EXPECT_EQ(drawn, Search::negamax(sameClock, 1, 0, -infinity, infinity, &nodes, nullptr, &tt));
    EXPECT_EQ(1ULL, nodes);

    EXPECT_EQ(winning, search(forkFen(0), 1, &tt));

    // Stored from clock 0, the score holds from clock 97, where the queen falls on halfmove 99
    EXPECT_EQ(winning, search(forkFen(97), 1, nullptr));
    EXPECT_EQ(winning, search(forkFen(97), 1, &tt));
}

TEST_CASE("Fifty-move draws rely on the clock since the last capture or pawn move") {
    // Every white move reaches clock 100 at ply 1, counted from a reset 99 plies before the root
    const ClockSearch queen = searchWithClock(kingQueenFen(99), 2, -infinity, infinity, nullptr);
    EXPECT_EQ(1 - 100, queen.clockPly);
    EXPECT_EQ(noHistoryPly, queen.historyPly);

    // The fork's king move reaches clock 100 at ply 2, inside quiescence
    const ClockSearch fork = searchWithClock(forkFen(98), 1, -infinity, infinity, nullptr);
    EXPECT_EQ(2 - 100, fork.clockPly);
    EXPECT_EQ(noHistoryPly, fork.historyPly);

    // Every white move resets the clock, so no draw is reached
    EXPECT_EQ(noHistoryPly,
              searchWithClock(pawnMovesOnlyFen(99), 3, -infinity, infinity, nullptr).clockPly);
}

TEST_CASE("Scores near the fifty-move limit are stored when no fifty-move draw was reached") {
    TranspositionTable tt(1);
    const int score = search(pawnMovesOnlyFen(99), 3, &tt);

    EXPECT_EQ(search(pawnMovesOnlyFen(99), 3, nullptr), score);

    // The score holds along any path here, so a lower clock reuses it without searching
    Board lowClock = boardFrom(pawnMovesOnlyFen(0));
    std::uint64_t nodes = 0;

    EXPECT_EQ(score, Search::negamax(lowClock, 3, 0, -infinity, infinity, &nodes, nullptr, &tt));
    EXPECT_EQ(1ULL, nodes);
}

TEST_CASE("Other scores are reused only while the requested depth stays short of the limit") {
    const std::uint64_t key = 0x0F1E2D3C4B5A6978ULL;
    int score = 0;
    Move move{};

    TranspositionTable tt(1);
    tt.store(key, 6, 300, Bound::Exact, Move{}, 0);
    EXPECT_TRUE(tt.probe(key, 4, -infinity, infinity, 0, score, move, 95));
    EXPECT_EQ(300, score);
    EXPECT_FALSE(tt.probe(key, 4, -infinity, infinity, 0, score, move, 96));
}

TEST_CASE("Positions near the fifty-move limit still read the stored best move") {
    TranspositionTable tt(1);
    search(kingQueenFen(0), 3, &tt);

    const std::uint64_t hitsBefore = tt.getHits();

    // Scores searched from clock 0 are unusable this close to the limit, but entries are still
    // found for move ordering
    EXPECT_EQ(search(kingQueenFen(98), 3, nullptr), search(kingQueenFen(98), 3, &tt));
    EXPECT_TRUE(tt.getHits() > hitsBefore);
}

// Clock dependence through table cutoffs

TEST_CASE("A cutoff on a clock-tagged entry ties the position above it to the clock") {
    TranspositionTable tt(1);
    tt.store(hashOf(afterForcedKingFen(51)), 5, -77, Bound::Exact, Move{}, 1, 51);

    // The child's clock follows from the last reset, 50 plies before the root
    const ClockSearch result = searchWithClock(forcedKingFen(50), 3, -infinity, infinity, &tt);
    EXPECT_EQ(77, result.score);
    EXPECT_EQ(1 - 51, result.clockPly);

    EXPECT_FALSE(usableAt(tt, forcedKingFen(10), 3, -infinity, infinity, 10));
    EXPECT_TRUE(usableAt(tt, forcedKingFen(50), 3, -infinity, infinity, 50));
}

TEST_CASE("Tagged lower and upper bound cutoffs tie the position above to the clock") {
    const std::uint64_t child = hashOf(afterForcedKingFen(51));

    // The child fails high on a lower bound, so the parent fails low and stores an upper bound
    TranspositionTable lower(1);
    lower.store(child, 5, 200, Bound::Lower, Move{}, 1, 51);
    const ClockSearch failLow = searchWithClock(forcedKingFen(50), 3, -100, 100, &lower);

    EXPECT_EQ(-200, failLow.score);
    EXPECT_EQ(1 - 51, failLow.clockPly);
    EXPECT_FALSE(usableAt(lower, forcedKingFen(10), 3, -150, -100, 10));
    EXPECT_TRUE(usableAt(lower, forcedKingFen(50), 3, -150, -100, 50));

    // The child fails low on an upper bound, so the parent fails high and stores a lower bound
    TranspositionTable upper(1);
    upper.store(child, 5, -200, Bound::Upper, Move{}, 1, 51);
    const ClockSearch failHigh = searchWithClock(forcedKingFen(50), 3, -100, 100, &upper);

    EXPECT_EQ(200, failHigh.score);
    EXPECT_EQ(1 - 51, failHigh.clockPly);
    EXPECT_FALSE(usableAt(upper, forcedKingFen(10), 3, 100, 150, 10));
    EXPECT_TRUE(usableAt(upper, forcedKingFen(50), 3, 100, 150, 50));
}

TEST_CASE("Clock dependence carries through consecutive table cutoffs") {
    TranspositionTable tt(1);
    tt.store(hashOf(afterForcedKingFen(51)), 5, -77, Bound::Exact, Move{}, 1, 51);

    // The parent is stored for clock 50 only, and its entry then cuts off below the grandparent
    EXPECT_EQ(77, searchWithClock(forcedKingFen(50), 3, -infinity, infinity, &tt).score);

    const ClockSearch result =
        searchWithClock(beforeForcedKingFen(49), 4, -infinity, infinity, &tt);
    EXPECT_EQ(1 - 50, result.clockPly);
    EXPECT_FALSE(usableAt(tt, beforeForcedKingFen(10), 4, -infinity, infinity, 10));
    EXPECT_TRUE(usableAt(tt, beforeForcedKingFen(49), 4, -infinity, infinity, 49));
}

TEST_CASE("Clock dependence follows the search whose score principal variation search keeps") {
    const std::string parent = twoKingMovesFen(50);
    const std::uint64_t second = hashAfter(parent, "b1c1");

    // A shallow parent entry makes Kb1-a1 the first move, so Kb1-c1 gets a null window
    auto tableWithSecond = [&](int score, Bound bound) {
        TranspositionTable tt(1);
        Board board = boardFrom(parent);
        tt.store(board.getHash(), 1, 0, Bound::Upper, findMove(board, "b1a1"), 0);
        tt.store(second, 5, score, bound, Move{}, 1, 51);
        return tt;
    };

    // The null window fails high on the tagged score and the re-search keeps it
    TranspositionTable reSearched = tableWithSecond(-5000, Bound::Exact);
    const ClockSearch kept = searchWithClock(parent, 3, -infinity, infinity, &reSearched);
    EXPECT_EQ(2ULL, reSearched.getCutoffs());
    EXPECT_EQ(5000, kept.score);
    EXPECT_EQ(1 - 51, kept.clockPly);
    EXPECT_FALSE(usableAt(reSearched, twoKingMovesFen(10), 3, -infinity, infinity, 10));

    // The null window fails low on a tagged bound, which rules the move out without a re-search
    TranspositionTable ruledOut = tableWithSecond(5000, Bound::Lower);
    const ClockSearch rejected = searchWithClock(parent, 3, -infinity, infinity, &ruledOut);
    EXPECT_EQ(1ULL, ruledOut.getCutoffs());
    EXPECT_TRUE(rejected.score < 1000);
    EXPECT_EQ(1 - 51, rejected.clockPly);
    EXPECT_FALSE(usableAt(ruledOut, twoKingMovesFen(10), 3, -infinity, infinity, 10));

    // The null window fails high on a tagged upper bound that the full window cannot use, so the
    // re-search searches the move. Only its score is kept, which holds at any clock.
    TranspositionTable discarded = tableWithSecond(-5000, Bound::Upper);
    const ClockSearch replaced = searchWithClock(parent, 3, -infinity, infinity, &discarded);
    EXPECT_EQ(1ULL, discarded.getCutoffs());
    EXPECT_TRUE(replaced.score < 1000);
    EXPECT_EQ(noHistoryPly, replaced.clockPly);
    EXPECT_TRUE(usableAt(discarded, twoKingMovesFen(10), 3, -infinity, infinity, 10));
}

TEST_CASE("Clock-tagged cutoffs after a capture or pawn move do not tie the position above") {
    // Every white move resets the clock, so no child's clock depends on the parent's
    const std::string parent = pawnMovesOnlyFen(60);
    TranspositionTable tt(1);
    tt.store(hashAfter(parent, "a2a3"), 5, -3000, Bound::Exact, Move{}, 1, 0);

    const ClockSearch result = searchWithClock(parent, 3, -infinity, infinity, &tt);
    EXPECT_EQ(3000, result.score);
    EXPECT_EQ(1, result.clockPly);
    EXPECT_TRUE(usableAt(tt, pawnMovesOnlyFen(10), 3, -infinity, infinity, 10));
}

TEST_CASE("Scores relying on tagged cutoffs in a real search are stored for that clock only") {
    // Late move reductions would shape the tree differently and reach this position elsewhere
    TranspositionTable tt(16);
    Board root = boardFrom(kingQueenFen(90));
    Search::Limits limits;
    limits.depth = 9;
    limits.lateMoveReductions = false;
    Search::findBestMove(root, limits, nullptr, {}, &tt);

    // Reached at ply 7 and clock 97, where a clock-tagged cutoff below it made it a lower bound of
    // -860. A fresh search gives -860 at clock 97 but -864 at clock 0.
    const std::string placement = "8/8/8/6k1/8/K7/Q7/8 b";
    Board atClock = boardFrom(withClock(placement, 97));
    Board atZero = boardFrom(withClock(placement, 0));
    EXPECT_EQ(-860, Search::negamax(atClock, 2, 7, -infinity, infinity));
    EXPECT_EQ(-864, Search::negamax(atZero, 2, 7, -infinity, infinity));

    int score = 0;
    Move move{};
    bool clockDependent = false;
    const std::uint64_t hitsBefore = tt.getHits();

    EXPECT_TRUE(tt.probe(atZero.getHash(), 2, -861, -860, 7, score, move, 97, &clockDependent));
    EXPECT_EQ(-860, score);
    EXPECT_TRUE(clockDependent);

    // Found at clock 0, but not returned
    EXPECT_FALSE(tt.probe(atZero.getHash(), 2, -865, -864, 7, score, move, 0));
    EXPECT_EQ(hitsBefore + 2, tt.getHits());

    Board warm = boardFrom(withClock(placement, 0));
    EXPECT_EQ(Search::negamax(atZero, 2, 7, -865, -864),
              Search::negamax(warm, 2, 7, -865, -864, nullptr, nullptr, &tt));
}

TEST_CASE("Interrupted searches near the limit restore the board and store nothing false") {
    TranspositionTable tt(1);
    search(forkFen(98), 3, &tt);

    const Board original = boardFrom(forkFen(98));
    const int winning = search(forkFen(0), 1, nullptr);
    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool rootNeverStored = true;
    bool lowClockUnaffected = true;

    for (std::uint64_t call = 1; call <= Search::stopCheckInterval; call += 37) {
        Board board = original;
        Search::Control control;
        control.stopRequested = &stopRequested;
        control.checkCounter = Search::stopCheckInterval - call;

        Search::negamax(board, 5, 0, -infinity, infinity, nullptr, nullptr, &tt, nullptr, &control);

        allRestored = allRestored && board.getHash() == original.getHash() &&
                      board.getHalfmoveClock() == original.getHalfmoveClock() &&
                      board.getHistory() == original.getHistory() &&
                      board.getOccupied() == original.getOccupied();

        rootNeverStored = rootNeverStored && !usableAt(tt, forkFen(98), 5, -infinity, infinity, 98);

        // Nothing stored from clock 98 turns the fork into a draw at clock 0
        TranspositionTable copy = tt;
        lowClockUnaffected = lowClockUnaffected && search(forkFen(0), 1, &copy) == winning;
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(rootNeverStored);
    EXPECT_TRUE(lowClockUnaffected);
}

// Clock tag range

TEST_CASE("Clock tags from the any-clock sentinel up are not stored") {
    const std::uint64_t key = 0x0123456789ABCDEFULL;
    int score = 0;
    Move move{};

    for (const int clock : {254, 255, 256, 300}) {
        TranspositionTable tt(1);
        tt.store(key, 3, 42, Bound::Exact, Move{}, 0, clock);

        // A stored entry counts a hit even when its score is unusable
        tt.probe(key, 3, -infinity, infinity, 0, score, move, 0);
        EXPECT_EQ(clock == 254 ? 1ULL : 0ULL, tt.getHits());
        EXPECT_FALSE(tt.probe(key, 3, -infinity, infinity, 0, score, move, 10));
    }

    // Scores that did not rely on a fifty-move draw hold at any clock
    TranspositionTable untagged(1);
    untagged.store(key, 3, 42, Bound::Exact, Move{}, 0);
    EXPECT_TRUE(untagged.probe(key, 3, -infinity, infinity, 0, score, move, 10));
    EXPECT_EQ(42, score);
}

TEST_CASE("Over-range clocks at a searched root never store a draw usable at another clock") {
    for (const int clock : {254, 255, 256, 300}) {
        TranspositionTable tt(1);
        EXPECT_EQ(0, search(kingQueenFen(clock), 2, &tt));

        EXPECT_FALSE(usableAt(tt, kingQueenFen(clock), 2, -infinity, infinity, 0));
        EXPECT_FALSE(usableAt(tt, kingQueenFen(clock), 2, -infinity, infinity, 50));
        EXPECT_TRUE(search(kingQueenFen(0), 2, &tt) > 500);
    }
}
