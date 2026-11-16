#pragma once

#include <cstdint>
#include "Board.h"

class Zobrist {
public:
    static std::uint64_t compute(const Board& board);

    static std::uint64_t pieceKey(Piece piece, int square);
    static std::uint64_t castlingKey(std::uint8_t castlingRights);
    static std::uint64_t enPassantKey(const Board& board);
    static std::uint64_t sideToMoveKey();
};