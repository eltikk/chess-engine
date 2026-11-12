#include "Evaluation.h"
#include "MoveGenerator.h"
#include "Search.h"
#include "TestUtils.h"
#include <limits>

int negamaxWithoutPruning(Board& board, int depth, int ply = 0, std::uint64_t* nodes = nullptr) {
    if (nodes != nullptr) {
        ++(*nodes);
    }

    constexpr int mateScore = 100000;

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        if (MoveGenerator::isKingInCheck(board, board.isWhiteToMove())) {
            return -mateScore + ply;
        }
        return 0;
    }

    if (depth <= 0) {
        int score = Evaluation::evaluate(board);
        return board.isWhiteToMove() ? score : -score;
    }

    int bestScore = std::numeric_limits<int>::min();

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);
        int score = -negamaxWithoutPruning(board, depth - 1, ply + 1, nodes);
        board.unmakeMove(move, undo);

        if (score > bestScore) {
            bestScore = score;
        }
    }

    return bestScore;
}
TEST_CASE("Negamax recognizes checkmate") {
    Board board;
    board.loadFEN("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");

    EXPECT_EQ(-100000, Search::negamax(board, 0));
}

TEST_CASE("Negamax recognizes stalemate") {
    Board board;
    board.loadFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");

    EXPECT_EQ(0, Search::negamax(board, 0));
}

TEST_CASE("Negamax finds a free queen at depth 1") {
    Board board;
    board.loadFEN("4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1");

    int score = Search::negamax(board, 1);

    EXPECT_TRUE(score > 0);
}

TEST_CASE("Negamax restores board after searching") {
    Board board;
    Board original = board;

    Search::negamax(board, 2);

    EXPECT_EQ(Evaluation::evaluate(original), Evaluation::evaluate(board));
}

TEST_CASE("Search captures a free queen") {
    Board board;
    board.loadFEN("4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1");

    auto bestMove = Search::findBestMove(board, 1);

    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        UndoState undo;
        board.makeMove(*bestMove, undo);

        EXPECT_EQ(0ULL, board.getPieceBitboard(BLACK_QUEEN));

        board.unmakeMove(*bestMove, undo);
    }
}

TEST_CASE("Alpha-beta produces the same score as Negamax") {
    Board board;

    int originalScore = negamaxWithoutPruning(board, 3);
    int alphaBetaScore = Search::negamax(board, 3);

    EXPECT_EQ(originalScore, alphaBetaScore);
}

TEST_CASE("Alpha-beta searches fewer nodes") {
    Board board;

    constexpr int depth = 4;

    std::uint64_t originalNodes = 0;
    std::uint64_t alphaBetaNodes = 0;

    int originalScore = negamaxWithoutPruning(
        board, depth, 0, &originalNodes
    );

    int alphaBetaScore = Search::negamax(
        board, depth, 0, -1000000, 1000000, &alphaBetaNodes
    );

    EXPECT_EQ(originalScore, alphaBetaScore);
    EXPECT_TRUE(alphaBetaNodes <= originalNodes);

    double reduction = 100.0 * (originalNodes - alphaBetaNodes) / originalNodes;

    std::cout << "\nSearch depth: " << depth << '\n';
    std::cout << "Original Negamax nodes: " << originalNodes << '\n';
    std::cout << "Alpha-Beta nodes: " << alphaBetaNodes << '\n';
    std::cout << "Nodes eliminated: " << originalNodes - alphaBetaNodes << '\n';
    std::cout << "Reduction: " << reduction << "%\n";
}

TEST_CASE("Alpha-beta matches Negamax in tactical position") {
    Board board;
    board.loadFEN("4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1");

    constexpr int depth = 4;

    std::uint64_t originalNodes = 0;
    std::uint64_t alphaBetaNodes = 0;

    int originalScore = negamaxWithoutPruning(
        board, depth, 0, &originalNodes
    );

    int alphaBetaScore = Search::negamax(
        board, depth, 0, -1000000, 1000000, &alphaBetaNodes
    );

    EXPECT_EQ(originalScore, alphaBetaScore);
    EXPECT_TRUE(alphaBetaNodes <= originalNodes);

    double reduction = 100.0 *
        (originalNodes - alphaBetaNodes) / originalNodes;

    std::cout << "\nTactical position (depth " << depth << ")\n";
    std::cout << "Original nodes: " << originalNodes << '\n';
    std::cout << "Alpha-beta nodes: " << alphaBetaNodes << '\n';
    std::cout << "Reduction: " << reduction << "%\n";
}

TEST_CASE("Alpha-beta restores board after searching") {
    Board board;
    Board original = board;

    Search::negamax(board, 3);

    for (int i = WHITE_PAWN; i < PIECE_COUNT; ++i) {
        Piece piece = static_cast<Piece>(i);

        EXPECT_EQ(
            original.getPieceBitboard(piece),
            board.getPieceBitboard(piece)
        );
    }

    EXPECT_EQ(original.isWhiteToMove(), board.isWhiteToMove());
}

TEST_CASE("Search prefers capturing rook over pawn") {
    Board board;
    board.loadFEN("k7/5r2/8/1p6/2B5/8/8/4K3 w - - 0 1");

    auto bestMove = Search::findBestMove(board, 1);

    EXPECT_TRUE(bestMove.has_value());

    if (bestMove) {
        EXPECT_EQ(std::string("c4f7"), bestMove->toUCI());
    }
}