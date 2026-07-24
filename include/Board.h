#pragma once 
#include <string>
#include <cstdint>

enum Piece : std::uint8_t{
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

    //Debug getters
    std::uint64_t getPieceBitboard(Piece p) const { return pieces[p]; }
    std::uint64_t getWhitePieces() const { return whitePieces; }
    std::uint64_t getBlackPieces() const { return blackPieces; }
    std::uint64_t getOccupied() const { return occupied; }

    bool isWhiteToMove() const { return whiteToMove; }
    int getHalfmoveClock() const { return halfmoveClock; }
    int getFullmoveCounter() const { return fullmoveCounter; }
    std::uint8_t getCastlingRights() const { return castlingRights; }
    int getEnPassantSquare() const { return enPassantSquare; }
    char getPieceAt(int square) const { return pieceAt(square); } 

    
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
