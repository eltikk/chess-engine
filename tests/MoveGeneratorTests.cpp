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

TEST_CASE("Queen move generation respects blockers") {
    Board board;
    board.loadFEN("8/8/3p1p2/8/3Q4/2P5/3P4/8 w - - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateQueenMoves(board, moves);

    EXPECT_TRUE(moves.count > 0);
}

TEST_CASE("White pawn attacks square") {
    Board board;
    board.loadFEN("8/8/8/8/8/2P5/8/8 w - - 0 1"); // white pawn c3

    EXPECT_TRUE(
        MoveGenerator::isSquareAttacked(board, 27, true) // d4
    );
}

TEST_CASE("Knight attacks square") {
    Board board;
    board.loadFEN("8/8/8/5N2/8/8/8/8 w - - 0 1"); // knight f5

    EXPECT_TRUE(
        MoveGenerator::isSquareAttacked(board, 27, true)
    );
}

TEST_CASE("Bishop attacks square") {
    Board board;
    board.loadFEN("8/8/8/8/8/8/1B6/8 w - - 0 1"); // bishop b2

    EXPECT_TRUE(
        MoveGenerator::isSquareAttacked(board, 27, true)
    );
}

TEST_CASE("Rook attacks square") {
    Board board;
    board.loadFEN("3R4/8/8/8/8/8/8/8 w - - 0 1"); // rook d8

    EXPECT_TRUE(
        MoveGenerator::isSquareAttacked(board, 27, true)
    );
}

TEST_CASE("Queen attacks square") {
    Board board;
    board.loadFEN("8/8/8/8/Q7/8/8/8 w - - 0 1"); // queen a4

    EXPECT_TRUE(
        MoveGenerator::isSquareAttacked(board, 27, true)
    );
}

TEST_CASE("King attacks square") {
    Board board;
    board.loadFEN("8/8/8/8/8/2K5/8/8 w - - 0 1"); // king c3

    EXPECT_TRUE(
        MoveGenerator::isSquareAttacked(board, 27, true)
    );
}

TEST_CASE("Square is not attacked") {
    Board board;
    board.loadFEN("8/8/8/8/8/8/8/8 w - - 0 1");

    EXPECT_FALSE(
        MoveGenerator::isSquareAttacked(board, 27, true)
    );
}

TEST_CASE("White king is in check") {
    Board board;
    board.loadFEN("4r3/8/8/8/8/8/8/4K3 w - - 0 1");

    EXPECT_TRUE(
        MoveGenerator::isKingInCheck(board, true)
    );
}

TEST_CASE("White king is not in check") {
    Board board;
    board.loadFEN("8/8/8/8/8/8/8/4K3 w - - 0 1");

    EXPECT_FALSE(
        MoveGenerator::isKingInCheck(board, true)
    );
}