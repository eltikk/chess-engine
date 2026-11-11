#include "Evaluation.h"
#include "BitUtils.h"
#include "Board.h"

namespace {

constexpr int pieceSquareTables[6][64] = {

    // 0 - PAWNS
    {
        0,  0,  0,  0,  0,  0,  0,  0,  // Rank 1
        0,  0,  0,  0,  0,  0,  0,  0,  // Rank 2
        0,  0,  5,  10, 10, 5,  0,  0,  // Rank 3
        0,  0,  10, 20, 20, 10, 0,  0,  // Rank 4
        5,  5,  15, 25, 25, 15, 5,  5,  // Rank 5
        10, 10, 20, 30, 30, 20, 10, 10, // Rank 6
        30, 30, 30, 35, 35, 30, 30, 30, // Rank 7
        0,  0,  0,  0,  0,  0,  0,  0   // Rank 8
    },

    // 1 - KNIGHTS
    {
        -50, -40, -30, -30, -30, -30, -40, -50, // Rank 1
        -40, -20, 0,   5,   5,   0,   -20, -40, // Rank 2
        -30, 0,   10,  15,  15,  10,  0,   -30, // Rank 3
        -30, 5,   15,  20,  20,  15,  5,   -30, // Rank 4
        -30, 5,   15,  20,  20,  15,  5,   -30, // Rank 5
        -30, 0,   10,  15,  15,  10,  0,   -30, // Rank 6
        -40, -20, 0,   5,   5,   0,   -20, -40, // Rank 7
        -50, -40, -30, -30, -30, -30, -40, -50  // Rank 8
    },

    // 2 - BISHOPS
    {
        -20, -10, -10, -10, -10, -10, -10, -20, // Rank 1
        -10, 5,   0,   0,   0,   0,   5,   -10, // Rank 2
        -10, 10,  10,  10,  10,  10,  10,  -10, // Rank 3
        -10, 0,   10,  10,  10,  10,  0,   -10, // Rank 4
        -10, 5,   5,   10,  10,  5,   5,   -10, // Rank 5
        -10, 0,   5,   10,  10,  5,   0,   -10, // Rank 6
        -10, 0,   0,   0,   0,   0,   0,   -10, // Rank 7
        -20, -10, -10, -10, -10, -10, -10, -20  // Rank 8
    },

    // 3 - ROOKS
    {
        0,  0,  0,  5,  5,  0,  0,  0,  // Rank 1
        -5, 0,  0,  0,  0,  0,  0,  -5, // Rank 2
        -5, 0,  0,  0,  0,  0,  0,  -5, // Rank 3
        -5, 0,  0,  0,  0,  0,  0,  -5, // Rank 4
        -5, 0,  0,  0,  0,  0,  0,  -5, // Rank 5
        -5, 0,  0,  0,  0,  0,  0,  -5, // Rank 6
        5,  10, 10, 10, 10, 10, 10, 5,  // Rank 7
        0,  0,  0,  0,  0,  0,  0,  0   // Rank 8
    },

    // 4 - QUEENS
    {
        -20, -10, -10, -5, -5, -10, -10, -20, // Rank 1
        -10, 0,   0,   0,  0,  0,   0,   -10, // Rank 2
        -10, 0,   5,   5,  5,  5,   0,   -10, // Rank 3
        -5,  0,   5,   10, 10, 5,   0,   -5,  // Rank 4
        -5,  0,   5,   10, 10, 5,   0,   -5,  // Rank 5
        -10, 0,   5,   5,  5,  5,   0,   -10, // Rank 6
        -10, 0,   0,   0,  0,  0,   0,   -10, // Rank 7
        -20, -10, -10, -5, -5, -10, -10, -20  // Rank 8
    },

    // 5 - KINGS (MIDDLEGAME)
    {
        20,  30,  10,  0,   0,   10,  30,  20,  // Rank 1
        0,   0,   -10, -20, -20, -10, 0,   0,   // Rank 2
        -20, -20, -30, -40, -40, -30, -20, -20, // Rank 3
        -30, -30, -40, -50, -50, -40, -30, -30, // Rank 4
        -30, -30, -40, -50, -50, -40, -30, -30, // Rank 5
        -30, -30, -40, -50, -50, -40, -30, -30, // Rank 6
        -40, -40, -40, -50, -50, -40, -40, -40, // Rank 7
        -40, -40, -40, -50, -50, -40, -40, -40  // Rank 8
    }

};

} // namespace

int Evaluation::evaluate(const Board& board) {
    constexpr int pieceValues[6] = {100, 320, 330, 500, 900, 0};
    int score = 0;

    for (int i = WHITE_PAWN; i < PIECE_COUNT; ++i) {
        Piece piece = static_cast<Piece>(i);
        bool isWhite = i < BLACK_PAWN;
        int pieceType = i % 6;

        std::uint64_t pieces = board.getPieceBitboard(piece);

        while (pieces) {
            int square = BitUtils::popLSB(pieces);
            int tableSquare = isWhite ? square : (square ^ 56);

            int material = pieceValues[pieceType];
            int positional = pieceSquareTables[pieceType][tableSquare];
            
            score += (isWhite ? 1 : -1) * (material + positional);
            
        }
    }

    return score;
}

