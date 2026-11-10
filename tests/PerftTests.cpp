#include "Perft.h"
#include "Board.h"
#include "MoveGenerator.h"
#include "TestUtils.h"

TEST_CASE("Perft starting position") {
    Board board;
    MoveGenerator::init();

    EXPECT_EQ(20ULL, Perft::run(board, 1));
    EXPECT_EQ(400ULL, Perft::run(board, 2));
    EXPECT_EQ(8902ULL, Perft::run(board, 3));
    EXPECT_EQ(197281ULL, Perft::run(board, 4));
}

TEST_CASE("Perft Kiwipete") {
    Board board;

    board.loadFEN(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/"
        "1p2P3/2N2Q1p/PPPBBPPP/R3K2R "
        "w KQkq - 0 1"
    );

    MoveGenerator::init();

    EXPECT_EQ(48ULL, Perft::run(board, 1));
    EXPECT_EQ(2039ULL, Perft::run(board, 2));
    EXPECT_EQ(97862ULL, Perft::run(board, 3));
}

TEST_CASE("Perft Position 3") {
    Board board;
    board.loadFEN(
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 "
        "w - - 0 1"
    );

    MoveGenerator::init();

    EXPECT_EQ(14ULL, Perft::run(board, 1));
    EXPECT_EQ(191ULL, Perft::run(board, 2));
    EXPECT_EQ(2812ULL, Perft::run(board, 3));
    EXPECT_EQ(43238ULL, Perft::run(board, 4));
}

TEST_CASE("Perft Position 4") {
    Board board;
    board.loadFEN(
        "r3k2r/Pppp1ppp/1b3nbN/nP6/"
        "BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 "
        "w kq - 0 1"
    );

    MoveGenerator::init();

    EXPECT_EQ(6ULL, Perft::run(board, 1));
    EXPECT_EQ(264ULL, Perft::run(board, 2));
    EXPECT_EQ(9467ULL, Perft::run(board, 3));
    EXPECT_EQ(422333ULL, Perft::run(board, 4));
}

TEST_CASE("Perft Divide starting position") {
    Board board;
    MoveGenerator::init();

    auto results = Perft::divide(board, 3);

    EXPECT_EQ(20, static_cast<int>(results.size()));

    std::uint64_t total = 0;

    for (const auto& entry : results) {
        total += entry.nodes;
    }

    EXPECT_EQ(8902ULL, total);

    auto findNodes = [&](int from, int to) -> std::uint64_t {
        for (const auto& entry : results) {
            if (entry.move.from == from && entry.move.to == to) {
                return entry.nodes;
            }
        }

        return 0;
    };

    EXPECT_EQ(600ULL, findNodes(Test::square("e2"), Test::square("e4")));
    EXPECT_EQ(560ULL, findNodes(Test::square("d2"), Test::square("d4")));
    EXPECT_EQ(440ULL, findNodes(Test::square("g1"), Test::square("f3")));
    EXPECT_EQ(380ULL, findNodes(Test::square("a2"), Test::square("a3")));
}

TEST_CASE("Move converts to UCI notation") {
    Move pawnMove{12, 28, MoveType::DoublePawnPush};

    EXPECT_EQ(std::string("e2e4"), pawnMove.toUCI());

    Move knightMove{6, 21, MoveType::Quiet};

    EXPECT_EQ(std::string("g1f3"), knightMove.toUCI());

    Move promotion{48, 56, MoveType::Promotion, WHITE_QUEEN};

    EXPECT_EQ(std::string("a7a8q"), promotion.toUCI());
}