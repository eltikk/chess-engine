#include "Search.h"
#include "TestUtils.h"

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