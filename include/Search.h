#pragma once

#include "Board.h"

class Search {
public:
    static int negamax(Board& board, int depth, int ply = 0);
};