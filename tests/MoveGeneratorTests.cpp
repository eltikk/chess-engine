#include "MoveGenerator.h"
#include "TestUtils.h"

TEST_CASE("Knight attack table handles a corner") {
    EXPECT_EQ(Test::bitboard({"b3", "c2"}),
              MoveGenerator::getKnightAttacks(Test::square("a1")));
}

TEST_CASE("King attack table handles a corner") {
    EXPECT_EQ(Test::bitboard({"a2", "b1", "b2"}),
              MoveGenerator::getKingAttacks(Test::square("a1")));
}

TEST_CASE("White pawn attack table handles file edges") {
    EXPECT_EQ(Test::bitboard({"b3"}),
              MoveGenerator::getPawnAttacks(Test::square("a2"), true));
    EXPECT_EQ(Test::bitboard({"g3"}),
              MoveGenerator::getPawnAttacks(Test::square("h2"), true));
}

TEST_CASE("Bishop move generation respects blockers") {
    Board board;

    // Use whatever FEN-loading function you already use in BoardTests
    board.loadFEN("8/8/1p3p2/8/3B4/2P5/8/8 w - - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateBishopMoves(board, moves);

    EXPECT_EQ(7, moves.count);
}

TEST_CASE("Rook move generation respects blockers") {
    Board board;
    board.loadFEN("8/8/3p4/8/3R1p2/8/3P4/8 w - - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateRookMoves(board, moves);

    EXPECT_EQ(8, moves.count);
}
