#include "Evaluation.h"
#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include "TranspositionTable.h"
#include "Zobrist.h"
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr int infinity = 1000000;
constexpr int mateScore = 100000;
constexpr int mateThreshold = mateScore - 1000;

const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

// Without hanging pieces, so passing keeps the static eval
const char* quiet = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8";

// White to move with a legal en passant capture, so the en passant square is part of the hash
const char* enPassant = "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";

// Rh8 mates at once, and still mates after a pass, since Kb8 is Black's only move. The knight
// gives White the two pieces a null move needs.
const char* mateAfterPass = "k7/8/1K6/8/8/8/8/N6R w - - 0 1";

// After 1.Rf1 the g1 rook is pinned and Black, with no spare piece moves, is in zugzwang
const char* rookZugzwang = "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1";

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

// Plays moves given in UCI notation, which must be legal
void playMoves(Board& board, std::initializer_list<const char*> moves) {
    for (const char* uci : moves) {
        MoveList legal;
        MoveGenerator::generateLegalMoves(board, legal);
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

int staticEval(const Board& board) {
    const int eval = Evaluation::evaluate(board);
    return board.isWhiteToMove() ? eval : -eval;
}

Search::Control pruningControl() {
    Search::Control control;
    control.nullMovePruning = true;
    return control;
}

// Null move tries at the node itself: at the minimum depth, its children are too shallow to try one
std::uint64_t rootNullMoveTries(Board& board, int alpha, int beta,
                                int depth = Search::nullMoveMinDepth, bool afterNullMove = false) {
    Search::Control control = pruningControl();
    Search::negamax(board, depth, 0, alpha, beta, nullptr, nullptr, nullptr, nullptr, &control,
                    nullptr, nullptr, afterNullMove);
    return control.nullMoveTries;
}

// The same, with a null window well below the static eval
std::uint64_t triesBelowEval(const char* fen) {
    Board board;
    board.loadFEN(fen);
    const int beta = staticEval(board) - 200;
    return rootNullMoveTries(board, beta - 1, beta);
}

std::vector<Search::IterationInfo> iterate(Board& board, int depth, bool nullMovePruning) {
    std::vector<Search::IterationInfo> iterations;
    Search::Limits limits;
    limits.depth = depth;
    limits.nullMovePruning = nullMovePruning;
    Search::findBestMove(board, limits, nullptr, [&iterations](const Search::IterationInfo& info) {
        iterations.push_back(info);
    });
    return iterations;
}

} // namespace

TEST_CASE("A null move passes the turn and is undone exactly") {
    // A move first gives the board history; the clock and move counter differ between the two
    for (const char* fen :
         {kiwipete, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 7 30"}) {
        Board board;
        board.loadFEN(fen);
        playMoves(board, {board.isWhiteToMove() ? "e1d1" : "e8d8"});
        const Board original = board;

        UndoState undo;
        board.makeNullMove(undo);

        EXPECT_TRUE(board.isWhiteToMove() != original.isWhiteToMove());
        EXPECT_EQ(Zobrist::compute(board), board.getHash());
        EXPECT_EQ(original.getOccupied(), board.getOccupied());
        EXPECT_EQ(original.getCastlingRights(), board.getCastlingRights());
        EXPECT_EQ(-1, board.getEnPassantSquare());
        EXPECT_EQ(0, board.getHalfmoveClock());
        EXPECT_EQ(original.getFullmoveCounter() + (original.isWhiteToMove() ? 0 : 1),
                  board.getFullmoveCounter());
        EXPECT_EQ(original.getHistory().size() + 1, board.getHistory().size());

        board.unmakeNullMove(undo);

        EXPECT_TRUE(sameBoardState(original, board));
    }
}

TEST_CASE("A null move clears the en passant square and restores it") {
    Board board;
    board.loadFEN(enPassant);
    const Board original = board;

    UndoState undo;
    board.makeNullMove(undo);

    // The same position with Black to move and no en passant square
    Board expected;
    expected.loadFEN("4k3/8/8/3pP3/8/8/8/4K3 b - - 0 1");

    EXPECT_EQ(-1, board.getEnPassantSquare());
    EXPECT_EQ(expected.getHash(), board.getHash());

    board.unmakeNullMove(undo);

    EXPECT_EQ(original.getEnPassantSquare(), board.getEnPassantSquare());
    EXPECT_TRUE(sameBoardState(original, board));

    // The capture the en passant square allows is still legal after undoing
    playMoves(board, {"e5d6"});
}

TEST_CASE("Null moves cannot create repetition or fifty-move draws") {
    // White passes twice while Black's king steps away and back, reaching the starting position
    // again with White to move. No move by White repeated it, so it is not a repetition.
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
    const std::uint64_t start = board.getHash();

    UndoState first;
    UndoState second;
    board.makeNullMove(first);
    playMoves(board, {"e8d8"});
    board.makeNullMove(second);
    playMoves(board, {"d8e8"});

    // Searched from a root before these moves, one earlier occurrence would count
    constexpr int searchPly = 5;

    EXPECT_EQ(start, board.getHash());
    EXPECT_EQ(0, board.repetitionDistance(searchPly));

    // With real moves, the same king walk does repeat the position
    Board played;
    played.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
    playMoves(played, {"a1a2", "e8d8", "a2a1", "d8e8"});
    EXPECT_EQ(4, played.repetitionDistance(searchPly));

    // A pass one ply before the fifty-move limit does not reach it
    Board clock;
    clock.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 99 80");
    UndoState undo;
    clock.makeNullMove(undo);
    EXPECT_FALSE(clock.isFiftyMoveRule());
    playMoves(clock, {"e8d8"});
    EXPECT_EQ(1, clock.getHalfmoveClock());
}

TEST_CASE("A null move that still reaches beta prunes the node") {
    Board board;
    board.loadFEN(quiet);
    const Board original = board;

    const int beta = staticEval(board) - 200;

    Search::Control control = pruningControl();
    std::uint64_t nodes = 0;
    std::uint64_t qNodes = 0;
    const int score =
        Search::negamax(board, 3, 0, beta - 1, beta, &nodes, &qNodes, nullptr, nullptr, &control);

    // The depth-0 null search runs in quiescence, so no move of the node is searched
    EXPECT_TRUE(score >= beta);
    EXPECT_EQ(std::uint64_t{1}, control.nullMoveTries);
    EXPECT_EQ(std::uint64_t{1}, control.nullMoveCutoffs);
    EXPECT_EQ(std::uint64_t{1}, nodes);
    EXPECT_TRUE(sameBoardState(original, board));

    // Without pruning, the node searches its moves
    std::uint64_t exactNodes = 0;
    Search::negamax(board, 3, 0, beta - 1, beta, &exactNodes);
    EXPECT_TRUE(exactNodes > nodes);
}

TEST_CASE("Null moves are only tried where passing is a safe bound") {
    Board board;
    board.loadFEN(quiet);
    const int eval = staticEval(board);
    const int beta = eval - 200;

    // A null window at or below the static eval, with enough depth, tries one
    EXPECT_EQ(std::uint64_t{1}, rootNullMoveTries(board, beta - 1, beta));
    EXPECT_EQ(std::uint64_t{1}, rootNullMoveTries(board, eval - 1, eval));

    // PV nodes, shallow nodes, replies to a null move, and a static eval below beta
    EXPECT_EQ(std::uint64_t{0}, rootNullMoveTries(board, beta - 2, beta));
    EXPECT_EQ(std::uint64_t{0},
              rootNullMoveTries(board, beta - 1, beta, Search::nullMoveMinDepth - 1));
    EXPECT_EQ(std::uint64_t{0},
              rootNullMoveTries(board, beta - 1, beta, Search::nullMoveMinDepth, true));
    EXPECT_EQ(std::uint64_t{0}, rootNullMoveTries(board, eval, eval + 1));

    // Mate bounds, which a pass cannot prove
    EXPECT_EQ(std::uint64_t{0}, rootNullMoveTries(board, mateThreshold - 1, mateThreshold));
    EXPECT_EQ(std::uint64_t{0}, rootNullMoveTries(board, -mateThreshold - 1, -mateThreshold));

    // Without control, or with pruning disabled in it
    Search::Control disabled;
    Search::negamax(board, 3, 0, beta - 1, beta, nullptr, nullptr, nullptr, nullptr, &disabled);
    EXPECT_EQ(std::uint64_t{0}, disabled.nullMoveTries);

    // Near the fifty-move limit, where the null move's restarted clock would hide draws
    EXPECT_EQ(
        std::uint64_t{1},
        triesBelowEval("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 96 60"));
    EXPECT_EQ(
        std::uint64_t{0},
        triesBelowEval("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 97 60"));

    // In check, where a pass would be illegal
    EXPECT_EQ(std::uint64_t{1}, triesBelowEval("4k3/8/8/8/8/8/q7/RN2K3 w - - 0 1"));
    EXPECT_EQ(std::uint64_t{0}, triesBelowEval("4k3/8/8/8/8/8/4q3/RN2K3 w - - 0 1"));

    // With fewer than two pieces for the side to move, whatever the opponent has
    EXPECT_EQ(std::uint64_t{1}, triesBelowEval("4k3/8/8/8/8/8/PPP5/3NK2R w - - 0 1"));
    EXPECT_EQ(std::uint64_t{0}, triesBelowEval("3qk3/8/8/8/8/8/PPP5/4K2R w - - 0 1"));
    EXPECT_EQ(std::uint64_t{0}, triesBelowEval("3qk3/8/8/8/8/8/PPP5/4K3 w - - 0 1"));
}

TEST_CASE("Stalemate is scored before a null move is considered") {
    // White's bishop and knight are pinned and its king is boxed in: two pieces, not in check,
    // and no legal move, where a pass would hide the stalemate
    Board board;
    board.loadFEN("r7/8/8/8/8/2k5/B7/KN5r w - - 0 1");

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);
    EXPECT_EQ(0, moves.count);
    EXPECT_FALSE(MoveGenerator::isKingInCheck(board, true));

    const int beta = staticEval(board) - 200;
    Search::Control control = pruningControl();
    const int score =
        Search::negamax(board, 3, 0, beta - 1, beta, nullptr, nullptr, nullptr, nullptr, &control);

    EXPECT_EQ(0, score);
    EXPECT_EQ(std::uint64_t{0}, control.nullMoveTries);
}

TEST_CASE("Endgames prone to zugzwang are searched without null moves") {
    struct Ending {
        const char* fen;
        int depth;
        const char* bestMove;
    };

    // Fine 70 needs a zugzwang-based king walk, the opposition decides the king and pawn endings,
    // and Rf1 wins only through zugzwang. With a null move for Black, Rf1 was found five plies
    // later.
    const Ending endings[] = {
        {"8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1", 14, nullptr},
        {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", 11, nullptr},
        {"8/8/8/3k4/8/3K4/3P4/8 w - - 0 1", 11, nullptr},
        {rookZugzwang, 8, "e1f1"},
    };

    for (const Ending& ending : endings) {
        Board board;
        board.loadFEN(ending.fen);

        const auto pruned = iterate(board, ending.depth, true);
        const auto exact = iterate(board, ending.depth, false);

        EXPECT_EQ(exact.size(), pruned.size());

        for (std::size_t i = 0; i < pruned.size() && i < exact.size(); ++i) {
            EXPECT_EQ(std::uint64_t{0}, pruned[i].nullMoveTries);
            EXPECT_EQ(exact[i].score, pruned[i].score);
            EXPECT_EQ(exact[i].nodes, pruned[i].nodes);
            EXPECT_EQ(exact[i].bestMove.toUCI(), pruned[i].bestMove.toUCI());
        }

        if (ending.bestMove != nullptr && !pruned.empty()) {
            EXPECT_EQ(std::string(ending.bestMove), pruned.back().bestMove.toUCI());
        }
    }
}

TEST_CASE("Null move pruning keeps mate scores and distances") {
    struct MateScenario {
        const char* fen;
        int depth;
        int expected;
    };

    const MateScenario mates[] = {
        {mateAfterPass, 6, mateScore - 1},
        {"7k/8/5K2/8/8/8/8/R7 w - - 0 1", 6, mateScore - 3},
        {"1k1r4/pp1b1R2/6pp/4p3/2B5/4Q3/PPP2B2/2Kq4 w - - 1 2", 6, -(mateScore - 4)},
        {"2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1", 6, mateScore - 3},
    };

    for (const MateScenario& mate : mates) {
        Board board;
        board.loadFEN(mate.fen);

        const auto iterations = iterate(board, mate.depth, true);
        EXPECT_EQ(static_cast<std::size_t>(mate.depth), iterations.size());

        if (!iterations.empty()) {
            EXPECT_EQ(mate.expected, iterations.back().score);
        }
    }
}

TEST_CASE("A mate found after passing only proves beta and is not stored") {
    Board board;
    board.loadFEN(mateAfterPass);

    constexpr int depth = 5;
    constexpr int beta = 100;
    constexpr int nullDepth = depth - 1 - Search::nullMoveReduction;

    // A null window stops at the first move reaching beta, so a mate only comes back from the null
    // move search once the table holds it: here, from a search of the position after the pass
    TranspositionTable tt(1);
    UndoState undo;
    board.makeNullMove(undo);
    Search::negamax(board, nullDepth, 1, -infinity, infinity, nullptr, nullptr, &tt);
    const int nullScore =
        -Search::negamax(board, nullDepth, 1, -beta, -beta + 1, nullptr, nullptr, &tt);
    board.unmakeNullMove(undo);

    EXPECT_TRUE(nullScore >= mateThreshold);

    Search::Control control = pruningControl();
    const int score =
        Search::negamax(board, depth, 0, beta - 1, beta, nullptr, nullptr, &tt, nullptr, &control);

    // The node keeps beta rather than the mate score
    EXPECT_EQ(beta, score);
    EXPECT_EQ(std::uint64_t{1}, control.nullMoveCutoffs);

    // The cutoff leaves no entry for the node
    const std::uint64_t hitsBefore = tt.getHits();
    int stored = 0;
    Move move{};
    tt.probe(board.getHash(), 0, -infinity, infinity, 0, stored, move);
    EXPECT_EQ(hitsBefore, tt.getHits());

    // The full-window search still finds the mate
    EXPECT_EQ(mateScore - 1, Search::negamax(board, depth));
}

TEST_CASE("A null move cutoff leaves the node's table entry unchanged") {
    Board board;
    board.loadFEN(quiet);

    // An entry too shallow to answer the deeper search below, which then tries a null move
    TranspositionTable tt(1);
    const int shallow = Search::negamax(board, 2, 0, -infinity, infinity, nullptr, nullptr, &tt);

    int score = 0;
    Move storedMove{};
    EXPECT_TRUE(tt.probe(board.getHash(), 2, -infinity, infinity, 0, score, storedMove));

    const int beta = staticEval(board) - 200;
    Search::Control control = pruningControl();
    Search::negamax(board, 3, 0, beta - 1, beta, nullptr, nullptr, &tt, nullptr, &control);
    EXPECT_EQ(std::uint64_t{1}, control.nullMoveCutoffs);

    // Neither a deeper bound nor a different move replaced it
    Move move{};
    EXPECT_FALSE(tt.probe(board.getHash(), 3, beta - 1, beta, 0, score, move));
    EXPECT_TRUE(tt.probe(board.getHash(), 2, -infinity, infinity, 0, score, move));
    EXPECT_EQ(shallow, score);
    EXPECT_EQ(storedMove.toUCI(), move.toUCI());
}

TEST_CASE("Null move pruning keeps repetition and fifty-move draws") {
    // Losing, White draws by repeating with g1h1
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/q7/7K w - - 0 1");
    playMoves(board,
              {"h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8", "g1h1", "d8e8", "h1g1", "e8d8"});

    for (const Search::IterationInfo& info : iterate(board, 6, true)) {
        EXPECT_EQ(0, info.score);
        EXPECT_EQ(std::string("g1h1"), info.bestMove.toUCI());
    }

    // From the fifth ply every position is a fifty-move draw, so from depth 5 no null move is
    // tried and the scores are exact
    Board cliff;
    cliff.loadFEN("7k/8/8/8/8/8/1Q6/K7 w - - 95 80");

    for (int depth = 5; depth <= 7; ++depth) {
        Search::Control control = pruningControl();
        const int score = Search::negamax(cliff, depth, 0, -infinity, infinity, nullptr, nullptr,
                                          nullptr, nullptr, &control);

        EXPECT_EQ(0, score);
        EXPECT_EQ(std::uint64_t{0}, control.nullMoveTries);
    }

    // Resetting the clock is still found
    Board reset;
    reset.loadFEN("7k/8/8/8/8/8/1Q4p1/K7 w - - 99 80");
    const auto iterations = iterate(reset, 5, true);

    if (!iterations.empty()) {
        EXPECT_EQ(std::string("b2g2"), iterations.back().bestMove.toUCI());
        EXPECT_TRUE(iterations.back().score > 500);
    }
}

TEST_CASE("Interrupting null move searches restores the board") {
    Board board;
    board.loadFEN("8/5k2/3n4/8/2R5/4KN2/4P3/6r1 w - - 0 1");
    const Board original = board;

    constexpr int depth = 5;
    const int beta = staticEval(board) - 50;

    // Every node reads the stop condition once, so an uninterrupted search counts the nodes
    TranspositionTable sizingTable(1);
    Search::Control sizing = pruningControl();
    const int complete = Search::negamax(board, depth, 0, beta - 1, beta, nullptr, nullptr,
                                         &sizingTable, nullptr, &sizing);
    const std::uint64_t calls = sizing.checkCounter;

    // The sweep reaches every node, including those of null move searches that fail low
    EXPECT_TRUE(sizing.nullMoveTries > sizing.nullMoveCutoffs);
    EXPECT_TRUE(sizing.nullMoveCutoffs > 0);
    EXPECT_TRUE(calls < Search::stopCheckInterval);

    const std::atomic<bool> stopRequested = true;
    bool allStopped = true;
    bool allRestored = true;

    for (std::uint64_t call = 1; call <= calls; ++call) {
        TranspositionTable tt(1);
        Search::Control control = pruningControl();
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
    Search::Control late = pruningControl();
    late.stopRequested = &stopRequested;
    late.checkCounter = Search::stopCheckInterval - (calls + 1);
    const int score =
        Search::negamax(board, depth, 0, beta - 1, beta, nullptr, nullptr, &tt, nullptr, &late);

    EXPECT_FALSE(late.stopped);
    EXPECT_EQ(complete, score);
    EXPECT_EQ(sizing.nullMoveTries, late.nullMoveTries);
    EXPECT_EQ(sizing.nullMoveCutoffs, late.nullMoveCutoffs);
}

TEST_CASE("Iterative deepening reports null move counts and can disable pruning") {
    Board board;
    board.loadFEN(kiwipete);

    const auto pruned = iterate(board, 5, true);
    const auto exact = iterate(board, 5, false);

    EXPECT_EQ(std::size_t{5}, pruned.size());
    EXPECT_EQ(std::size_t{5}, exact.size());

    if (pruned.size() == 5 && exact.size() == 5) {
        EXPECT_TRUE(pruned.back().nullMoveCutoffs > 0);
        EXPECT_TRUE(pruned.back().nullMoveTries >= pruned.back().nullMoveCutoffs);
        EXPECT_TRUE(pruned.back().nodes < exact.back().nodes);

        // Disabled pruning reproduces the full-window score
        EXPECT_EQ(std::uint64_t{0}, exact.back().nullMoveTries);
        EXPECT_EQ(Search::negamax(board, 5), exact.back().score);
    }
}
