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
