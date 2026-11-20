#include "BitUtils.h"
#include "MagicBitboards.h"
#include "TestUtils.h"

#include <cstdint>
#include <iostream>
#include <random>

namespace {

using Test::bitboard;
using Test::square;

enum class Slider { Rook, Bishop };

struct Direction {
    int rankStep;
    int fileStep;
};

constexpr Direction rookDirections[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr Direction bishopDirections[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

const Direction* directionsOf(Slider slider) {
    return slider == Slider::Rook ? rookDirections : bishopDirections;
}

bool onBoard(int rank, int file) {
    return rank >= 0 && rank < 8 && file >= 0 && file < 8;
}

// Independent reference: walks each ray one square at a time, including the first occupied square
// and stopping there, or stopping at the edge of the board
std::uint64_t referenceAttacks(Slider slider, int square, std::uint64_t occupied) {
    std::uint64_t attacks = 0ULL;

    for (int i = 0; i < 4; i++) {
        const Direction direction = directionsOf(slider)[i];
        int rank = square / 8 + direction.rankStep;
        int file = square % 8 + direction.fileStep;

        while (onBoard(rank, file)) {
            const std::uint64_t target = 1ULL << (rank * 8 + file);
            attacks |= target;

            if (occupied & target) {
                break;
            }

            rank += direction.rankStep;
            file += direction.fileStep;
        }
    }

    return attacks;
}

// Squares whose occupancy can change the attacks: every ray square except the last one before the
// edge, since nothing lies behind it to block
std::uint64_t relevantBlockerSquares(Slider slider, int square) {
    std::uint64_t squares = 0ULL;

    for (int i = 0; i < 4; i++) {
        const Direction direction = directionsOf(slider)[i];
        int rank = square / 8 + direction.rankStep;
        int file = square % 8 + direction.fileStep;

        while (onBoard(rank + direction.rankStep, file + direction.fileStep)) {
            squares |= 1ULL << (rank * 8 + file);
            rank += direction.rankStep;
            file += direction.fileStep;
        }
    }

    return squares;
}

std::uint64_t magicAttacks(Slider slider, int square, std::uint64_t occupied) {
    return slider == Slider::Rook ? MagicBitboards::getRookAttacks(square, occupied)
                                  : MagicBitboards::getBishopAttacks(square, occupied);
}

// Returns false on a mismatch, so callers can stop after the first one instead of flooding output
bool matchesReference(Slider slider, int square, std::uint64_t occupied) {
    const std::uint64_t expected = referenceAttacks(slider, square, occupied);
    const std::uint64_t actual = magicAttacks(slider, square, occupied);

    EXPECT_EQ(expected, actual);

    if (expected != actual) {
        std::cerr << "    " << (slider == Slider::Rook ? "rook" : "bishop") << " on square "
                  << square << " with occupancy " << Test::valueToString(occupied) << '\n';
        return false;
    }

    return true;
}

constexpr Slider sliders[2] = {Slider::Rook, Slider::Bishop};

} // namespace

TEST_CASE("Reference slider attacks match hand-computed boards") {
    EXPECT_EQ(bitboard({"a2", "a3", "a4", "a5", "a6", "a7", "a8",
                        "b1", "c1", "d1", "e1", "f1", "g1", "h1"}),
              referenceAttacks(Slider::Rook, square("a1"), 0ULL));

    EXPECT_EQ(bitboard({"d2", "d3", "d5", "d6", "b4", "c4", "e4", "f4"}),
              referenceAttacks(Slider::Rook, square("d4"), bitboard({"d2", "d6", "b4", "f4", "a4"})));

    EXPECT_EQ(bitboard({"e5", "f6", "c5", "b6", "a7", "e3", "f2", "g1", "c3"}),
              referenceAttacks(Slider::Bishop, square("d4"), bitboard({"f6", "c3", "h8"})));

    // Ray squares minus the board edges: 10 for a central rook, 9 for a central bishop
    EXPECT_EQ(10, BitUtils::popcount(relevantBlockerSquares(Slider::Rook, square("d4"))));
    EXPECT_EQ(12, BitUtils::popcount(relevantBlockerSquares(Slider::Rook, square("a1"))));
    EXPECT_EQ(9, BitUtils::popcount(relevantBlockerSquares(Slider::Bishop, square("d4"))));
    EXPECT_EQ(6, BitUtils::popcount(relevantBlockerSquares(Slider::Bishop, square("a1"))));
}

TEST_CASE("Rook attacks on an empty board") {
    const std::uint64_t expected = bitboard({
        "e1", "e3", "e4", "e5", "e6", "e7", "e8",
        "a2", "b2", "c2", "d2", "f2", "g2", "h2"
    });

    EXPECT_EQ(expected, MagicBitboards::getRookAttacks(square("e2"), 0ULL));
}

TEST_CASE("Rook stops at the first blocker in every direction") {
    const std::uint64_t occupied = bitboard({"d2", "d6", "d8", "b4", "a4", "f4", "h4"});
    const std::uint64_t expected = bitboard({
        "d2", "d3", "d5", "d6", "b4", "c4", "e4", "f4"
    });

    EXPECT_EQ(expected, MagicBitboards::getRookAttacks(square("d4"), occupied));
}

TEST_CASE("Adjacent blockers are attacked with no squares beyond them") {
    const std::uint64_t rookBlockers = bitboard({"d3", "d5", "c4", "e4"});
    const std::uint64_t bishopBlockers = bitboard({"c3", "c5", "e3", "e5"});

    EXPECT_EQ(rookBlockers, MagicBitboards::getRookAttacks(square("d4"), rookBlockers));
    EXPECT_EQ(bishopBlockers, MagicBitboards::getBishopAttacks(square("d4"), bishopBlockers));
}

TEST_CASE("Bishop attacks on an empty board") {
    const std::uint64_t expected = bitboard({
        "e5", "f6", "g7", "h8", "c5", "b6", "a7",
        "e3", "f2", "g1", "c3", "b2", "a1"
    });

    EXPECT_EQ(expected, MagicBitboards::getBishopAttacks(square("d4"), 0ULL));
}

TEST_CASE("Bishop stops at the first blocker in every direction") {
    const std::uint64_t occupied = bitboard({"f6", "g7", "b6", "f2", "b2"});
    const std::uint64_t expected = bitboard({"e5", "f6", "c5", "b6", "e3", "f2", "c3", "b2"});

    EXPECT_EQ(expected, MagicBitboards::getBishopAttacks(square("d4"), occupied));
}

TEST_CASE("Slider attacks on corner and edge squares") {
    EXPECT_EQ(bitboard({"a2", "a3", "a4", "b1", "c1", "d1"}),
              MagicBitboards::getRookAttacks(square("a1"), bitboard({"a4", "d1", "a8"})));

    EXPECT_EQ(bitboard({"h7", "h6", "h5", "h4", "h3", "h2", "h1",
                        "g8", "f8", "e8", "d8", "c8", "b8", "a8"}),
              MagicBitboards::getRookAttacks(square("h8"), bitboard({"h1", "a8"})));

    EXPECT_EQ(bitboard({"b2", "c3", "d4", "e5", "f6", "g7", "h8"}),
              MagicBitboards::getBishopAttacks(square("a1"), 0ULL));

    EXPECT_EQ(bitboard({"g2", "f3", "e4"}),
              MagicBitboards::getBishopAttacks(square("h1"), bitboard({"e4", "b7"})));

    EXPECT_EQ(bitboard({"d2", "c3", "b4", "a5", "f2", "g3", "h4"}),
              MagicBitboards::getBishopAttacks(square("e1"), 0ULL));

    EXPECT_EQ(bitboard({"a6", "a7", "a8", "a4", "a3", "a2", "a1",
                        "b5", "c5", "d5"}),
              MagicBitboards::getRookAttacks(square("a5"), bitboard({"d5", "a1", "a8"})));
}

TEST_CASE("Slider attacks match the reference on an empty board for every square") {
    for (Slider slider : sliders) {
        for (int square = 0; square < 64; square++) {
            matchesReference(slider, square, 0ULL);
        }
    }

    // A rook always sees its full rank and file on an empty board
    for (int square = 0; square < 64; square++) {
        EXPECT_EQ(14, BitUtils::popcount(MagicBitboards::getRookAttacks(square, 0ULL)));
    }
}

TEST_CASE("Slider attacks match the reference on a fully occupied board for every square") {
    for (Slider slider : sliders) {
        for (int square = 0; square < 64; square++) {
            // With and without the slider's own square, which real boards always occupy
            matchesReference(slider, square, ~0ULL);
            matchesReference(slider, square, ~0ULL & ~(1ULL << square));
        }
    }
}

TEST_CASE("Slider attacks match the reference for every relevant blocker subset") {
    for (Slider slider : sliders) {
        for (int square = 0; square < 64; square++) {
            const std::uint64_t relevant = relevantBlockerSquares(slider, square);

            // Squares that cannot change the result: edge squares, off-ray squares and the slider
            const std::uint64_t irrelevant = ~relevant;

            // Visits every subset of the relevant squares, from the empty set back around to it
            std::uint64_t subset = 0ULL;

            do {
                if (!matchesReference(slider, square, subset) ||
                    !matchesReference(slider, square, subset | irrelevant)) {
                    break;
                }

                subset = (subset - relevant) & relevant;
            } while (subset != 0ULL);
        }
    }
}

TEST_CASE("Slider attacks match the reference for deterministic random occupancies") {
    std::mt19937_64 random(0x5EED'C0DE'2026ULL);

    for (Slider slider : sliders) {
        for (int square = 0; square < 64; square++) {
            for (int i = 0; i < 500; i++) {
                const std::uint64_t dense = random();
                const std::uint64_t sparse = random() & random() & random();
                const std::uint64_t withSlider = sparse | (1ULL << square);

                if (!matchesReference(slider, square, dense) ||
                    !matchesReference(slider, square, sparse) ||
                    !matchesReference(slider, square, withSlider)) {
                    break;
                }
            }
        }
    }
}
