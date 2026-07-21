#pragma once 
#include <string>
#include <cstdint>

enum Piece{
    WHITE_PAWN,
    WHITE_KNIGHT,
    WHITE_BISHOP,
    WHITE_ROOK,
    WHITE_QUEEN,
    WHITE_KING,

    BLACK_PAWN,
    BLACK_KNIGHT,
    BLACK_BISHOP,
    BLACK_ROOK,
    BLACK_QUEEN,
    BLACK_KING,

    PIECE_COUNT
};

class Board {
public:
    Board();
    void setStartingPosition();
    void clearBoard();
    void updateOccupancy();
    void print() const;
    void loadFEN(const std::string& fen);

private:
    std::uint64_t pieces[PIECE_COUNT]{};

    std::uint64_t whitePieces = 0;
    std::uint64_t blackPieces = 0;
    std::uint64_t occupied = 0;

    bool whiteToMove = true;
    int halfmoveClock = 0;
    int fullmoveCounter = 1;
    std::uint8_t castlingRights = 0;
    int enPassantSquare = -1;

    char pieceAt(int square) const;
};