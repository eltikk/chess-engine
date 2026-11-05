#include "BitUtils.h"
#include "Board.h"
#include "Move.h"
#include "TestUtils.h"

#include <stdexcept>
#include <string>

namespace {

bool loadFENThrows(Board& board, const std::string& fen) {
    try {
        board.loadFEN(fen);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

} // namespace

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

TEST_CASE("Board loads six-field starting position FEN") {
    Board board;
    board.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    const Board startingPosition;
    for (int piece = WHITE_PAWN; piece < PIECE_COUNT; piece++) {
        EXPECT_EQ(startingPosition.getPieceBitboard(static_cast<Piece>(piece)),
                  board.getPieceBitboard(static_cast<Piece>(piece)));
    }
    EXPECT_TRUE(board.isWhiteToMove());
    EXPECT_EQ(0b1111, board.getCastlingRights());
    EXPECT_EQ(-1, board.getEnPassantSquare());
    EXPECT_EQ(0, board.getHalfmoveClock());
    EXPECT_EQ(1, board.getFullmoveCounter());
}

TEST_CASE("Board loads FEN without clock fields") {
    Board board;
    board.loadFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -");

    EXPECT_TRUE(board.isWhiteToMove());
    EXPECT_EQ(0, board.getHalfmoveClock());
    EXPECT_EQ(1, board.getFullmoveCounter());
    EXPECT_EQ(-1, board.getEnPassantSquare());
    EXPECT_EQ(0, board.getCastlingRights());
    EXPECT_EQ('K', board.getPieceAt(Test::square("a5")));
    EXPECT_EQ('r', board.getPieceAt(Test::square("h5")));
    EXPECT_EQ(10, BitUtils::popcount(board.getOccupied()));
}

TEST_CASE("Board loads FEN with only the halfmove clock") {
    Board board;
    board.loadFEN("4k3/8/8/8/8/8/8/4K3 b - - 12");

    EXPECT_EQ(12, board.getHalfmoveClock());
    EXPECT_EQ(1, board.getFullmoveCounter());
}

TEST_CASE("Board loads FEN with extra whitespace") {
    Board board;
    board.loadFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - ");

    EXPECT_TRUE(board.isWhiteToMove());
    EXPECT_EQ(0, board.getHalfmoveClock());
    EXPECT_EQ(1, board.getFullmoveCounter());
    EXPECT_EQ('K', board.getPieceAt(Test::square("a5")));

    board.loadFEN("  4k3/8/8/8/8/8/8/4K3   b  -  -  7   42  \n");

    EXPECT_FALSE(board.isWhiteToMove());
    EXPECT_EQ(7, board.getHalfmoveClock());
    EXPECT_EQ(42, board.getFullmoveCounter());
}

TEST_CASE("Board loads en passant square from FEN") {
    Board board;

    board.loadFEN("4k3/8/8/4pP2/8/8/8/4K3 w - e6 0 2");
    EXPECT_EQ(Test::square("e6"), board.getEnPassantSquare());

    board.loadFEN("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
    EXPECT_EQ(Test::square("e3"), board.getEnPassantSquare());

    board.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(-1, board.getEnPassantSquare());
}

TEST_CASE("Board rejects invalid en passant squares") {
    Board board;

    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w - e3 0 1")); // wrong rank for side
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 b - e6 0 1")); // wrong rank for side
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w - e4 0 1"));
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w - i6 0 1"));
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w - e 0 1"));
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w - e66 0 1"));
}

TEST_CASE("Board loads castling rights from FEN") {
    Board board;

    board.loadFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    EXPECT_EQ(0b1111, board.getCastlingRights());

    board.loadFEN("r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1");
    EXPECT_EQ(0b1001, board.getCastlingRights());

    board.loadFEN("r3k2r/8/8/8/8/8/8/R3K2R b Qk - 0 1");
    EXPECT_EQ(0b0110, board.getCastlingRights());

    board.loadFEN("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1");
    EXPECT_EQ(0, board.getCastlingRights());
}

TEST_CASE("Board rejects FEN missing required fields") {
    Board board;

    EXPECT_TRUE(loadFENThrows(board, ""));
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3"));
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w"));
    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w -"));
}

TEST_CASE("Rejected FEN leaves the board unchanged") {
    Board board;
    board.loadFEN("8/8/8/3k4/8/4K3/8/8 b - - 7 42");

    EXPECT_TRUE(loadFENThrows(board, "4k3/8/8/8/8/8/8/4K3 w - z9 0 1"));

    EXPECT_FALSE(board.isWhiteToMove());
    EXPECT_EQ('k', board.getPieceAt(Test::square("d5")));
    EXPECT_EQ('K', board.getPieceAt(Test::square("e3")));
    EXPECT_EQ(7, board.getHalfmoveClock());
    EXPECT_EQ(42, board.getFullmoveCounter());
}

TEST_CASE("makeMove handles quiet knight moves") {
    Board board;
    UndoState undo;

    Move move{6, 21, MoveType::Quiet}; // Ng1-f3

    board.makeMove(move, undo);

    EXPECT_EQ('.', board.getPieceAt(6));
    EXPECT_EQ('N', board.getPieceAt(21));

    EXPECT_FALSE(board.isWhiteToMove());

    EXPECT_EQ(1, board.getHalfmoveClock());
    EXPECT_EQ(1, board.getFullmoveCounter());
}

TEST_CASE("makeMove and unmakeMove restore the original position") {

    struct Scenario {
        const char* fen;
        Move move;
    };

    const Scenario scenarios[] = {
        // Quiet move: Ng1-f3
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {6, 21, MoveType::Quiet}},

        // Double pawn push: e2-e4
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
         {12, 28, MoveType::DoublePawnPush}},

        // Capture: Ra1xa8
        {"r3k3/8/8/8/8/8/8/R3K3 w Qq - 0 1", {0, 56, MoveType::Capture}},

        // En passant: e5xd6
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", {36, 43, MoveType::EnPassant}},

        // Promotion: a7-a8=Q
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", {48, 56, MoveType::Promotion, WHITE_QUEEN}},

        // Promotion capture: b7xa8=Q
        {"r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", {49, 56, MoveType::PromotionCapture, WHITE_QUEEN}},

        // White kingside castling
        {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", {4, 6, MoveType::KingCastle}},

        // Black queenside castling
        {"r3k2r/8/8/8/8/8/8/4K3 b kq - 4 10", {60, 58, MoveType::QueenCastle}}};

    for (const auto& scenario : scenarios) {

        Board board;
        board.loadFEN(scenario.fen);

        Board original = board;
        UndoState undo;

        board.makeMove(scenario.move, undo);
        if (scenario.move.type == MoveType::DoublePawnPush) {
            EXPECT_EQ(20, board.getEnPassantSquare());
        }

        if (scenario.move.type == MoveType::KingCastle) {
            EXPECT_EQ('K', board.getPieceAt(6)); // King on g1
            EXPECT_EQ('R', board.getPieceAt(5)); // Rook on f1

            EXPECT_EQ('.', board.getPieceAt(4)); // e1 empty
            EXPECT_EQ('.', board.getPieceAt(7)); // h1 empty

            EXPECT_EQ(0, board.getCastlingRights() & 0b1100);
        }

        if (scenario.move.type == MoveType::QueenCastle) {
            EXPECT_EQ('k', board.getPieceAt(58)); // King on c8
            EXPECT_EQ('r', board.getPieceAt(59)); // Rook on d8

            EXPECT_EQ('.', board.getPieceAt(60)); // e8 empty
            EXPECT_EQ('.', board.getPieceAt(56)); // a8 empty

            EXPECT_EQ(0, board.getCastlingRights() & 0b0011);
        }

        // Verify en passant actually removes the captured pawn
        if (scenario.move.type == MoveType::EnPassant) {
            EXPECT_EQ('P', board.getPieceAt(43));
            EXPECT_EQ('.', board.getPieceAt(35));
        }

        // Verify promotion creates the promoted piece
        if (scenario.move.type == MoveType::Promotion ||
            scenario.move.type == MoveType::PromotionCapture) {
            EXPECT_EQ('Q', board.getPieceAt(56));
        }

        board.unmakeMove(scenario.move, undo);

        // Compare all 12 piece bitboards
        for (int p = 0; p < PIECE_COUNT; p++) {
            Piece piece = static_cast<Piece>(p);

            EXPECT_EQ(original.getPieceBitboard(piece), board.getPieceBitboard(piece));
        }

        // Compare occupancy
        EXPECT_EQ(original.getOccupied(), board.getOccupied());
        EXPECT_EQ(original.getWhitePieces(), board.getWhitePieces());
        EXPECT_EQ(original.getBlackPieces(), board.getBlackPieces());

        // Compare game state
        EXPECT_EQ(original.isWhiteToMove(), board.isWhiteToMove());
        EXPECT_EQ(original.getCastlingRights(), board.getCastlingRights());
        EXPECT_EQ(original.getEnPassantSquare(), board.getEnPassantSquare());
        EXPECT_EQ(original.getHalfmoveClock(), board.getHalfmoveClock());
        EXPECT_EQ(original.getFullmoveCounter(), board.getFullmoveCounter());
    }
}
