#include "Evaluation.h"
#include "Board.h"
#include "TestUtils.h"

TEST_CASE("Starting position has equal material") {
    Board board;

    EXPECT_EQ(0, Evaluation::evaluate(board));
}

TEST_CASE("White has an extra pawn") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");

    EXPECT_EQ(100, Evaluation::evaluate(board));
}

TEST_CASE("Black has an extra knight") {
    Board board;
    board.loadFEN("1n2k3/8/8/8/8/8/8/4K3 w - - 0 1");

    EXPECT_EQ(-320, Evaluation::evaluate(board));
}

TEST_CASE("White has an extra queen") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");

    EXPECT_EQ(900, Evaluation::evaluate(board));
}

TEST_CASE("Material evaluation is independent of side to move") {
    Board board;

    board.loadFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
    EXPECT_EQ(100, Evaluation::evaluate(board));

    board.loadFEN("4k3/8/8/8/8/8/P7/4K3 b - - 0 1");
    EXPECT_EQ(100, Evaluation::evaluate(board));
}