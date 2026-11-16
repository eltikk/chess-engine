#include "Board.h"
#include "Move.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include "MoveList.h"
#include "TestUtils.h"

namespace {

int countHashMismatches(Board& board, int depth) {
    if (depth == 0) {
        return 0;
    }

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    int mismatches = 0;

    for (int i = 0; i < moves.count; i++) {
        const std::uint64_t hashBefore = board.getHash();
        UndoState undo;

        board.makeMove(moves.moves[i], undo);

        if (board.getHash() != Zobrist::compute(board)) {
            mismatches++;
        }

        mismatches += countHashMismatches(board, depth - 1);

        board.unmakeMove(moves.moves[i], undo);

        if (board.getHash() != hashBefore) {
            mismatches++;
        }
    }

    return mismatches;
}

} // namespace

TEST_CASE("Zobrist produces deterministic hashes") {
    Board board;

    const auto hash1 = Zobrist::compute(board);
    const auto hash2 = Zobrist::compute(board);

    EXPECT_EQ(hash1, hash2);
}

TEST_CASE("Zobrist distinguishes side to move") {
    Board white;
    Board black;

    black.loadFEN(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"
    );

    EXPECT_TRUE(
        Zobrist::compute(white) != Zobrist::compute(black)
    );
}

TEST_CASE("Zobrist distinguishes castling rights") {
    Board board1;
    Board board2;

    board2.loadFEN(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1"
    );

    EXPECT_TRUE(
        Zobrist::compute(board1) != Zobrist::compute(board2)
    );
}

TEST_CASE("Zobrist changes after moving a piece") {
    Board board;

    const auto originalHash = Zobrist::compute(board);

    UndoState undo;
    Move move{6, 21, MoveType::Quiet}; // Ng1-f3

    board.makeMove(move, undo);

    EXPECT_TRUE(
        originalHash != Zobrist::compute(board)
    );
}

TEST_CASE("Zobrist restores hash after unmakeMove") {
    Board board;

    const auto originalHash = Zobrist::compute(board);

    UndoState undo;
    Move move{6, 21, MoveType::Quiet};

    board.makeMove(move, undo);
    board.unmakeMove(move, undo);

    EXPECT_EQ(originalHash, Zobrist::compute(board));
}

TEST_CASE("Zobrist ignores move counters") {
    Board board1;
    Board board2;

    board2.loadFEN(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 15 30"
    );

    EXPECT_EQ(
        Zobrist::compute(board1),
        Zobrist::compute(board2)
    );
}

TEST_CASE("Board hash matches recomputation after initialization") {
    Board board;
    EXPECT_EQ(Zobrist::compute(board), board.getHash());

    board.clearBoard();
    EXPECT_EQ(Zobrist::compute(board), board.getHash());

    board.setStartingPosition();
    EXPECT_EQ(Zobrist::compute(board), board.getHash());

    board.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1");
    EXPECT_EQ(Zobrist::compute(board), board.getHash());

    board.loadFEN("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    EXPECT_EQ(Zobrist::compute(board), board.getHash());
}

TEST_CASE("Incremental hash matches recomputation for each move type") {

    struct Scenario {
        const char* fen;
        Move move;
    };

    const Scenario scenarios[] = {
        // Quiet move: Ng1-f3
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {6, 21, MoveType::Quiet}},

        // Black quiet move: Ng8-f6
        {"rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1", {62, 45, MoveType::Quiet}},

        // Capture: Nf3xe5
        {"rnbqkbnr/pppp1ppp/8/4p3/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 0 2", {21, 36, MoveType::Capture}},

        // Double pawn push with no possible en passant reply: e2-e4
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {12, 28, MoveType::DoublePawnPush}},

        // Double pawn push next to an enemy pawn: e2-e4
        {"4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1", {12, 28, MoveType::DoublePawnPush}},

        // Black double pawn push next to an enemy pawn: d7-d5
        {"4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 1", {51, 35, MoveType::DoublePawnPush}},

        // White en passant: e5xd6
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", {36, 43, MoveType::EnPassant}},

        // Black en passant: d4xe3
        {"4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", {27, 20, MoveType::EnPassant}},

        // Promotion: a7-a8=Q
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", {48, 56, MoveType::Promotion, WHITE_QUEEN}},

        // Underpromotion: h2-h1=N
        {"4k3/8/8/8/8/8/7p/K7 b - - 0 1", {15, 7, MoveType::Promotion, BLACK_KNIGHT}},

        // Promotion capture removing castling rights: b7xa8=Q
        {"r3k3/1P6/8/8/8/8/8/4K3 w q - 0 1", {49, 56, MoveType::PromotionCapture, WHITE_QUEEN}},

        // White kingside castling
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {4, 6, MoveType::KingCastle}},

        // White queenside castling
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {4, 2, MoveType::QueenCastle}},

        // Black kingside castling
        {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", {60, 62, MoveType::KingCastle}},

        // Black queenside castling
        {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", {60, 58, MoveType::QueenCastle}},

        // King move removes both castling rights
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {4, 12, MoveType::Quiet}},

        // Rook move removes one castling right
        {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", {63, 55, MoveType::Quiet}},

        // Rook capture removes castling rights for both sides
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {0, 56, MoveType::Capture}}};

    for (const auto& scenario : scenarios) {

        Board board;
        board.loadFEN(scenario.fen);

        const std::uint64_t originalHash = board.getHash();
        UndoState undo;

        board.makeMove(scenario.move, undo);

        EXPECT_EQ(Zobrist::compute(board), board.getHash());
        EXPECT_TRUE(originalHash != board.getHash());

        board.unmakeMove(scenario.move, undo);

        EXPECT_EQ(originalHash, board.getHash());
        EXPECT_EQ(Zobrist::compute(board), board.getHash());
    }
}

TEST_CASE("Hash repeats after returning to the same position") {
    Board board;

    const std::uint64_t startHash = board.getHash();

    const Move moves[] = {
        {6, 21, MoveType::Quiet},  // Ng1-f3
        {62, 45, MoveType::Quiet}, // Ng8-f6
        {21, 6, MoveType::Quiet},  // Nf3-g1
        {45, 62, MoveType::Quiet}  // Nf6-g8
    };

    UndoState undo;

    for (const Move& move : moves) {
        board.makeMove(move, undo);
    }

    EXPECT_EQ(startHash, board.getHash());
}

TEST_CASE("Hash ignores en passant square when no capture is possible") {
    Board board;
    UndoState undo;

    board.makeMove({12, 28, MoveType::DoublePawnPush}, undo); // e2-e4

    Board expected;
    expected.loadFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");

    EXPECT_EQ(20, board.getEnPassantSquare());
    EXPECT_EQ(expected.getHash(), board.getHash());
}

TEST_CASE("Hash includes en passant square when a capture is possible") {
    Board board;
    board.loadFEN("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");

    UndoState undo;
    board.makeMove({12, 28, MoveType::DoublePawnPush}, undo); // e2-e4

    Board withEnPassant;
    withEnPassant.loadFEN("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");

    Board withoutEnPassant;
    withoutEnPassant.loadFEN("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1");

    EXPECT_EQ(withEnPassant.getHash(), board.getHash());
    EXPECT_TRUE(withoutEnPassant.getHash() != board.getHash());
}

TEST_CASE("Incremental hash matches recomputation through legal move trees") {
    Board board;
    EXPECT_EQ(0, countHashMismatches(board, 4));

    board.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    EXPECT_EQ(0, countHashMismatches(board, 3));

    board.loadFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    EXPECT_EQ(0, countHashMismatches(board, 4));

    board.loadFEN("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    EXPECT_EQ(0, countHashMismatches(board, 3));
}
