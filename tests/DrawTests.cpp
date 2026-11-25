#include "Board.h"
#include "MoveGenerator.h"
#include "MoveList.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"

#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using Bound = TranspositionTable::Bound;

constexpr int mateScore = 100000;
constexpr int infinity = 1000000;
constexpr int noHistoryPly = std::numeric_limits<int>::max();

struct PlayedMove {
    Move move;
    UndoState undo;
};

// Plays legal moves given in UCI notation, returning them so they can be unmade in reverse order
std::vector<PlayedMove> playMoves(Board& board, std::initializer_list<const char*> moves) {
    std::vector<PlayedMove> played;

    for (const char* uci : moves) {
        MoveList legalMoves;
        MoveGenerator::generateLegalMoves(board, legalMoves);

        bool found = false;

        for (int i = 0; i < legalMoves.count; ++i) {
            if (legalMoves.moves[i].toUCI() == uci) {
                PlayedMove entry{legalMoves.moves[i], UndoState{}};
                board.makeMove(entry.move, entry.undo);
                played.push_back(entry);
                found = true;
                break;
            }
        }

        EXPECT_TRUE(found);

        if (!found) {
            std::cerr << "    illegal test move: " << uci << '\n';
            break;
        }
    }

    return played;
}

void unmakeMoves(Board& board, std::vector<PlayedMove>& played) {
    while (!played.empty()) {
        board.unmakeMove(played.back().move, played.back().undo);
        played.pop_back();
    }
}

std::uint64_t hashAfter(Board& board, const char* uci) {
    auto played = playMoves(board, {uci});
    const std::uint64_t hash = board.getHash();
    unmakeMoves(board, played);
    return hash;
}

// Both scores fail low, both fail high, or both are the same exact score inside the window
bool sameWindowOutcome(int expected, int actual, int alpha, int beta) {
    if (expected <= alpha) {
        return actual <= alpha;
    }

    if (expected >= beta) {
        return actual >= beta;
    }

    return expected == actual;
}

int finalSearchScore(Board& board, int depth, std::optional<Move>& bestMove) {
    int score = -infinity;

    bestMove = Search::findBestMove(
        board, depth, [&score](const Search::IterationInfo& info) { score = info.score; });

    return score;
}

// White to move with only a bare king against king and queen. White's king shuffles between g1
// and h1 while black's shuffles between d8 and e8.
constexpr const char* bareKingFen = "4k3/8/8/8/8/8/q7/7K w - - 0 1";

// Leaves white to move, where g1h1 reaches the same position for the third time
void playToWhiteRepetitionChoice(Board& board) {
    board.loadFEN(bareKingFen);
    playMoves(board,
              {"h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8"});
}

} // namespace

// Threefold repetition

TEST_CASE("Repetition is not detected without position history") {
    Board board;

    EXPECT_FALSE(board.isRepetition());
    EXPECT_FALSE(board.isRepetition(100));
    EXPECT_TRUE(board.getHistory().empty());
}

TEST_CASE("Threefold repetition is detected after knight shuffles") {
    Board board;
    const std::uint64_t start = board.getHash();

    playMoves(board, {"g1f3", "g8f6", "f3g1", "f6g8"});

    // Second occurrence of the starting position
    EXPECT_EQ(start, board.getHash());
    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"g1f3", "g8f6", "f3g1"});

    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"f6g8"});

    // Third occurrence of the starting position
    EXPECT_EQ(start, board.getHash());
    EXPECT_TRUE(board.isRepetition());
}

TEST_CASE("Repetition is detected across different move orders") {
    Board board;

    playMoves(board, {"g1f3", "g8f6", "b1c3", "b8c6"});
    const std::uint64_t fourKnights = board.getHash();

    playMoves(board, {"c3b1", "c6b8", "f3g1", "f6g8"});

    EXPECT_FALSE(board.isRepetition());

    // Same four knights position through a different move order
    playMoves(board, {"b1c3", "b8c6", "g1f3", "g8f6"});

    EXPECT_EQ(fourKnights, board.getHash());
    EXPECT_FALSE(board.isRepetition());
    EXPECT_TRUE(board.isRepetition(100));

    // Return to the starting position for the third time, again by a different order
    playMoves(board, {"f3g1", "f6g8", "c3b1", "c6b8"});

    EXPECT_TRUE(board.isRepetition());
}

TEST_CASE("Twofold repetition counts only when it follows the search root") {
    Board board;

    playMoves(board, {"g1f3", "g8f6", "f3g1", "f6g8"});

    // The earlier occurrence is four plies back
    EXPECT_FALSE(board.isRepetition());
    EXPECT_FALSE(board.isRepetition(4));
    EXPECT_TRUE(board.isRepetition(5));
}

TEST_CASE("Castling rights are part of repetition identity") {
    Board board;
    board.loadFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");

    const std::uint64_t withCastling = board.getHash();

    playMoves(board, {"a1b1", "a8b8", "b1a1", "b8a8"});

    // Same placement, but the queenside castling rights are gone
    EXPECT_TRUE(withCastling != board.getHash());
    EXPECT_FALSE(board.isRepetition(100));

    playMoves(board, {"a1b1", "a8b8", "b1a1", "b8a8"});

    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"a1b1", "a8b8", "b1a1", "b8a8"});

    EXPECT_TRUE(board.isRepetition());
}

TEST_CASE("Legal en passant rights are part of repetition identity") {
    Board board;
    board.loadFEN("4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 1");

    playMoves(board, {"d7d5"});

    // exd6 is legal here, but no longer after the kings shuffle
    const std::uint64_t withEnPassant = board.getHash();

    playMoves(board, {"e1e2", "e8e7", "e2e1", "e7e8"});

    EXPECT_TRUE(withEnPassant != board.getHash());
    EXPECT_FALSE(board.isRepetition(100));

    playMoves(board, {"e1e2", "e8e7", "e2e1", "e7e8"});

    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"e1e2", "e8e7", "e2e1", "e7e8"});

    EXPECT_TRUE(board.isRepetition());
}

TEST_CASE("Unusable en passant square does not affect repetition identity") {
    Board board;
    board.loadFEN("4k3/3p4/8/8/8/8/8/4K3 b - - 0 1");

    playMoves(board, {"d7d5"});

    // No white pawn can capture on d6, so this matches the later positions without it
    const std::uint64_t afterPush = board.getHash();
    EXPECT_EQ(43, board.getEnPassantSquare());

    playMoves(board, {"e1e2", "e8e7", "e2e1", "e7e8"});

    EXPECT_EQ(afterPush, board.getHash());
    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"e1e2", "e8e7", "e2e1", "e7e8"});

    EXPECT_TRUE(board.isRepetition());
}

TEST_CASE("Pinned en passant capture does not affect repetition identity") {
    Board board;
    board.loadFEN("k3r3/3p4/8/4P3/8/8/8/4K3 b - - 0 1");

    playMoves(board, {"d7d5"});

    // exd6 would expose the white king to the rook on e8
    const std::uint64_t afterPush = board.getHash();
    EXPECT_EQ(43, board.getEnPassantSquare());

    playMoves(board, {"e1f1", "a8b8", "f1e1", "b8a8"});

    EXPECT_EQ(afterPush, board.getHash());
    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"e1f1", "a8b8", "f1e1", "b8a8"});

    EXPECT_TRUE(board.isRepetition());
}

// Position history

TEST_CASE("Make and unmake maintain position history") {
    Board board;
    const std::uint64_t start = board.getHash();

    auto first = playMoves(board, {"g1f3"});

    EXPECT_EQ(1ULL, board.getHistory().size());

    if (!board.getHistory().empty()) {
        EXPECT_EQ(start, board.getHistory()[0]);
    }

    unmakeMoves(board, first);

    EXPECT_TRUE(board.getHistory().empty());

    playMoves(board, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1"});
    const std::vector<std::uint64_t> history = board.getHistory();

    auto last = playMoves(board, {"f6g8"});

    EXPECT_TRUE(board.isRepetition());

    unmakeMoves(board, last);

    EXPECT_FALSE(board.isRepetition());
    EXPECT_TRUE(history == board.getHistory());

    playMoves(board, {"f6g8"});

    EXPECT_TRUE(board.isRepetition());
}

TEST_CASE("Loading a position clears position history") {
    Board board;

    playMoves(board, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"});

    EXPECT_TRUE(board.isRepetition());

    board.setStartingPosition();

    EXPECT_TRUE(board.getHistory().empty());
    EXPECT_FALSE(board.isRepetition());

    playMoves(board, {"g1f3", "g8f6"});
    board.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    EXPECT_TRUE(board.getHistory().empty());
}

TEST_CASE("Search restores position history") {
    Board board;

    playMoves(board, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6"});

    const std::vector<std::uint64_t> history = board.getHistory();
    const std::uint64_t hash = board.getHash();

    Search::findBestMove(board, 4);

    EXPECT_TRUE(history == board.getHistory());
    EXPECT_EQ(hash, board.getHash());

    TranspositionTable tt(1);
    Search::negamax(board, 3, 0, -infinity, infinity, nullptr, nullptr, &tt);

    EXPECT_TRUE(history == board.getHistory());
    EXPECT_EQ(hash, board.getHash());
}

// Fifty-move rule

TEST_CASE("Fifty-move rule applies at one hundred halfmoves") {
    Board board;

    board.loadFEN("4k3/8/8/8/8/8/4P3/4K1N1 w - - 99 80");
    EXPECT_FALSE(board.isFiftyMoveRule());

    auto quiet = playMoves(board, {"g1f3"});

    EXPECT_TRUE(board.isFiftyMoveRule());

    unmakeMoves(board, quiet);

    EXPECT_FALSE(board.isFiftyMoveRule());

    playMoves(board, {"e2e3"});

    EXPECT_FALSE(board.isFiftyMoveRule());

    board.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 100 80");
    EXPECT_TRUE(board.isFiftyMoveRule());

    // A capture resets the clock
    board.loadFEN("4k3/8/8/8/8/8/3n4/4K3 w - - 99 80");
    playMoves(board, {"e1d2"});

    EXPECT_FALSE(board.isFiftyMoveRule());
}

TEST_CASE("Search scores the fifty-move rule as a draw") {
    Board board;
    board.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 99 80");

    EXPECT_EQ(0, Search::negamax(board, 1));
    EXPECT_EQ(0, Search::negamax(board, 2));

    std::optional<Move> bestMove;
    EXPECT_EQ(0, finalSearchScore(board, 3, bestMove));
    EXPECT_TRUE(bestMove.has_value());

    // The same position with a fresh clock is clearly winning
    board.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 0 80");

    EXPECT_TRUE(Search::negamax(board, 2) > 500);
}

TEST_CASE("Search prefers resetting the fifty-move clock") {
    Board board;
    board.loadFEN("7k/8/8/8/8/8/1Q4p1/K7 w - - 99 80");

    std::optional<Move> bestMove;
    const int score = finalSearchScore(board, 3, bestMove);

    EXPECT_TRUE(score > 500);
    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_EQ(std::string("b2g2"), bestMove->toUCI());
    }
}

TEST_CASE("Checkmate takes precedence over the fifty-move rule") {
    Board board;
    board.loadFEN("7k/8/5KQ1/8/8/8/8/8 w - - 99 1");

    std::optional<Move> bestMove;
    EXPECT_EQ(mateScore - 1, finalSearchScore(board, 3, bestMove));
    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_EQ(std::string("g6g7"), bestMove->toUCI());
    }

    // Mated and stalemated positions inside the search, already past the limit
    board.loadFEN("7k/6Q1/6K1/8/8/8/8/8 b - - 100 1");

    EXPECT_EQ(-mateScore + 1, Search::negamax(board, 1, 1));
    EXPECT_EQ(-mateScore + 1, Search::negamax(board, 0, 1));

    board.loadFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 100 1");

    EXPECT_EQ(0, Search::negamax(board, 1, 1));
}

// Insufficient material

TEST_CASE("Insufficient material detects dead positions") {
    struct Scenario {
        const char* fen;
        bool dead;
    };

    const Scenario scenarios[] = {
        {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", true},      // K v K
        {"4k3/8/8/8/8/8/8/3NK3 w - - 0 1", true},     // KN v K
        {"4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", true},    // KB v K
        {"3bk3/8/8/8/8/8/8/4K3 b - - 0 1", true},     // K v KB
        {"4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1", true},   // KB v KB, both dark squared
        {"4k3/8/8/8/8/4B3/8/2B1K3 w - - 0 1", true},  // KBB v K, both dark squared
        {"2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1", false}, // KB v KB, opposite colors
        {"3nk3/8/8/8/8/8/8/3NK3 w - - 0 1", false},   // KN v KN
        {"4k3/8/8/8/8/8/8/1N1NK3 w - - 0 1", false},  // KNN v K
        {"3nk3/8/8/8/8/8/8/2B1K3 w - - 0 1", false},  // KB v KN
        {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", false},   // KP v K
        {"4k3/8/8/8/8/8/8/R3K3 w - - 0 1", false},    // KR v K
        {"4k3/8/8/8/8/8/8/3QK3 w - - 0 1", false},    // KQ v K
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false},
    };

    for (const auto& scenario : scenarios) {
        Board board;
        board.loadFEN(scenario.fen);

        if (board.isInsufficientMaterial() != scenario.dead) {
            std::cerr << "    position: " << scenario.fen << '\n';
        }

        EXPECT_EQ(scenario.dead, board.isInsufficientMaterial());
    }
}

TEST_CASE("Search scores insufficient material as a draw") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1");

    EXPECT_EQ(0, Search::negamax(board, 2));

    std::optional<Move> bestMove;
    EXPECT_EQ(0, finalSearchScore(board, 3, bestMove));

    // The root itself is never scored as a draw, so a move is still returned
    EXPECT_TRUE(bestMove.has_value());
}

TEST_CASE("Search scores a capture into a dead position as a draw") {
    Board board;
    board.loadFEN("8/8/8/8/8/P7/1k6/4K2B b - - 0 1");

    std::optional<Move> bestMove;
    EXPECT_EQ(0, finalSearchScore(board, 3, bestMove));
    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_EQ(std::string("b2a3"), bestMove->toUCI());
    }
}

// Repetition in search

TEST_CASE("Search claims a threefold repetition when losing") {
    Board board;
    playToWhiteRepetitionChoice(board);

    std::optional<Move> bestMove;
    EXPECT_EQ(0, finalSearchScore(board, 3, bestMove));
    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_EQ(std::string("g1h1"), bestMove->toUCI());
    }

    // Without the game history the same position is lost
    Board fresh;
    fresh.loadFEN("3k4/8/8/8/8/8/q7/6K1 w - - 0 1");

    EXPECT_EQ(board.getHash(), fresh.getHash());
    EXPECT_TRUE(finalSearchScore(fresh, 3, bestMove) < -500);
}

TEST_CASE("Search avoids a threefold repetition when winning") {
    Board board;
    board.loadFEN(bareKingFen);
    playMoves(board, {"h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8", "g1h1"});

    // d8e8 would reach the starting position for the third time
    std::optional<Move> bestMove;
    EXPECT_TRUE(finalSearchScore(board, 3, bestMove) > 500);
    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_TRUE(bestMove->toUCI() != "d8e8");
    }
}

TEST_CASE("Negamax scores a repetition after the root as a draw") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/R3K1N1 w - - 0 1");
    playMoves(board, {"g1f3", "e8d8", "f3g1", "d8e8"});

    // At ply 5 the earlier occurrence is inside the search line; at ply 4 it is the root
    EXPECT_EQ(0, Search::negamax(board, 1, 5));
    EXPECT_EQ(0, Search::negamax(board, 0, 5));
    EXPECT_TRUE(Search::negamax(board, 1, 4) > 0);
    EXPECT_TRUE(Search::negamax(board, 0, 4) > 0);
    EXPECT_TRUE(Search::negamax(board, 1) > 0);
}

// Transposition table interaction

TEST_CASE("Cached scores do not override a repetition draw") {
    Board board;
    playToWhiteRepetitionChoice(board);

    // Pretend an earlier search found g1h1 winning for white
    const std::uint64_t repeated = hashAfter(board, "g1h1");

    TranspositionTable tt(1);
    tt.store(repeated, 10, -50000, Bound::Exact, Move{}, 1);

    EXPECT_EQ(0, Search::negamax(board, 3, 0, -infinity, infinity, nullptr, nullptr, &tt));

    // The entry itself is used when the position is reached without repetition
    Board fresh;
    fresh.loadFEN("3k4/8/8/8/8/8/q7/7K b - - 0 1");

    EXPECT_EQ(repeated, fresh.getHash());
    EXPECT_EQ(-50000, Search::negamax(fresh, 3, 1, -infinity, infinity, nullptr, nullptr, &tt));
}

TEST_CASE("Repetition draws are not stored in the transposition table") {
    Board board;
    playToWhiteRepetitionChoice(board);

    const std::uint64_t repeated = hashAfter(board, "g1h1");

    TranspositionTable tt(1);
    EXPECT_EQ(0, Search::negamax(board, 3, 0, -infinity, infinity, nullptr, nullptr, &tt));

    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(repeated, 0, -infinity, infinity, 1, score, move));

    // Reaching the same position without the repetition must not see a draw
    Board fresh;
    fresh.loadFEN("3k4/8/8/8/8/8/q7/7K b - - 0 1");

    const int plainScore = Search::negamax(fresh, 2, 1);
    const int tableScore = Search::negamax(fresh, 2, 1, -infinity, infinity, nullptr, nullptr, &tt);

    EXPECT_TRUE(plainScore > 500);
    EXPECT_EQ(plainScore, tableScore);
}

TEST_CASE("Scores searched with a lower clock are not reused near the fifty-move limit") {
    Board board;
    board.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 99 80");

    TranspositionTable tt(1);
    tt.store(board.getHash(), 5, 777, Bound::Exact, Move{}, 0);

    // A score searched with a lower clock is not reused once the limit is within reach
    EXPECT_EQ(0, Search::negamax(board, 2, 0, -infinity, infinity, nullptr, nullptr, &tt));

    Board lowClock;
    lowClock.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 0 80");

    EXPECT_EQ(board.getHash(), lowClock.getHash());
    EXPECT_EQ(777, Search::negamax(lowClock, 2, 0, -infinity, infinity, nullptr, nullptr, &tt));
}

TEST_CASE("Fifty-move draws in the transposition table are not reused at another clock") {
    Board board;
    board.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 99 80");

    TranspositionTable tt(1);
    EXPECT_EQ(0, Search::negamax(board, 2, 0, -infinity, infinity, nullptr, nullptr, &tt));

    // The draw is stored for clock 99 only
    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(board.getHash(), 0, -infinity, infinity, 0, score, move, 0));
    EXPECT_FALSE(tt.probe(board.getHash(), 0, -infinity, infinity, 0, score, move, 98));
    EXPECT_TRUE(tt.probe(board.getHash(), 0, -infinity, infinity, 0, score, move, 99));
    EXPECT_EQ(0, score);

    // The same position with a fresh clock is searched normally
    Board lowClock;
    lowClock.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 0 80");

    const int plainScore = Search::negamax(lowClock, 2);

    EXPECT_TRUE(plainScore > 500);
    EXPECT_EQ(plainScore,
              Search::negamax(lowClock, 2, 0, -infinity, infinity, nullptr, nullptr, &tt));
}

TEST_CASE("Repetition-dependent scores do not contaminate the root entry") {
    Board board;
    playToWhiteRepetitionChoice(board);

    // Both searches share one table, so a contaminated entry would affect the second
    TranspositionTable tt(1);
    EXPECT_EQ(0, Search::negamax(board, 3, 0, -infinity, infinity, nullptr, nullptr, &tt));

    // The root score relied on game history, so it must not be stored
    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(board.getHash(), 0, -infinity, infinity, 0, score, move));

    Board fresh;
    fresh.loadFEN("3k4/8/8/8/8/8/q7/6K1 w - - 0 1");

    EXPECT_EQ(board.getHash(), fresh.getHash());

    const int plainScore = Search::negamax(fresh, 3);

    EXPECT_TRUE(plainScore < -500);
    EXPECT_EQ(plainScore, Search::negamax(fresh, 3, 0, -infinity, infinity, nullptr, nullptr, &tt));
}

TEST_CASE("Repetition-dependent scores do not contaminate ancestor entries") {
    Board board;
    playToWhiteRepetitionChoice(board);

    // After g1f1 d8e8, white can return with f1g1 for a threefold repetition. The node two plies
    // below the root relies on that draw even though it is not itself a repetition.
    TranspositionTable tt(1);
    Search::negamax(board, 4, 0, -infinity, infinity, nullptr, nullptr, &tt);

    auto played = playMoves(board, {"g1f1", "d8e8"});
    const std::uint64_t ancestor = board.getHash();
    unmakeMoves(board, played);

    // Its "at least a draw" bound only holds with this game history
    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(ancestor, 0, -infinity, -100, 2, score, move));

    Board fresh;
    fresh.loadFEN("4k3/8/8/8/8/8/q7/5K2 w - - 0 1");

    EXPECT_EQ(ancestor, fresh.getHash());
    EXPECT_TRUE(Search::negamax(fresh, 2) < -500);

    // Without the history f1g1 is no draw. Fail-soft scores outside the window may differ, but
    // both searches must agree on which side of the window the result lies. Each search starts
    // from the table exactly as the history search left it.
    const int windows[][2] = {{-infinity, infinity}, {-100, -50}, {-1000, -1}, {-1, 1}};

    for (const auto& window : windows) {
        const int alpha = window[0];
        const int beta = window[1];

        // As the root and at the ply where the entry was stored
        for (const int ply : {0, 2}) {
            const int plainScore = Search::negamax(fresh, 2, ply, alpha, beta);
            TranspositionTable shared = tt;
            const int tableScore =
                Search::negamax(fresh, 2, ply, alpha, beta, nullptr, nullptr, &shared);

            EXPECT_TRUE(sameWindowOutcome(plainScore, tableScore, alpha, beta));
        }
    }
}

TEST_CASE("Search reports the earliest occurrence a repetition relies on") {
    Board board;

    playMoves(board, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"});

    // Threefold relies on the farthest occurrence, twofold after the root on the nearest
    EXPECT_EQ(8, board.repetitionDistance());
    EXPECT_EQ(4, board.repetitionDistance(5));
    EXPECT_EQ(0, Board().repetitionDistance(100));

    // g1h1 repeats game plies 3 and 7, which are 7 and 3 plies before the root
    playToWhiteRepetitionChoice(board);

    int historyPly = noHistoryPly;
    Search::negamax(board, 1, 0, -infinity, infinity, nullptr, nullptr, nullptr, &historyPly);

    EXPECT_EQ(-7, historyPly);
}

TEST_CASE("Repetitions first reached inside the subtree are still stored") {
    Board board;
    board.loadFEN(bareKingFen);

    TranspositionTable tt(1);
    int historyPly = noHistoryPly;

    const int score =
        Search::negamax(board, 5, 0, -infinity, infinity, nullptr, nullptr, &tt, &historyPly);

    // The search did meet repetitions, but only of positions below the root
    EXPECT_TRUE(historyPly != noHistoryPly);
    EXPECT_TRUE(historyPly > 0);

    int stored = 0;
    Move move{};

    EXPECT_TRUE(tt.probe(board.getHash(), 5, -infinity, infinity, 0, stored, move));
    EXPECT_EQ(score, stored);
}

TEST_CASE("Scores relying on a repetition of the node itself are not stored") {
    Board board;
    board.loadFEN(bareKingFen);

    // Searched below the root, lines that return here are twofold draws. Searched as the root
    // they are not, so the score depends on where this position is reached.
    TranspositionTable tt(1);
    int historyPly = noHistoryPly;

    Search::negamax(board, 4, 1, -infinity, infinity, nullptr, nullptr, &tt, &historyPly);

    EXPECT_EQ(1, historyPly);

    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(board.getHash(), 0, -infinity, infinity, 1, score, move));
}
