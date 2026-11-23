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

int scoreMove(const Board& board, const Move& move, const Search::HistoryTable* history) {
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
        return history != nullptr ? (*history)[board.isWhiteToMove()][move.from][move.to] : 0;
    }
}

static_assert(Search::MoveOrdering::maxHistory < 10000, "history must rank below captures");

struct ScoredMove {
    int score;
    Move move;
};

void orderMoves(const Board& board, MoveList& moves,
                const Search::HistoryTable* history = nullptr) {
    ScoredMove scored[MoveList::capacity];

    for (int i = 0; i < moves.count; ++i) {
        scored[i] = ScoredMove{scoreMove(board, moves.moves[i], history), moves.moves[i]};
    }

    std::sort(scored, scored + moves.count,
              [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

    for (int i = 0; i < moves.count; ++i) {
        moves.moves[i] = scored[i].move;
    }
}

bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.promotionPiece == b.promotionPiece;
}

bool isQuiet(const Move& move) {
    return move.type == MoveType::Quiet || move.type == MoveType::DoublePawnPush ||
           move.type == MoveType::KingCastle || move.type == MoveType::QueenCastle;
}

// Moves the killers to the front of the quiet moves, which follow the captures and promotions
void orderKillers(MoveList& moves, const std::array<Move, 2>& killers) {
    int next = 0;

    while (next < moves.count && !isQuiet(moves.moves[next])) {
        ++next;
    }

    for (const Move& killer : killers) {
        for (int i = next; i < moves.count; ++i) {
            if (sameMove(moves.moves[i], killer)) {
                std::rotate(moves.moves + next, moves.moves + i, moves.moves + i + 1);
                ++next;
                break;
            }
        }
    }
}

void storeKiller(std::array<Move, 2>& killers, const Move& move) {
    if (!sameMove(move, killers[0])) {
        killers[1] = killers[0];
        killers[0] = move;
    }
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

bool checkStop(Search::Control& control) {
    if (!control.stopped) {
        control.stopped = (control.stopRequested != nullptr &&
                           control.stopRequested->load(std::memory_order_relaxed)) ||
                          Search::Clock::now() >= control.hardDeadline;
    }

    return control.stopped;
}

// Called at every node; the flag and clock are only read once per stopCheckInterval nodes
bool shouldStop(Search::Control* control) {
    if (control == nullptr) {
        return false;
    }

    if (control->stopped) {
        return true;
    }

    if (++control->checkCounter % Search::stopCheckInterval != 0) {
        return false;
    }

    return checkStop(*control);
}

bool wasStopped(const Search::Control* control) { return control != nullptr && control->stopped; }

int quiescence(Board& board, int alpha, int beta, int ply, std::uint64_t* qNodes, int* historyPly,
               Search::Control* control) {
    if (shouldStop(control)) {
        return 0;
    }

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

    if (!inCheck) {
        int tacticalCount = 0;

        for (int i = 0; i < moves.count; ++i) {
            if (!isQuiet(moves.moves[i])) {
                moves.moves[tacticalCount++] = moves.moves[i];
            }
        }

        moves.count = tacticalCount;
    }

    orderMoves(board, moves);

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];

        UndoState undo;

        board.makeMove(move, undo);

        int score = -quiescence(board, -beta, -alpha, ply + 1, qNodes, historyPly, control);

        board.unmakeMove(move, undo);

        if (wasStopped(control)) {
            return 0;
        }

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

void Search::MoveOrdering::updateHistory(bool whiteToMove, const Move& move, int depth) {
    static_assert(maxSearchDepth * maxSearchDepth <= maxHistory);

    int& score = history[whiteToMove][move.from][move.to];
    const int clampedDepth = std::clamp(depth, 0, maxSearchDepth);
    const int bonus = clampedDepth * clampedDepth;

    // Approaches maxHistory without passing it, however often the move is rewarded
    score += bonus - score * bonus / maxHistory;
}

int Search::negamax(Board& board, int depth, int ply, int alpha, int beta, std::uint64_t* nodes,
                    std::uint64_t* qNodes, TranspositionTable* tt, int* historyPly,
                    Control* control, MoveOrdering* ordering) {

    // Leaf nodes are counted in qNodes by quiescence, so check depth before counting here.
    if (depth <= 0) {
        return quiescence(board, alpha, beta, ply, qNodes, historyPly, control);
    }

    if (shouldStop(control)) {
        return 0;
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

    orderMoves(board, moves, ordering != nullptr ? &ordering->history : nullptr);

    const bool useKillers = ordering != nullptr && ply < maxSearchDepth;

    if (useKillers) {
        orderKillers(moves, ordering->killers[ply]);
    }

    // Search the stored best move first, keeping the order of the remaining moves
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
                             &subtreeHistoryPly, control, ordering);
        board.unmakeMove(move, undo);

        if (wasStopped(control)) {
            return 0;
        }

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            if (ordering != nullptr && isQuiet(move)) {
                if (useKillers) {
                    storeKiller(ordering->killers[ply], move);
                }

                ordering->updateHistory(board.isWhiteToMove(), move, depth);
            }

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

    Limits limits;
    limits.depth = maxDepth;

    return findBestMove(board, limits, nullptr, onIteration);
}

std::optional<Move> Search::findBestMove(Board& board, const Limits& limits,
                                         const std::atomic<bool>* stopRequested,
                                         const IterationCallback& onIteration,
                                         TranspositionTable* tt) {
    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    if (moves.count == 0) {
        return std::nullopt;
    }

    orderMoves(board, moves);

    constexpr int infinity = 1000000;

    // Played if the search is stopped before its first iteration completes
    const Move fallbackMove = moves.moves[0];

    // Best move of the last completed iteration
    std::optional<Move> bestMove;

    std::uint64_t totalNodes = 0;

    std::optional<TranspositionTable> ownTable;

    if (tt == nullptr) {
        tt = &ownTable.emplace();
    }

    // The table may hold counts from earlier searches
    const std::uint64_t initialHits = tt->getHits();
    const std::uint64_t initialCutoffs = tt->getCutoffs();

    // Owned by this search so it persists across iterations without leaking into other searches
    MoveOrdering ordering;

    Control control;
    control.stopRequested = stopRequested;
    control.hardDeadline = limits.hardDeadline;

    const auto startTime = Clock::now();

    // Iterative deepening
    for (int depth = 1; depth <= limits.depth; ++depth) {
        if (checkStop(control)) {
            break;
        }

        // The next iteration would most likely be interrupted, so it is not started
        if (depth > 1 && Clock::now() >= limits.softDeadline) {
            break;
        }

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
            if (checkStop(control)) {
                break;
            }

            const Move& move = moves.moves[i];

            UndoState undo;
            board.makeMove(move, undo);

            int score = -negamax(board, depth - 1, 1, -beta, -alpha, &nodes, &qNodes, tt, nullptr,
                                 &control, &ordering);

            board.unmakeMove(move, undo);

            if (control.stopped) {
                break;
            }

            if (score > bestScore) {
                bestScore = score;
                iterationBestMove = move;
            }

            if (score > alpha) {
                alpha = score;
            }
        }

        // An interrupted iteration may not have searched its best move, so it is discarded
        if (control.stopped) {
            break;
        }

        bestMove = iterationBestMove;

        totalNodes += nodes + qNodes;

        const auto elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - startTime).count();

        const std::uint64_t timeMs = elapsedUs / 1000;

        const std::uint64_t nps = elapsedUs > 0 ? totalNodes * 1000000ULL / elapsedUs : 0;

        if (bestMove.has_value() && onIteration) {
            onIteration(IterationInfo{depth, *bestMove, bestScore, totalNodes, timeMs, nps,
                                      tt->getHits() - initialHits,
                                      tt->getCutoffs() - initialCutoffs});
        }
    }

    return bestMove.value_or(fallbackMove);
}

std::int64_t Search::allocateTimeMs(std::int64_t timeMs, std::int64_t incMs,
                                    std::int64_t movesToGo) {
    constexpr int defaultMovesToGo = 30;

    timeMs = std::max<std::int64_t>(timeMs, 0);
    incMs = std::max<std::int64_t>(incMs, 0);

    if (movesToGo <= 0) {
        movesToGo = defaultMovesToGo;
    }

    const std::int64_t target = timeMs / movesToGo + incMs * 3 / 4;
    const std::int64_t available = timeMs - moveOverheadMs;

    return std::max<std::int64_t>(std::min(target, available), 1);
}