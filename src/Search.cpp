#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"
#include <algorithm>
#include <limits>

namespace {

int pieceValue(char piece) {
    switch (piece) {
    case 'P':
    case 'p':
        return 100;

    case 'N':
    case 'n':
        return 320;

    case 'B':
    case 'b':
        return 330;

    case 'R':
    case 'r':
        return 500;

    case 'Q':
    case 'q':
        return 900;

    default:
        return 0;
    }
}

int scoreMove(const Board& board, const Move& move) {
    constexpr int pieceValues[6] = {100, 320, 330, 500, 900, 0};

    switch (move.type) {
    case MoveType::PromotionCapture: {
        int victim = pieceValue(board.getPieceAt(move.to));
        int promotion = pieceValues[move.promotionPiece % 6];

        return 30000 + victim * 10 + promotion;
    }

    case MoveType::Promotion: {
        int promotion = pieceValues[move.promotionPiece % 6];

        return 20000 + promotion;
    }

    case MoveType::Capture: {
        int attacker = pieceValue(board.getPieceAt(move.from));
        int victim = pieceValue(board.getPieceAt(move.to));

        return 10000 + victim * 10 - attacker;
    }

    case MoveType::EnPassant:
        return 10900;

    default:
        return 0;
    }
}

int quiescence(Board& board, int alpha, int beta, int ply, std::uint64_t* qNodes) {
    if (qNodes != nullptr) {
        ++(*qNodes);
    }

    constexpr int mateScore = 100000;
    constexpr int maxQuiescencePly = 64;

    bool inCheck = MoveGenerator::isKingInCheck(board, board.isWhiteToMove());

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        if (inCheck) {
            return -mateScore + ply;
        }
        return 0;
    }

    if (!inCheck) {
        int standPat = Evaluation::evaluate(board);

        if (!board.isWhiteToMove()) {
            standPat = -standPat;
        }

        if (standPat >= beta) {
            return standPat;
        }

        if (standPat > alpha) {
            alpha = standPat;
        }
    }

    if (ply >= maxQuiescencePly) {
        if (!inCheck) {
            return alpha;
        }

        int bestScore = std::numeric_limits<int>::min();

        for (int i = 0; i < moves.count; ++i) {
            const Move& move = moves.moves[i];

            UndoState undo;
            board.makeMove(move, undo);

            int score = Evaluation::evaluate(board);

            if (!undo.whiteToMove) {
                score = -score;
            }

            board.unmakeMove(move, undo);

            if (score > bestScore) {
                bestScore = score;
            }
        }

        return bestScore;
    }

    std::sort(moves.moves, moves.moves + moves.count, [&board](const Move& a, const Move& b) {
        return scoreMove(board, a) > scoreMove(board, b);
    });

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];

        if (!inCheck && move.type != MoveType::Capture && move.type != MoveType::PromotionCapture &&
            move.type != MoveType::EnPassant && move.type != MoveType::Promotion) {
            continue;
        }

        UndoState undo;

        board.makeMove(move, undo);

        int score = -quiescence(board, -beta, -alpha, ply + 1, qNodes);

        board.unmakeMove(move, undo);

        if (score >= beta) {
            return score;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

} // namespace

int Search::negamax(Board& board, int depth, int ply, int alpha, int beta, std::uint64_t* nodes, std::uint64_t* qNodes) {
    if (nodes != nullptr) {
        ++(*nodes);
    }

    if (depth <= 0) {
    return quiescence(board, alpha, beta, ply, qNodes);
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

    std::sort(moves.moves, moves.moves + moves.count, [&board](const Move& a, const Move& b) {
        return scoreMove(board, a) > scoreMove(board, b);
    });

    int bestScore = std::numeric_limits<int>::min();

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);
        int score = -negamax(board, depth - 1, ply + 1, -beta, -alpha, nodes, qNodes);
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

    std::sort(moves.moves, moves.moves + moves.count, [&board](const Move& a, const Move& b) {
        return scoreMove(board, a) > scoreMove(board, b);
    });

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
        if (score > alpha) {
            alpha = score;
        }
    }

    return bestMove;
}