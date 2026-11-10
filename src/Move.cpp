#include "Move.h"

#include <stdexcept>

std::string Move::toUCI() const {
    std::string result;

    result += static_cast<char>('a' + (from % 8));
    result += static_cast<char>('1' + (from / 8));

    result += static_cast<char>('a' + (to % 8));
    result += static_cast<char>('1' + (to / 8));

    if (type == MoveType::Promotion ||
        type == MoveType::PromotionCapture) {

        char suffix;

        switch (promotionPiece) {
            case WHITE_QUEEN:
            case BLACK_QUEEN:
                suffix = 'q';
                break;

            case WHITE_ROOK:
            case BLACK_ROOK:
                suffix = 'r';
                break;

            case WHITE_BISHOP:
            case BLACK_BISHOP:
                suffix = 'b';
                break;

            case WHITE_KNIGHT:
            case BLACK_KNIGHT:
                suffix = 'n';
                break;

            default:
                throw std::invalid_argument("Invalid promotion piece");
        }

        result += suffix;
    }

    return result;
}