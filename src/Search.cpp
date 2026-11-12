#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"

#include <limits>

int Search::negamax(Board& board, int depth, int ply, int alpha, int beta, std::uint64_t* nodes) {
    if (nodes != nullptr) {
        ++(*nodes);
    }

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
        int score = -negamax(board, depth - 1, ply + 1, -beta, -alpha, nodes);
        board.unmakeMove(move, undo);

        if (score > bestScore) {
            bestScore = score;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    return bestScore;
}

std::optional<Move> Search::findBestMove(Board& board, int depth) {
    if (depth <= 0) {
        return std::nullopt;
    }

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        return std::nullopt;
    }

    constexpr int infinity = 1000000;

    int alpha = -infinity;
    int beta = infinity;

    int bestScore = std::numeric_limits<int>::min();
    std::optional<Move> bestMove;

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);
        int score = -negamax(board, depth - 1, 1, -beta, -alpha);
        board.unmakeMove(move, undo);

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
    }

    return bestMove;
}