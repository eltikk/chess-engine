#pragma once

#include <cstdint>
#include "Board.h"

enum class MoveType : uint8_t{
    Quiet,
    Capture,
    DoublePawnPush,
    KingCastle,
    QueenCastle,
    EnPassant,
    Promotion,
    PromotionCapture
};

struct Move{
    std::uint8_t from;
    std::uint8_t to;
    MoveType type = MoveType::Quiet;
    Piece promotionPiece = PIECE_COUNT;
};

