#pragma once

#include "Board.h"

class Evaluation {
public:
    static int evaluate(const Board& board);

    // Non-pawn material, from 0 (pawn endgame) to 24 (every piece on the board)
    static int gamePhase(const Board& board);

    // One side's middlegame king safety: 0 when sheltered, negative when weakened or attacked
    static int kingSafety(const Board& board, bool white);
};