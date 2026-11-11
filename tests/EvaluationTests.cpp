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

TEST_CASE("Black knight evaluation includes positional penalty") {
    Board board;
    board.loadFEN("1n2k3/8/8/8/8/8/8/4K3 w - - 0 1");

    EXPECT_EQ(-280, Evaluation::evaluate(board));
}

TEST_CASE("White queen evaluation includes positional bonus") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");

    EXPECT_EQ(895, Evaluation::evaluate(board));
}

TEST_CASE("Material evaluation is independent of side to move") {
    Board board;

    board.loadFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
    EXPECT_EQ(100, Evaluation::evaluate(board));

    board.loadFEN("4k3/8/8/8/8/8/P7/4K3 b - - 0 1");
    EXPECT_EQ(100, Evaluation::evaluate(board));
}

TEST_CASE("White knight receives positional bonus") {
    Board board;
    board.loadFEN("4k3/8/8/8/3N4/8/8/4K3 w - - 0 1");

    EXPECT_EQ(340, Evaluation::evaluate(board));
}

TEST_CASE("Black knight receives mirrored positional bonus") {
    Board board;
    board.loadFEN("4k3/8/8/3n4/8/8/8/4K3 w - - 0 1");

    EXPECT_EQ(-340, Evaluation::evaluate(board));
}

TEST_CASE("Symmetrical knight positions cancel out") {
    Board board;
    board.loadFEN("4k3/8/8/3n4/3N4/8/8/4K3 w - - 0 1");

    EXPECT_EQ(0, Evaluation::evaluate(board));
}

TEST_CASE("Bishop PST handles both colors") {
    Board board;

    // White bishop on c4
    board.loadFEN("4k3/8/8/8/2B5/8/8/4K3 w - - 0 1");
    EXPECT_EQ(340, Evaluation::evaluate(board));

    // Black bishop on c5
    board.loadFEN("4k3/8/8/2b5/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(-340, Evaluation::evaluate(board));
}

TEST_CASE("Rook PST handles both colors") {
    Board board;

    // White rook on d7
    board.loadFEN("4k3/3R4/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(510, Evaluation::evaluate(board));

    // Black rook on d2
    board.loadFEN("4k3/8/8/8/8/8/3r4/4K3 w - - 0 1");
    EXPECT_EQ(-510, Evaluation::evaluate(board));
}

TEST_CASE("King PST rewards safer middlegame squares") {
    Board board;

    // White king on g1, Black king on e8
    board.loadFEN("4k3/8/8/8/8/8/8/6K1 w - - 0 1");

    EXPECT_EQ(30, Evaluation::evaluate(board));
}