#include "MagicBitboards.h"
#include "TestUtils.h"
#include "BitUtils.h"
#include <cstdint>

namespace {

using Test::bitboard;
using Test::square;



TEST_CASE("Bishop magic lookup matches all generated attacks") {
    MagicBitboards::init();

    for (int square = 0; square < 64; square++) {
        std::uint64_t mask = MagicBitboards::maskBishopAttacks(square);
        int relevantBits = std::popcount(mask);
        int occupancyCount = 1 << relevantBits;

        for (int index = 0; index < occupancyCount; index++) {
            std::uint64_t occupancy =
                MagicBitboards::setOccupancy(index, relevantBits, mask);

            EXPECT_EQ(
                MagicBitboards::generateBishopAttacks(square, occupancy),
                MagicBitboards::getBishopAttacks(square, occupancy)
            );
        }
    }
}

TEST_CASE("Rook attacks on an empty board") {
    const std::uint64_t expected = bitboard({
        "e1", "e3", "e4", "e5", "e6", "e7", "e8",
        "a2", "b2", "c2", "d2", "f2", "g2", "h2"
    });

    EXPECT_EQ(expected, MagicBitboards::getRookAttacks(square("e2"), 0ULL));
}

TEST_CASE("Rook in a corner attacks its rank and file") {
    const std::uint64_t expected = bitboard({
        "a2", "a3", "a4", "a5", "a6", "a7", "a8",
        "b1", "c1", "d1", "e1", "f1", "g1", "h1"
    });

    EXPECT_EQ(expected, MagicBitboards::getRookAttacks(square("a1"), 0ULL));
}

TEST_CASE("Rook in the center attacks four empty rays") {
    const std::uint64_t expected = bitboard({
        "d1", "d2", "d3", "d5", "d6", "d7", "d8",
        "a4", "b4", "c4", "e4", "f4", "g4", "h4"
    });

    EXPECT_EQ(expected, MagicBitboards::getRookAttacks(square("d4"), 0ULL));
}

TEST_CASE("Rook stops at the first blocker in every direction") {
    const std::uint64_t occupied = bitboard({"d2", "d6", "b4", "f4"});
    const std::uint64_t expected = bitboard({
        "d2", "d3", "d5", "d6", "b4", "c4", "e4", "f4"
    });
    const std::uint64_t attacks =
        MagicBitboards::getRookAttacks(square("d4"), occupied);

    EXPECT_EQ(expected, attacks);
    EXPECT_TRUE(attacks & bitboard({"d2", "d6", "b4", "f4"}));
    EXPECT_FALSE(attacks & bitboard({"d1", "d7", "a4", "g4"}));
}

TEST_CASE("Adjacent blockers are attacked with no squares beyond them") {
    const std::uint64_t occupied = bitboard({"d3", "d5", "c4", "e4"});
    const std::uint64_t expected = occupied;

    EXPECT_EQ(expected,
              MagicBitboards::getRookAttacks(square("d4"), occupied));
}

TEST_CASE("Nearest of multiple blockers controls each ray") {
    const std::uint64_t occupied = bitboard({
        "d2", "d6", "d8", "b4", "a4", "f4", "h4"
    });
    const std::uint64_t expected = bitboard({
        "d2", "d3", "d5", "d6", "b4", "c4", "e4", "f4"
    });

    EXPECT_EQ(expected,
              MagicBitboards::getRookAttacks(square("d4"), occupied));
}

TEST_CASE("Edge blockers are included in otherwise complete rays") {
    const std::uint64_t occupied = bitboard({"d1", "d8", "a4", "h4"});
    const std::uint64_t expected = bitboard({
        "d1", "d2", "d3", "d5", "d6", "d7", "d8",
        "a4", "b4", "c4", "e4", "f4", "g4", "h4"
    });

    EXPECT_EQ(expected,
              MagicBitboards::getRookAttacks(square("d4"), occupied));
}

TEST_CASE("Corner rook includes blockers and excludes squares behind them") {
    const std::uint64_t occupied = bitboard({"a4", "d1"});
    const std::uint64_t expected = bitboard({
        "a2", "a3", "a4", "b1", "c1", "d1"
    });
    const std::uint64_t attacks =
        MagicBitboards::getRookAttacks(square("a1"), occupied);

    EXPECT_EQ(expected, attacks);
    EXPECT_TRUE(attacks & bitboard({"a4", "d1"}));
    EXPECT_FALSE(attacks & bitboard({"a5", "e1"}));
}

TEST_CASE("Bishop attacks on empty board") {
    int d4 = 27;

    std::uint64_t expected =
        (1ULL << 36) | // e5
        (1ULL << 45) | // f6
        (1ULL << 54) | // g7
        (1ULL << 63) | // h8
        (1ULL << 34) | // c5
        (1ULL << 41) | // b6
        (1ULL << 48) | // a7
        (1ULL << 20) | // e3
        (1ULL << 13) | // f2
        (1ULL << 6)  | // g1
        (1ULL << 18) | // c3
        (1ULL << 9)  | // b2
        (1ULL << 0);   // a1

    EXPECT_EQ(
        MagicBitboards::generateBishopAttacks(d4, 0ULL),
        expected
    );
}

TEST_CASE("Bishop stops at blocker") {
    int d4 = 27;
    int f6 = 45;

    std::uint64_t blockers = 1ULL << f6;

    std::uint64_t attacks =
        MagicBitboards::generateBishopAttacks(d4, blockers);

    EXPECT_TRUE(attacks & (1ULL << 36));  // e5
    EXPECT_TRUE(attacks & (1ULL << 45));  // f6 blocker included

    EXPECT_FALSE(attacks & (1ULL << 54)); // g7
    EXPECT_FALSE(attacks & (1ULL << 63)); // h8
}

TEST_CASE("Bishop relevant mask excludes board edges") {
    int d4 = 27;

    std::uint64_t mask =
        MagicBitboards::maskBishopAttacks(d4);

    EXPECT_EQ(9, BitUtils::popcount(mask));

    EXPECT_TRUE(mask & (1ULL << 36)); // e5
    EXPECT_TRUE(mask & (1ULL << 45)); // f6
    EXPECT_TRUE(mask & (1ULL << 54)); // g7

    EXPECT_FALSE(mask & (1ULL << 63)); // h8 edge
    EXPECT_FALSE(mask & (1ULL << 48)); // a7 edge
    EXPECT_FALSE(mask & (1ULL << 6));  // g1 edge
    EXPECT_FALSE(mask & (1ULL << 0));  // a1 edge
}

TEST_CASE("Bishop magic lookup matches generated attacks") {
    MagicBitboards::init();

    int d4 = 27;

    std::uint64_t occupied =
        (1ULL << 45) | // f6
        (1ULL << 18);  // c3

    EXPECT_EQ(
        MagicBitboards::generateBishopAttacks(d4, occupied),
        MagicBitboards::getBishopAttacks(d4, occupied)
    );
}
} // namespace
