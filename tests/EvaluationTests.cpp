#include "Evaluation.h"
#include "Board.h"
#include "TestUtils.h"

#include <iostream>
#include <string>

namespace {

int evaluateFEN(const std::string& fen) {
    Board board;
    board.loadFEN(fen);
    return Evaluation::evaluate(board);
}

int kingSafetyFEN(const std::string& fen, bool white) {
    Board board;
    board.loadFEN(fen);
    return Evaluation::kingSafety(board, white);
}

int phaseFEN(const std::string& fen) {
    Board board;
    board.loadFEN(fen);
    return Evaluation::gamePhase(board);
}

} // namespace

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

TEST_CASE("Game phase counts non-pawn material") {
    EXPECT_EQ(24, phaseFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    EXPECT_EQ(0, phaseFEN("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1"));
    EXPECT_EQ(4, phaseFEN("4k3/8/8/8/8/8/8/3QK3 w - - 0 1"));

    // Rook 2, bishop 1, knight 1
    EXPECT_EQ(4, phaseFEN("r3k3/8/8/8/8/8/8/2B1K1N1 w - - 0 1"));

    // Promoted queens cannot push the phase past the middlegame
    EXPECT_EQ(24, phaseFEN("qqqqk3/8/8/8/8/8/8/QQQQK3 w - - 0 1"));
}

TEST_CASE("Kings in the starting position are safe") {
    const std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    EXPECT_EQ(0, kingSafetyFEN(fen, true));
    EXPECT_EQ(0, kingSafetyFEN(fen, false));
}

TEST_CASE("Intact castled pawn shield has no penalty") {
    const std::string fen = "6k1/5ppp/8/8/8/8/5PPP/6K1 w - - 0 1";

    EXPECT_EQ(0, kingSafetyFEN(fen, true));
    EXPECT_EQ(0, kingSafetyFEN(fen, false));
}

TEST_CASE("Advanced shield pawn is a smaller weakness than a missing one") {
    // h2-h3: the pawn still covers the king from one square further away
    EXPECT_EQ(-10, kingSafetyFEN("6k1/5ppp/8/8/8/7P/5PP1/6K1 w - - 0 1", true));

    // g2-g4: too far away to shield the king
    EXPECT_EQ(-20, kingSafetyFEN("6k1/5ppp/8/8/6P1/8/5P1P/6K1 w - - 0 1", true));

    // No h-pawn: missing shield pawn plus a file opened toward the king
    EXPECT_EQ(-30, kingSafetyFEN("6k1/5ppp/8/8/8/8/5PP1/6K1 w - - 0 1", true));
}

TEST_CASE("Open file next to the king is worse than a semi-open one") {
    // White g-pawn gone, Black g-pawn still blocks the file
    int semiOpen = kingSafetyFEN("6k1/5ppp/8/8/8/8/5P1P/6K1 w - - 0 1", true);

    // Both g-pawns gone
    int open = kingSafetyFEN("6k1/5p1p/8/8/8/8/5P1P/6K1 w - - 0 1", true);

    EXPECT_EQ(-30, semiOpen);
    EXPECT_EQ(-40, open);
}

TEST_CASE("Central king is not penalized for advancing center pawns") {
    // The king on e1 still expects to castle, so d4 and e4 do not weaken it
    EXPECT_EQ(0, kingSafetyFEN("4k3/pppppppp/8/8/3PP3/8/PPP2PPP/4K3 w - - 0 1", true));
}

TEST_CASE("King that walks away from its pawns is exposed") {
    int sheltered = kingSafetyFEN("6k1/5ppp/8/8/8/8/5PPP/6K1 w - - 0 1", true);
    int walkedOut = kingSafetyFEN("6k1/5ppp/8/8/6K1/8/5PPP/8 w - - 0 1", true);

    EXPECT_EQ(0, sheltered);
    EXPECT_EQ(-60, walkedOut);
}

TEST_CASE("Enemy attacks near the king count only with several attackers") {
    // The queen hits f2 and h2, but a single attacker is not counted
    EXPECT_EQ(0, kingSafetyFEN("6k1/5ppp/8/8/7q/8/5PPP/6K1 w - - 0 1", true));

    // A knight joins on f2 and h2: (2 * 16 + 2 * 8) at 50% for two attackers
    EXPECT_EQ(-24, kingSafetyFEN("6k1/5ppp/8/8/6nq/8/5PPP/6K1 w - - 0 1", true));

    // A bishop joins on f2: (32 + 16 + 8) at 75% for three attackers
    EXPECT_EQ(-42, kingSafetyFEN("6k1/5ppp/8/8/3b2nq/8/5PPP/6K1 w - - 0 1", true));

    // Pieces that do not reach the king zone are not attackers
    EXPECT_EQ(0, kingSafetyFEN("6k1/5ppp/n7/8/7q/8/5PPP/6K1 w - - 0 1", true));
}

TEST_CASE("King safety counts fully in the middlegame") {
    // Full material, so the phase is 24 and the whole shield penalty applies
    int shieldIntact =
        evaluateFEN("rnbq1rk1/ppppbppp/5n2/4p3/4P3/5N2/PPPPBPPP/RNBQ1RK1 w - - 0 1");
    int pawnOnH4 =
        evaluateFEN("rnbq1rk1/ppppbppp/5n2/4p3/4P2P/5N2/PPPPBPP1/RNBQ1RK1 w - - 0 1");

    EXPECT_EQ(20, shieldIntact - pawnOnH4);
}

TEST_CASE("King safety does not count in a pawn endgame") {
    // h2 and h4 have the same pawn table value, so only king safety could separate them
    int shieldIntact = evaluateFEN("6k1/5ppp/8/8/8/8/5PPP/6K1 w - - 0 1");
    int pawnOnH4 = evaluateFEN("6k1/5ppp/8/8/7P/8/5PP1/6K1 w - - 0 1");

    EXPECT_EQ(shieldIntact, pawnOnH4);
}

TEST_CASE("Middlegame king prefers shelter") {
    int castled =
        evaluateFEN("r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 1");
    int kingOnE2 =
        evaluateFEN("r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP1KPPP/R1BQ1R2 w - - 0 1");

    EXPECT_TRUE(castled > kingOnE2);
}

TEST_CASE("Endgame king prefers activity") {
    int inCorner = evaluateFEN("6k1/5ppp/8/8/8/8/5PPP/6K1 w - - 0 1");
    int centralized = evaluateFEN("6k1/5ppp/8/8/8/4K3/5PPP/8 w - - 0 1");

    EXPECT_TRUE(centralized > inCorner);

    // Bare kings: g1 and e8 are equally far from the center, d4 is in it
    EXPECT_EQ(0, evaluateFEN("4k3/8/8/8/8/8/8/6K1 w - - 0 1"));
    EXPECT_EQ(70, evaluateFEN("4k3/8/8/8/3K4/8/8/8 w - - 0 1"));
}

TEST_CASE("Evaluation is symmetric between White and Black") {
    struct Scenario {
        const char* fen;
        const char* mirrored;
    };

    const Scenario scenarios[] = {
        // Kiwipete
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         "r3k2r/pppbbppp/2n2q1P/1P2p3/3pn3/BN2PNP1/P1PPQPB1/R3K2R b KQkq - 0 1"},

        // Perft position 5
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         "rnbqk2r/ppp1nNpp/8/2b5/8/2P5/PP1pBPPP/RNBQ1K1R b kq - 1 8"},

        // Uncastled king facing an open file
        {"2r1k2r/p5pp/2p2b2/2BQpq2/B3p3/8/PP3PPP/5RK1 b k - 0 26",
         "5rk1/pp3ppp/8/b3P3/2bqPQ2/2P2B2/P5PP/2R1K2R w K - 0 26"},

        // Enemy queen next to a castled king
        {"5rk1/pp3pp1/2n5/2b1pb2/6p1/2N2qP1/PP1P1P1P/R1B2QKR w - - 4 19",
         "r1b2qkr/pp1p1p1p/2n2Qp1/6P1/2B1PB2/2N5/PP3PP1/5RK1 b - - 4 19"},

        // King walked out with queens on the board
        {"8/2Q3k1/2p1r1p1/p6p/q2p3P/5PP1/1R2P1K1/8 b - - 0 40",
         "8/1r2p1k1/5pp1/Q2P3p/P6P/2P1R1P1/2q3K1/8 w - - 0 40"},

        // Three pieces attacking the king
        {"6k1/5ppp/8/8/3b2nq/8/5PPP/6K1 w - - 0 1",
         "6k1/5ppp/8/3B2NQ/8/8/5PPP/6K1 b - - 0 1"},

        // Pawn endgame
        {"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1",
         "4k3/4p3/8/8/4K3/8/8/8 b - - 0 1"},
    };

    for (const auto& scenario : scenarios) {
        int score = evaluateFEN(scenario.fen);
        int mirroredScore = evaluateFEN(scenario.mirrored);

        if (score != -mirroredScore) {
            std::cerr << "    position: " << scenario.fen << '\n';
        }

        EXPECT_EQ(score, -mirroredScore);
        EXPECT_EQ(kingSafetyFEN(scenario.fen, true), kingSafetyFEN(scenario.mirrored, false));
        EXPECT_EQ(kingSafetyFEN(scenario.fen, false), kingSafetyFEN(scenario.mirrored, true));
    }
}