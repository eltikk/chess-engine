#include "BitUtils.h"
#include "Board.h"
#include "TestUtils.h"

TEST_CASE("Board starts in the standard chess position") {
    const Board board;

    EXPECT_TRUE(board.isWhiteToMove());
    EXPECT_EQ(32, BitUtils::popcount(board.getOccupied()));
    EXPECT_EQ('R', board.getPieceAt(Test::square("a1")));
    EXPECT_EQ('k', board.getPieceAt(Test::square("e8")));
    EXPECT_EQ('.', board.getPieceAt(Test::square("e4")));
}

TEST_CASE("Board loads basic FEN state") {
    Board board;
    board.loadFEN("8/8/8/3k4/8/4K3/8/8 b - - 7 42");

    EXPECT_FALSE(board.isWhiteToMove());
    EXPECT_EQ('k', board.getPieceAt(Test::square("d5")));
    EXPECT_EQ('K', board.getPieceAt(Test::square("e3")));
    EXPECT_EQ(7, board.getHalfmoveClock());
    EXPECT_EQ(42, board.getFullmoveCounter());
}
