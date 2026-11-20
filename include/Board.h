#pragma once
#include <string>
#include <cstdint>
#include <vector>

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

struct Move;

struct UndoState{
    Piece movedPiece = PIECE_COUNT;
    Piece capturedPiece = PIECE_COUNT;
    int capturedSquare = -1;

    std::uint8_t castlingRights = 0;
    int enPassantSquare = -1;

    int halfmoveClock = 0;
    int fullmoveCounter = 1;

    bool whiteToMove = true;

    std::uint64_t hash = 0;
    std::uint64_t enPassantKey = 0;
};

class Board {
public:
    Board();
    void setStartingPosition();
    void clearBoard();
    void updateOccupancy();
    void print() const;
    void loadFEN(const std::string& fen);
    void makeMove(const Move& move, UndoState& undo);
    void unmakeMove(const Move& move, const UndoState& undo);

    static constexpr int fiftyMoveLimit = 100;

    // Threefold repetition. During search, pass the current ply: a single earlier occurrence after
    // the search root is then also reported. That is a search heuristic rather than a threefold
    // claim: a line that repeated the position once could repeat it again.
    bool isRepetition(int searchPly = 0) const { return repetitionDistance(searchPly) != 0; }
    // Plies back to the furthest occurrence the repetition relies on, or 0 if there is none
    int repetitionDistance(int searchPly = 0) const;
    bool isFiftyMoveRule() const { return halfmoveClock >= fiftyMoveLimit; }
    bool isInsufficientMaterial() const;

    std::uint64_t getPieceBitboard(Piece p) const { return pieces[p]; }
    std::uint64_t getWhitePieces() const { return whitePieces; }
    std::uint64_t getBlackPieces() const { return blackPieces; }
    std::uint64_t getOccupied() const { return occupied; }
    std::uint64_t getFriendlyPieces() const { return whiteToMove ? whitePieces : blackPieces;}
    std::uint64_t getEnemyPieces() const { return whiteToMove ? blackPieces : whitePieces;}
    bool isWhiteToMove() const { return whiteToMove; }
    int getHalfmoveClock() const { return halfmoveClock; }
    int getFullmoveCounter() const { return fullmoveCounter; }
    std::uint8_t getCastlingRights() const { return castlingRights; }
    int getEnPassantSquare() const { return enPassantSquare; }
    char getPieceAt(int square) const { return pieceAt(square); }
    std::uint64_t getHash() const { return hash; }
    const std::vector<std::uint64_t>& getHistory() const { return history; }


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

    std::uint64_t hash = 0;

    // En passant key included in hash, cached because computing it requires a legality check
    std::uint64_t enPassantKey = 0;

    // Hashes of the positions before each move made since the last loadFEN/setStartingPosition
    std::vector<std::uint64_t> history;

    char pieceAt(int square) const;
    Piece pieceAtSquare(int square) const;
};
