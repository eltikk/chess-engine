#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"

#include <limits>

int Search::negamax(Board& board, int depth, int ply) {
    constexpr int mateScore = 100000;

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        if (MoveGenerator::isKingInCheck(board, board.isWhiteToMove())) {
            return -mateScore + ply;
        }
        return 0;
    }

    if (depth <= 0) {
        int score = Evaluation::evaluate(board);
        return board.isWhiteToMove() ? score : -score;
    }

    int bestScore = std::numeric_limits<int>::min();

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);
        int score = -negamax(board, depth - 1, ply + 1);
        board.unmakeMove(move, undo);

        if (score > bestScore) {
            bestScore = score;
        }
    }

    return bestScore;
}