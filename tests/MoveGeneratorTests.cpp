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

TEST_CASE("White kingside castling is generated when legal") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/4K2R w K - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateCastlingMoves(board, moves);

    EXPECT_EQ(1, moves.count);

    EXPECT_EQ(4, moves.moves[0].from); // e1
    EXPECT_EQ(6, moves.moves[0].to);   // g1
    EXPECT_EQ(MoveType::KingCastle, moves.moves[0].type);
}

TEST_CASE("White kingside castling is blocked by attacked transit square") {
    Board board;
    board.loadFEN("4kr2/8/8/8/8/8/8/4K2R w K - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateCastlingMoves(board, moves);

    EXPECT_EQ(0, moves.count);
}

TEST_CASE("Starting position has 20 pseudo-legal moves") {
    Board board;
    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generatePseudoLegalMoves(board, moves);

    EXPECT_EQ(20, moves.count);
}


TEST_CASE("Pinned knight has no legal moves") {
    Board board;
    board.loadFEN("4r2k/8/8/8/8/8/4N3/4K3 w - - 0 1"); // knight e2 pinned by rook e8

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateLegalMoves(board, moves);

    EXPECT_EQ(4, moves.count); // Kd1, Kd2, Kf1, Kf2

    for (int i = 0; i < moves.count; i++) {
        EXPECT_FALSE(moves.moves[i].from == Test::square("e2"));
    }
}

TEST_CASE("King in check only has evasions") {
    Board board;
    board.loadFEN("4r2k/8/8/8/8/8/R7/4K3 w - - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateLegalMoves(board, moves);

    EXPECT_EQ(5, moves.count); // Kd1, Kd2, Kf1, Kf2, Re2

    for (int i = 0; i < moves.count; i++) {
        EXPECT_FALSE(moves.moves[i].to == Test::square("e2") &&
                     moves.moves[i].from == Test::square("e1"));
    }
}

TEST_CASE("En passant exposing the king is illegal") {
    Board board;
    board.loadFEN("8/8/8/K2pP2r/8/8/8/7k w - d6 0 1");

    MoveList pseudoMoves;
    MoveList legalMoves;

    MoveGenerator::init();
    MoveGenerator::generatePseudoLegalMoves(board, pseudoMoves);
    MoveGenerator::generateLegalMoves(board, legalMoves);

    bool pseudoHasEnPassant = false;
    for (int i = 0; i < pseudoMoves.count; i++) {
        if (pseudoMoves.moves[i].type == MoveType::EnPassant) {
            pseudoHasEnPassant = true;
        }
    }

    bool legalHasEnPassant = false;
    for (int i = 0; i < legalMoves.count; i++) {
        if (legalMoves.moves[i].type == MoveType::EnPassant) {
            legalHasEnPassant = true;
        }
    }

    EXPECT_TRUE(pseudoHasEnPassant);
    EXPECT_FALSE(legalHasEnPassant);
}

TEST_CASE("Starting position has 20 legal moves") {
    Board board;
    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateLegalMoves(board, moves);

    EXPECT_EQ(20, moves.count);
}

TEST_CASE("Kiwipete has 48 legal moves including both castles") {
    Board board;
    board.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateLegalMoves(board, moves);

    EXPECT_EQ(48, moves.count);

    bool hasKingCastle = false;
    bool hasQueenCastle = false;
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].type == MoveType::KingCastle) {
            hasKingCastle = true;
        }
        if (moves.moves[i].type == MoveType::QueenCastle) {
            hasQueenCastle = true;
        }
    }

    EXPECT_TRUE(hasKingCastle);
    EXPECT_TRUE(hasQueenCastle);
}

TEST_CASE("Legal move generation leaves the board unchanged") {
    Board board;
    board.loadFEN("r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 5 10");

    Board original = board;
    MoveList moves;

    MoveGenerator::init();
    MoveGenerator::generateLegalMoves(board, moves);

    for (int p = 0; p < PIECE_COUNT; p++) {
        Piece piece = static_cast<Piece>(p);

        EXPECT_EQ(original.getPieceBitboard(piece), board.getPieceBitboard(piece));
    }

    EXPECT_EQ(original.getOccupied(), board.getOccupied());
    EXPECT_EQ(original.isWhiteToMove(), board.isWhiteToMove());
    EXPECT_EQ(original.getCastlingRights(), board.getCastlingRights());
    EXPECT_EQ(original.getEnPassantSquare(), board.getEnPassantSquare());
    EXPECT_EQ(original.getHalfmoveClock(), board.getHalfmoveClock());
    EXPECT_EQ(original.getFullmoveCounter(), board.getFullmoveCounter());
}
