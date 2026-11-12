#include "Search.h"
#include "TestUtils.h"
#include "Evaluation.h" 

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