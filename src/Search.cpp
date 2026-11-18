#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <string>

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

bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.promotionPiece == b.promotionPiece;
}

// No repetition draw was relied on
constexpr int noHistoryPly = std::numeric_limits<int>::max();

// Draws that can be scored before generating moves, since these positions are never checkmate.
// The root is never scored as a draw so it still gets a best move.
bool isDrawnPosition(const Board& board, int ply, int* historyPly) {
    if (ply == 0) {
        return false;
    }

    if (const int distance = board.repetitionDistance(ply)) {
        if (historyPly != nullptr) {
            *historyPly = std::min(*historyPly, ply - distance);
        }
        return true;
    }

    return board.isInsufficientMaterial();
}

// Checkmate takes precedence, so this is only tested once the side to move has a legal move
bool isFiftyMoveDraw(const Board& board, int ply) { return ply > 0 && board.isFiftyMoveRule(); }

int quiescence(Board& board, int alpha, int beta, int ply, std::uint64_t* qNodes, int* historyPly) {
    if (qNodes != nullptr) {
        ++(*qNodes);
    }

    constexpr int mateScore = 100000;
    constexpr int maxQuiescencePly = 64;

    if (isDrawnPosition(board, ply, historyPly)) {
        return 0;
    }

    bool inCheck = MoveGenerator::isKingInCheck(board, board.isWhiteToMove());

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        if (inCheck) {
            return -mateScore + ply;
        }
        return 0;
    }

    if (isFiftyMoveDraw(board, ply)) {
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

        int score = -quiescence(board, -beta, -alpha, ply + 1, qNodes, historyPly);

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

int Search::negamax(Board& board, int depth, int ply, int alpha, int beta, std::uint64_t* nodes,
                    std::uint64_t* qNodes, TranspositionTable* tt, int* historyPly) {

    // Leaf nodes are counted in qNodes by quiescence, so check depth before counting here.
    if (depth <= 0) {
        return quiescence(board, alpha, beta, ply, qNodes, historyPly);
    }

    if (nodes != nullptr) {
        ++(*nodes);
    }

    constexpr int mateScore = 100000;

    // The hash encodes neither the moves that led here nor the halfmove clock. Repetition depends
    // on those moves, so it is tested before the table probe, together with insufficient material,
    // which depends only on the position. Subtrees that may reach the fifty-move limit neither read
    // nor write table scores.
    if (isDrawnPosition(board, ply, historyPly)) {
        return 0;
    }

    const bool useTable = tt != nullptr && board.getHalfmoveClock() + depth < Board::fiftyMoveLimit;

    const int originalAlpha = alpha;
    Move ttMove{};

    if (useTable) {
        int ttScore = 0;

        if (tt->probe(board.getHash(), depth, alpha, beta, ply, ttScore, ttMove)) {
            return ttScore;
        }
    }

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        if (MoveGenerator::isKingInCheck(board, board.isWhiteToMove())) {
            return -mateScore + ply;
        }
        return 0;
    }

    if (isFiftyMoveDraw(board, ply)) {
        return 0;
    }

    std::sort(moves.moves, moves.moves + moves.count, [&board](const Move& a, const Move& b) {
        return scoreMove(board, a) > scoreMove(board, b);
    });

    // Search the stored best move first, keeping the MVV-LVA order of the remaining moves
    for (int i = 0; i < moves.count; ++i) {
        if (sameMove(moves.moves[i], ttMove)) {
            std::rotate(moves.moves, moves.moves + i, moves.moves + i + 1);
            break;
        }
    }

    int bestScore = std::numeric_limits<int>::min();
    Move bestMove = moves.moves[0];

    // Earliest occurrence relied on by repetition draws below this node
    int subtreeHistoryPly = noHistoryPly;

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);
        int score = -negamax(board, depth - 1, ply + 1, -beta, -alpha, nodes, qNodes, tt,
                             &subtreeHistoryPly);
        board.unmakeMove(move, undo);

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    if (historyPly != nullptr) {
        *historyPly = std::min(*historyPly, subtreeHistoryPly);
    }

    // Store only scores whose repetitions lie entirely below this node; those hold along any path
    // here. Repeating this position or one above it depends on how it was reached.
    if (useTable && subtreeHistoryPly > ply) {
        TranspositionTable::Bound bound = TranspositionTable::Bound::Exact;

        if (bestScore <= originalAlpha) {
            bound = TranspositionTable::Bound::Upper;
        } else if (bestScore >= beta) {
            bound = TranspositionTable::Bound::Lower;
        }

        tt->store(board.getHash(), depth, bestScore, bound, bestMove, ply);
    }

    return bestScore;
}

std::optional<Move> Search::findBestMove(Board& board, int maxDepth,
                                         const IterationCallback& onIteration) {
    if (maxDepth <= 0) {
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

    std::optional<Move> bestMove;

    std::uint64_t totalNodes = 0;

    // Owned by this search so it persists across iterations without leaking into other searches
    TranspositionTable tt;

    const auto startTime = std::chrono::steady_clock::now();

    // Iterative deepening
    for (int depth = 1; depth <= maxDepth; ++depth) {

        std::uint64_t nodes = 1; // Count the root
        std::uint64_t qNodes = 0;

        // Search the previous iteration's best move first
        if (bestMove.has_value()) {
            for (int i = 0; i < moves.count; ++i) {
                if (moves.moves[i].toUCI() == bestMove->toUCI()) {
                    std::swap(moves.moves[0], moves.moves[i]);
                    break;
                }
            }
        }

        int alpha = -infinity;
        int beta = infinity;
        int bestScore = -infinity;

        std::optional<Move> iterationBestMove;

        for (int i = 0; i < moves.count; ++i) {
            const Move& move = moves.moves[i];

            UndoState undo;
            board.makeMove(move, undo);

            int score = -negamax(board, depth - 1, 1, -beta, -alpha, &nodes, &qNodes, &tt);

            board.unmakeMove(move, undo);

            if (score > bestScore) {
                bestScore = score;
                iterationBestMove = move;
            }

            if (score > alpha) {
                alpha = score;
            }
        }

        bestMove = iterationBestMove;

        totalNodes += nodes + qNodes;

        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - startTime)
                                   .count();

        const std::uint64_t timeMs = elapsedUs / 1000;

        const std::uint64_t nps = elapsedUs > 0 ? totalNodes * 1000000ULL / elapsedUs : 0;

        if (bestMove.has_value() && onIteration) {
            onIteration(IterationInfo{depth, *bestMove, bestScore, totalNodes, timeMs, nps,
                                      tt.getHits(), tt.getCutoffs()});
        }
    }

    return bestMove;
}