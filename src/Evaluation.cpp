#include "Evaluation.h"
#include "BitUtils.h"
#include "Board.h"

int Evaluation::evaluate(const Board& board) {
    constexpr int pieceValues[6] = {100, 320, 330, 500, 900, 0};

    int score = 0;

    for (int i = WHITE_PAWN; i < PIECE_COUNT; i++) {
        std::uint64_t bitboard = board.getPieceBitboard(static_cast<Piece>(i));
        int count = BitUtils::popcount(bitboard);

        if (i < BLACK_PAWN) {
            score += count * pieceValues[i % 6];
        } else {
            score -= count * pieceValues[i % 6];
        }
    }
    return score;
}