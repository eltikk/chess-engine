#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace {

using Bound = TranspositionTable::Bound;

constexpr int mateScore = 100000;
constexpr std::uint64_t testKey = 0x123456789ABCDEF0ULL;

const Move testMove{12, 28, MoveType::DoublePawnPush};

bool probeScore(TranspositionTable& tt, std::uint64_t key, int depth, int alpha, int beta,
                int& score, int ply = 0) {
    Move move{};
    return tt.probe(key, depth, alpha, beta, ply, score, move);
}

bool boardsMatch(const Board& a, const Board& b) {
    for (int i = WHITE_PAWN; i < PIECE_COUNT; ++i) {
        Piece piece = static_cast<Piece>(i);

        if (a.getPieceBitboard(piece) != b.getPieceBitboard(piece)) {
            return false;
        }
    }

    return a.isWhiteToMove() == b.isWhiteToMove() &&
           a.getCastlingRights() == b.getCastlingRights() &&
           a.getEnPassantSquare() == b.getEnPassantSquare() &&
           a.getHalfmoveClock() == b.getHalfmoveClock() &&
           a.getFullmoveCounter() == b.getFullmoveCounter() && a.getHash() == b.getHash();
}

bool isLegal(Board& board, const Move& move) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i].toUCI() == move.toUCI()) {
            return true;
        }
    }

    return false;
}

struct SearchResult {
    std::optional<Move> bestMove;
    std::vector<Search::IterationInfo> iterations;
};

// A null table gives the search a table of its own, as for a search without a caller-owned one
SearchResult searchWith(Board& board, int depth, TranspositionTable* tt) {
    Search::Limits limits;
    limits.depth = depth;

    SearchResult result;
    result.bestMove = Search::findBestMove(
        board, limits, nullptr,
        [&result](const Search::IterationInfo& info) { result.iterations.push_back(info); }, tt);

    return result;
}

void expectSameSearch(const SearchResult& expected, const SearchResult& actual) {
    EXPECT_EQ(expected.iterations.size(), actual.iterations.size());

    for (std::size_t i = 0; i < expected.iterations.size() && i < actual.iterations.size(); ++i) {
        EXPECT_EQ(expected.iterations[i].score, actual.iterations[i].score);
        EXPECT_EQ(expected.iterations[i].nodes, actual.iterations[i].nodes);
        EXPECT_EQ(expected.iterations[i].ttHits, actual.iterations[i].ttHits);
        EXPECT_EQ(expected.iterations[i].ttCutoffs, actual.iterations[i].ttCutoffs);
        EXPECT_EQ(expected.iterations[i].bestMove.toUCI(), actual.iterations[i].bestMove.toUCI());
    }
}

void playMoves(Board& board, const std::vector<std::string>& moves) {
    for (const std::string& uci : moves) {
        MoveList legal;
        MoveGenerator::generateLegalMoves(board, legal);

        for (int i = 0; i < legal.count; ++i) {
            if (legal.moves[i].toUCI() == uci) {
                UndoState undo;
                board.makeMove(legal.moves[i], undo);
                break;
            }
        }
    }
}

} // namespace

TEST_CASE("Transposition table size is a power of two within budget") {
    TranspositionTable tt;

    EXPECT_TRUE(tt.size() > 0);
    EXPECT_EQ(0ULL, tt.size() & (tt.size() - 1));
    constexpr std::size_t budget = TranspositionTable::defaultSizeMb * 1024 * 1024;

    EXPECT_TRUE(tt.size() * sizeof(TranspositionTable::Entry) <= budget);
    EXPECT_TRUE(tt.size() * 2 * sizeof(TranspositionTable::Entry) > budget);
}

TEST_CASE("Transposition table misses on an empty table") {
    TranspositionTable tt(1);

    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(testKey, 0, -1000000, 1000000, 0, score, move));
    EXPECT_EQ(0, static_cast<int>(move.from));
    EXPECT_EQ(0, static_cast<int>(move.to));
    EXPECT_EQ(0ULL, tt.getHits());
    EXPECT_EQ(0ULL, tt.getCutoffs());
}

TEST_CASE("Transposition table stores and retrieves an entry") {
    TranspositionTable tt(1);
    tt.store(testKey, 4, 37, Bound::Exact, testMove, 0);

    int score = 0;
    Move move{};

    EXPECT_TRUE(tt.probe(testKey, 4, -1000000, 1000000, 0, score, move));
    EXPECT_EQ(37, score);
    EXPECT_EQ(std::string("e2e4"), move.toUCI());
    EXPECT_EQ(MoveType::DoublePawnPush, move.type);
    EXPECT_EQ(1ULL, tt.getHits());
    EXPECT_EQ(1ULL, tt.getCutoffs());
}

TEST_CASE("Transposition table rejects hash collisions") {
    TranspositionTable tt(1);

    // Same index, different full key
    const std::uint64_t collidingKey = testKey + tt.size();

    tt.store(testKey, 4, 37, Bound::Exact, testMove, 0);

    int score = 0;
    Move move{};

    EXPECT_FALSE(tt.probe(collidingKey, 0, -1000000, 1000000, 0, score, move));
    EXPECT_EQ(0, static_cast<int>(move.to));
    EXPECT_EQ(0ULL, tt.getHits());

    tt.store(collidingKey, 1, -12, Bound::Exact, Move{6, 21, MoveType::Quiet}, 0);

    EXPECT_FALSE(probeScore(tt, testKey, 0, -1000000, 1000000, score));
    EXPECT_TRUE(probeScore(tt, collidingKey, 0, -1000000, 1000000, score));
    EXPECT_EQ(-12, score);
}

TEST_CASE("Transposition table requires sufficient depth") {
    TranspositionTable tt(1);
    tt.store(testKey, 3, 37, Bound::Exact, testMove, 0);

    int score = 0;
    Move move{};

    // Too shallow for the score, but the move is still available for ordering
    EXPECT_FALSE(tt.probe(testKey, 4, -1000000, 1000000, 0, score, move));
    EXPECT_EQ(std::string("e2e4"), move.toUCI());
    EXPECT_EQ(1ULL, tt.getHits());
    EXPECT_EQ(0ULL, tt.getCutoffs());

    EXPECT_TRUE(probeScore(tt, testKey, 3, -1000000, 1000000, score));
    EXPECT_TRUE(probeScore(tt, testKey, 1, -1000000, 1000000, score));
    EXPECT_EQ(37, score);
}

TEST_CASE("Transposition table exact bound is usable in any window") {
    TranspositionTable tt(1);
    tt.store(testKey, 2, 50, Bound::Exact, testMove, 0);

    int score = 0;

    EXPECT_TRUE(probeScore(tt, testKey, 2, -1000000, 1000000, score));
    EXPECT_TRUE(probeScore(tt, testKey, 2, 60, 70, score));
    EXPECT_TRUE(probeScore(tt, testKey, 2, 10, 20, score));
    EXPECT_EQ(50, score);
}

TEST_CASE("Transposition table lower bound only cuts at or above beta") {
    TranspositionTable tt(1);
    tt.store(testKey, 2, 50, Bound::Lower, testMove, 0);

    int score = 0;

    EXPECT_TRUE(probeScore(tt, testKey, 2, 0, 50, score));
    EXPECT_EQ(50, score);
    EXPECT_TRUE(probeScore(tt, testKey, 2, 0, 40, score));
    EXPECT_FALSE(probeScore(tt, testKey, 2, 0, 51, score));
    EXPECT_FALSE(probeScore(tt, testKey, 2, -1000000, 1000000, score));
}

TEST_CASE("Transposition table upper bound only cuts at or below alpha") {
    TranspositionTable tt(1);
    tt.store(testKey, 2, 50, Bound::Upper, testMove, 0);

    int score = 0;

    EXPECT_TRUE(probeScore(tt, testKey, 2, 50, 100, score));
    EXPECT_EQ(50, score);
    EXPECT_TRUE(probeScore(tt, testKey, 2, 60, 100, score));
    EXPECT_FALSE(probeScore(tt, testKey, 2, 49, 100, score));
    EXPECT_FALSE(probeScore(tt, testKey, 2, -1000000, 1000000, score));
}

TEST_CASE("Transposition table keeps deeper results for the same position") {
    TranspositionTable tt(1);
    tt.store(testKey, 5, 10, Bound::Exact, testMove, 0);
    tt.store(testKey, 3, 20, Bound::Exact, testMove, 0);

    int score = 0;

    EXPECT_TRUE(probeScore(tt, testKey, 5, -1000000, 1000000, score));
    EXPECT_EQ(10, score);

    tt.store(testKey, 5, 30, Bound::Exact, testMove, 0);

    EXPECT_TRUE(probeScore(tt, testKey, 5, -1000000, 1000000, score));
    EXPECT_EQ(30, score);

    tt.store(testKey, 6, 40, Bound::Lower, testMove, 0);

    EXPECT_TRUE(probeScore(tt, testKey, 6, 0, 40, score));
    EXPECT_EQ(40, score);
}

TEST_CASE("Transposition table always replaces a different position") {
    TranspositionTable tt(1);
    const std::uint64_t collidingKey = testKey + tt.size();

    tt.store(testKey, 8, 10, Bound::Exact, testMove, 0);
    tt.store(collidingKey, 1, 20, Bound::Exact, testMove, 0);

    int score = 0;

    EXPECT_FALSE(probeScore(tt, testKey, 0, -1000000, 1000000, score));
    EXPECT_TRUE(probeScore(tt, collidingKey, 1, -1000000, 1000000, score));
    EXPECT_EQ(20, score);
}

TEST_CASE("Transposition table normalizes mate scores by ply") {
    TranspositionTable tt(1);

    // Mate found 5 plies from the root while storing at ply 3: mate is 2 plies from this node
    tt.store(testKey, 4, mateScore - 5, Bound::Exact, testMove, 3);

    int score = 0;

    EXPECT_TRUE(probeScore(tt, testKey, 4, -1000000, 1000000, score, 3));
    EXPECT_EQ(mateScore - 5, score);
    EXPECT_TRUE(probeScore(tt, testKey, 4, -1000000, 1000000, score, 7));
    EXPECT_EQ(mateScore - 9, score);
    EXPECT_TRUE(probeScore(tt, testKey, 4, -1000000, 1000000, score, 1));
    EXPECT_EQ(mateScore - 3, score);

    const std::uint64_t matedKey = testKey + 1;
    tt.store(matedKey, 4, -mateScore + 6, Bound::Upper, testMove, 2);

    EXPECT_TRUE(probeScore(tt, matedKey, 4, 0, 1000000, score, 5));
    EXPECT_EQ(-mateScore + 9, score);

    // Ordinary scores are never adjusted
    const std::uint64_t normalKey = testKey + 2;
    tt.store(normalKey, 4, 250, Bound::Exact, testMove, 3);

    EXPECT_TRUE(probeScore(tt, normalKey, 4, -1000000, 1000000, score, 9));
    EXPECT_EQ(250, score);
}

TEST_CASE("Negamax returns a usable table score without searching") {
    Board board;
    TranspositionTable tt(1);
    tt.store(board.getHash(), 3, 1234, Bound::Exact, Move{}, 0);

    std::uint64_t nodes = 0;
    std::uint64_t qNodes = 0;

    EXPECT_EQ(1234, Search::negamax(board, 3, 0, -1000000, 1000000, &nodes, &qNodes, &tt));
    EXPECT_EQ(1ULL, nodes);
    EXPECT_EQ(0ULL, qNodes);

    // A deeper request cannot use the depth 3 entry
    EXPECT_TRUE(Search::negamax(board, 4, 0, -1000000, 1000000, &nodes, &qNodes, &tt) != 1234);
}

TEST_CASE("Negamax with transposition table matches plain alpha-beta") {
    struct Scenario {
        const char* fen;
        int depth;
    };

    const Scenario scenarios[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
        {"r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPQBBPPP/R3K2R w KQkq - 0 1", 3},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
        {"4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1", 4},
        {"7k/8/5K2/8/8/8/8/R7 w - - 0 1", 4},
        {"8/8/8/8/8/2k5/8/K1q5 w - - 0 1", 4},
    };

    for (const auto& scenario : scenarios) {
        for (int depth = 1; depth <= scenario.depth; ++depth) {
            Board board;
            board.loadFEN(scenario.fen);

            TranspositionTable tt(1);

            int plainScore = Search::negamax(board, depth);
            int tableScore =
                Search::negamax(board, depth, 0, -1000000, 1000000, nullptr, nullptr, &tt);

            EXPECT_EQ(plainScore, tableScore);
        }
    }
}

TEST_CASE("Search reports consistent mate distance across iterations") {
    Board board;
    board.loadFEN("7k/8/5K2/8/8/8/8/R7 w - - 0 1");

    std::vector<int> scores;

    auto bestMove = Search::findBestMove(
        board, 5, [&scores](const Search::IterationInfo& info) { scores.push_back(info.score); });

    EXPECT_TRUE(bestMove.has_value());
    EXPECT_EQ(5ULL, scores.size());

    // Mate in 2 is 3 plies from the root once the search is deep enough to see it
    for (std::size_t i = 2; i < scores.size(); ++i) {
        EXPECT_EQ(mateScore - 3, scores[i]);
    }
}

TEST_CASE("Search with transposition table finds mate in one") {
    Board board;
    board.loadFEN("7k/8/5KQ1/8/8/8/8/8 w - - 0 1");

    int finalScore = 0;

    auto bestMove = Search::findBestMove(
        board, 4, [&finalScore](const Search::IterationInfo& info) { finalScore = info.score; });

    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_EQ(std::string("g6g7"), bestMove->toUCI());
    }

    EXPECT_EQ(mateScore - 1, finalScore);
}

TEST_CASE("Search reports transposition table activity") {
    Board board;

    Search::IterationInfo last{};

    Search::findBestMove(board, 4, [&last](const Search::IterationInfo& info) { last = info; });

    EXPECT_TRUE(last.ttHits > 0);
    EXPECT_TRUE(last.ttCutoffs > 0);
    EXPECT_TRUE(last.ttCutoffs <= last.ttHits);
}

TEST_CASE("Independent searches do not share transposition table state") {
    Board board;
    board.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPQBBPPP/R3K2R w KQkq - 0 1");

    std::vector<Search::IterationInfo> first;
    std::vector<Search::IterationInfo> second;

    auto firstMove = Search::findBestMove(
        board, 3, [&first](const Search::IterationInfo& info) { first.push_back(info); });
    auto secondMove = Search::findBestMove(
        board, 3, [&second](const Search::IterationInfo& info) { second.push_back(info); });

    EXPECT_TRUE(firstMove.has_value());
    EXPECT_TRUE(secondMove.has_value());
    EXPECT_EQ(first.size(), second.size());

    if (firstMove && secondMove) {
        EXPECT_EQ(firstMove->toUCI(), secondMove->toUCI());
    }

    for (std::size_t i = 0; i < first.size() && i < second.size(); ++i) {
        EXPECT_EQ(first[i].score, second[i].score);
        EXPECT_EQ(first[i].nodes, second[i].nodes);
        EXPECT_EQ(first[i].ttHits, second[i].ttHits);
        EXPECT_EQ(first[i].ttCutoffs, second[i].ttCutoffs);
    }
}

TEST_CASE("Search with transposition table restores complete board state") {
    Board board;
    board.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPQBBPPP/R3K2R w KQkq - 3 17");

    const Board original = board;

    TranspositionTable tt(1);
    Search::negamax(board, 3, 0, -1000000, 1000000, nullptr, nullptr, &tt);

    EXPECT_TRUE(boardsMatch(original, board));

    Search::findBestMove(board, 3);

    EXPECT_TRUE(boardsMatch(original, board));
}

TEST_CASE("Search stores a legal best move for the root") {
    Board board;
    TranspositionTable tt(1);

    Search::negamax(board, 3, 0, -1000000, 1000000, nullptr, nullptr, &tt);

    int score = 0;
    Move stored{};

    EXPECT_TRUE(tt.probe(board.getHash(), 3, -1000000, 1000000, 0, score, stored));

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    bool legal = false;

    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i].toUCI() == stored.toUCI()) {
            legal = true;
        }
    }

    EXPECT_TRUE(legal);
}

TEST_CASE("Transposition table resize keeps an unchanged size and clear empties the table") {
    TranspositionTable tt(1);
    const std::size_t oneMbSize = tt.size();
    int score = 0;

    tt.store(testKey, 4, 37, Bound::Exact, testMove, 0);
    tt.resize(1);
    EXPECT_EQ(oneMbSize, tt.size());
    EXPECT_TRUE(probeScore(tt, testKey, 4, -1000000, 1000000, score));

    tt.clear();
    EXPECT_EQ(oneMbSize, tt.size());
    EXPECT_EQ(0ULL, tt.getHits());
    EXPECT_FALSE(probeScore(tt, testKey, 0, -1000000, 1000000, score));

    tt.store(testKey, 4, 37, Bound::Exact, testMove, 0);
    tt.resize(2);
    EXPECT_EQ(oneMbSize * 2, tt.size());
    EXPECT_FALSE(probeScore(tt, testKey, 0, -1000000, 1000000, score));
    EXPECT_EQ(0ULL, tt.getHits());

    // Smaller than one entry still leaves a usable table
    tt.resize(0);
    EXPECT_EQ(1ULL, tt.size());
    tt.store(testKey, 4, 37, Bound::Exact, testMove, 0);
    EXPECT_TRUE(probeScore(tt, testKey, 4, -1000000, 1000000, score));
}

TEST_CASE("A caller-owned table searches like a search-owned one until it is reused") {
    Board board;
    board.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    const SearchResult cold = searchWith(board, 4, nullptr);

    TranspositionTable tt;
    expectSameSearch(cold, searchWith(board, 4, &tt));

    // The second search starts from the first one's entries
    const std::uint64_t hitsBefore = tt.getHits();
    const SearchResult warm = searchWith(board, 4, &tt);

    EXPECT_TRUE(warm.bestMove.has_value() && isLegal(board, *warm.bestMove));
    EXPECT_TRUE(warm.iterations.back().nodes < cold.iterations.back().nodes);
    // Depth 1 goes straight to quiescence, which does not use the table
    EXPECT_TRUE(warm.iterations[1].ttCutoffs > 0);

    // Statistics are counted per search, not over the table's lifetime
    EXPECT_EQ(tt.getHits() - hitsBefore, warm.iterations.back().ttHits);

    // A cleared table behaves like a new one, as after ucinewgame
    tt.clear();
    expectSameSearch(cold, searchWith(board, 4, &tt));
}

TEST_CASE("Consecutive searches of different positions return legal moves") {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    };

    TranspositionTable tt(1);

    for (const char* fen : fens) {
        Board board;
        board.loadFEN(fen);
        const Board original = board;

        const SearchResult result = searchWith(board, 4, &tt);

        EXPECT_TRUE(boardsMatch(original, board));
        EXPECT_TRUE(result.bestMove.has_value() && isLegal(board, *result.bestMove));
        EXPECT_EQ(4ULL, result.iterations.size());
    }
}

TEST_CASE("Mate distances stay correct when the table is reused after moves") {
    // 1. Kb6 Kb8 2. Rh8#
    Board board;
    board.loadFEN("k7/8/2K5/8/8/8/8/7R w - - 0 1");

    TranspositionTable tt(1);
    EXPECT_EQ(mateScore - 3, searchWith(board, 5, &tt).iterations.back().score);

    playMoves(board, {"c6b6", "a8b8"});

    const SearchResult warm = searchWith(board, 5, &tt);
    EXPECT_EQ(mateScore - 1, warm.iterations.back().score);
    EXPECT_EQ(std::string("h1h8"), warm.bestMove->toUCI());
}

TEST_CASE("Entries stored without a game history do not hide repetition or fifty-move draws") {
    // Without history the bare king is lost
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/q7/7K w - - 0 1");

    TranspositionTable tt(1);
    EXPECT_TRUE(searchWith(board, 4, &tt).iterations.back().score < -500);

    // After these moves g1h1 repeats a position for the third time
    playMoves(board,
              {"h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8"});

    const SearchResult repeated = searchWith(board, 4, &tt);
    EXPECT_EQ(0, repeated.iterations.back().score);
    EXPECT_EQ(std::string("g1h1"), repeated.bestMove->toUCI());

    // Every move reaches the fifty-move limit in a position the table scored as winning
    Board fresh;
    fresh.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 0 80");
    EXPECT_TRUE(searchWith(fresh, 4, &tt).iterations.back().score > 500);

    Board drawn;
    drawn.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 99 80");
    EXPECT_EQ(0, searchWith(drawn, 4, &tt).iterations.back().score);
}

TEST_CASE("Interrupted searches leave a table that later searches can use") {
    Board board;
    board.loadFEN("r1bqk2r/ppp2ppp/2n2n2/2bpp3/2BPP3/2N2N2/PPP2PPP/R1BQK2R w KQkq - 0 1");
    const Board original = board;

    TranspositionTable tt(1);
    const std::atomic<bool> stopRequested = true;
    bool allRestored = true;
    bool allLegal = true;

    for (std::uint64_t call = 1; call <= Search::stopCheckInterval; call += 97) {
        Search::Control control;
        control.stopRequested = &stopRequested;
        control.checkCounter = Search::stopCheckInterval - call;

        Search::negamax(board, 5, 0, -1000000, 1000000, nullptr, nullptr, &tt, nullptr, &control);

        const SearchResult next = searchWith(board, 3, &tt);

        allRestored = allRestored && boardsMatch(original, board);
        allLegal = allLegal && next.bestMove.has_value() && isLegal(board, *next.bestMove);
    }

    EXPECT_TRUE(allRestored);
    EXPECT_TRUE(allLegal);
}
