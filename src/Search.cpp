#include "Search.h"
#include "BitUtils.h"
#include "Evaluation.h"
#include "MoveGenerator.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

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

static_assert(TranspositionTable::fiftyMoveLimit == Board::fiftyMoveLimit);

// No draw was relied on
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

// Checkmate takes precedence, so this is only tested once the side to move has a legal move.
// The clock is not hashed, so the draw relies on every position since the last capture or pawn
// move, which may precede the root.
bool isFiftyMoveDraw(const Board& board, int ply, int* clockPly) {
    if (ply == 0 || !board.isFiftyMoveRule()) {
        return false;
    }

    if (clockPly != nullptr) {
        *clockPly = std::min(*clockPly, ply - board.getHalfmoveClock());
    }

    return true;
}

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

// Knights, bishops, rooks and queens of the side to move
int pieceCount(const Board& board) {
    const bool white = board.isWhiteToMove();

    return BitUtils::popcount(board.getPieceBitboard(white ? WHITE_KNIGHT : BLACK_KNIGHT) |
                              board.getPieceBitboard(white ? WHITE_BISHOP : BLACK_BISHOP) |
                              board.getPieceBitboard(white ? WHITE_ROOK : BLACK_ROOK) |
                              board.getPieceBitboard(white ? WHITE_QUEEN : BLACK_QUEEN));
}

// Null move pruning assumes passing is worse than any move. The node must have legal moves, since a
// pass would hide a stalemate.
bool canTryNullMove(const Board& board, int depth, int alpha, int beta, bool afterNullMove,
                    const Search::Control* control) {
    constexpr int mateScore = 100000;
    constexpr int mateThreshold = mateScore - 1000;

    if (control == nullptr || !control->nullMovePruning || afterNullMove ||
        depth < Search::nullMoveMinDepth) {
        return false;
    }

    // PV nodes need exact scores, and passing cannot prove a mate
    if (beta - alpha > 1 || std::abs(beta) >= mateThreshold) {
        return false;
    }

    // The null move restarts the halfmove clock, so its search would miss fifty-move draws that
    // this node's search can reach
    if (board.getHalfmoveClock() + depth >= Board::fiftyMoveLimit) {
        return false;
    }

    // A pass is illegal in check. Zugzwang, where it would beat every move, is common when the side
    // to move has at most one piece, whose spare moves run out, as with a pinned rook in a rook
    // ending.
    if (pieceCount(board) < 2 || MoveGenerator::isKingInCheck(board, board.isWhiteToMove())) {
        return false;
    }

    // From below beta a null move rarely fails high, so it is not worth its search
    const int staticEval = Evaluation::evaluate(board);

    return (board.isWhiteToMove() ? staticEval : -staticEval) >= beta;
}

// Nodes in check reduce nothing: every evasion answers a threat, and so few moves give their order
// little meaning
bool canReduceLateMoves(const Board& board, int depth, const Search::Control* control) {
    return control != nullptr && control->lateMoveReductions &&
           depth >= Search::lateMoveReductionMinDepth &&
           !MoveGenerator::isKingInCheck(board, board.isWhiteToMove());
}

// Whether the move just made may be searched at reduced depth if it is ordered late. Captures and
// promotions change the material, checks force the reply and killers have caused cutoffs at this
// ply, so only the other quiet moves are reduced. The table move is always searched first.
bool canReduceMove(const Board& board, const Move& move, const std::array<Move, 2>& killers) {
    return isQuiet(move) && !sameMove(move, killers[0]) && !sameMove(move, killers[1]) &&
           !MoveGenerator::isKingInCheck(board, board.isWhiteToMove());
}

int quiescence(Board& board, int alpha, int beta, int ply, std::uint64_t* qNodes, int* historyPly,
               Search::Control* control, int* clockPly) {
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

    if (isFiftyMoveDraw(board, ply, clockPly)) {
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

        int score =
            -quiescence(board, -beta, -alpha, ply + 1, qNodes, historyPly, control, clockPly);

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

int Search::lateMoveReduction(int depth, int moveNumber, bool pvNode) {
    constexpr int tableSize = 64;

    // round(ln(depth) * ln(moveNumber) / 3): one ply for most late moves, two from about the
    // tenth move at depth 8, and at most six at the deepest nodes
    static const auto table = [] {
        std::array<std::array<int, tableSize>, tableSize> reductions{};

        for (int d = 1; d < tableSize; ++d) {
            for (int m = 1; m < tableSize; ++m) {
                reductions[d][m] = static_cast<int>(0.5 + std::log(d) * std::log(m) / 3.0);
            }
        }

        return reductions;
    }();

    if (depth < lateMoveReductionMinDepth || moveNumber <= lateMoveReductionMinMoves) {
        return 0;
    }

    // The reduced search keeps at least one ply before quiescence
    const int reduction = std::min(
        table[std::min(depth, tableSize - 1)][std::min(moveNumber, tableSize - 1)], depth - 2);

    // PV nodes decide the score reported for the root, so their moves are reduced one ply less
    return pvNode ? std::max(reduction - 1, 0) : reduction;
}

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
                    Control* control, MoveOrdering* ordering, int* clockPly, bool afterNullMove) {

    // Leaf nodes are counted in qNodes by quiescence, so check depth before counting here.
    if (depth <= 0) {
        return quiescence(board, alpha, beta, ply, qNodes, historyPly, control, clockPly);
    }

    if (shouldStop(control)) {
        return 0;
    }

    if (nodes != nullptr) {
        ++(*nodes);
    }

    constexpr int mateScore = 100000;
    constexpr int mateThreshold = mateScore - 1000;

    // The hash encodes neither the moves that led here nor the halfmove clock. Repetition depends
    // on those moves, so it is tested before the table probe, together with insufficient material,
    // which depends only on the position. The table decides from the clock which scores still
    // hold here; the stored move is used for ordering regardless.
    if (isDrawnPosition(board, ply, historyPly)) {
        return 0;
    }

    const int originalAlpha = alpha;
    Move ttMove{};

    if (tt != nullptr) {
        int ttScore = 0;
        bool clockDependent = false;

        if (tt->probe(board.getHash(), depth, alpha, beta, ply, ttScore, ttMove,
                      board.getHalfmoveClock(), &clockDependent)) {
            // A score that holds only at this clock relies on every position since the last
            // capture or pawn move, just as the fifty-move draw it was searched with did
            if (clockDependent && clockPly != nullptr) {
                *clockPly = std::min(*clockPly, ply - board.getHalfmoveClock());
            }

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

    if (isFiftyMoveDraw(board, ply, clockPly)) {
        return 0;
    }

    if (canTryNullMove(board, depth, alpha, beta, afterNullMove, control)) {
        ++control->nullMoveTries;

        UndoState undo;
        board.makeNullMove(undo);

        // The null move restarts the halfmove clock, so no draw in its search relies on a position
        // at or above this node, and there is nothing to report to the callers
        const int nullScore =
            -negamax(board, depth - 1 - nullMoveReduction, ply + 1, -beta, -beta + 1, nodes, qNodes,
                     tt, nullptr, control, ordering, nullptr, true);

        board.unmakeNullMove(undo);

        if (wasStopped(control)) {
            return 0;
        }

        // The cutoff is not stored in the table: it is a guess, not the result of a search of
        // this node's moves
        if (nullScore >= beta) {
            ++control->nullMoveCutoffs;

            // A mate found after passing is unproven, and its distance counts the pass
            return nullScore >= mateThreshold ? beta : nullScore;
        }
    }

    orderMoves(board, moves, ordering != nullptr ? &ordering->history : nullptr);

    const bool useKillers = ordering != nullptr && ply < maxSearchDepth;

    // Only this node stores killers for its ply, so they stay the same while its moves are searched
    const std::array<Move, 2> killers = useKillers ? ordering->killers[ply] : std::array<Move, 2>{};

    if (useKillers) {
        orderKillers(moves, killers);
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

    // Earliest positions relied on by repetition and fifty-move draws below this node
    int subtreeHistoryPly = noHistoryPly;
    int subtreeClockPly = noHistoryPly;

    // Decided from the window this node was called with, since alpha rises as moves are searched
    const bool pvNode = beta - alpha > 1;
    const bool reduceLateMoves = canReduceLateMoves(board, depth, control);

    // Searches the move just made to newDepth with the window (low, high), from this node's point
    // of view
    auto searchMove = [&](int newDepth, int low, int high, int* moveHistoryPly, int* moveClockPly) {
        return -negamax(board, newDepth, ply + 1, -high, -low, nodes, qNodes, tt, moveHistoryPly,
                        control, ordering, moveClockPly);
    };

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);

        // Draws relied on by the search whose score is kept
        int moveHistoryPly = noHistoryPly;
        int moveClockPly = noHistoryPly;
        int score = 0;

        // Principal variation search: the first move gets the full window. Later moves only need
        // to be shown no better than alpha, which a null window does more cheaply; a move that
        // beats alpha without reaching beta is searched again with the full window for its score.
        //
        // Late move reductions shorten that null window search for quiet moves ordered late. Only
        // a reduced search that fails low is kept: one that beats alpha is repeated at full depth
        // before it can raise alpha, cause a cutoff or be searched with the full window.
        if (i == 0) {
            score = searchMove(depth - 1, alpha, beta, &moveHistoryPly, &moveClockPly);
        } else {
            const int reduction = reduceLateMoves && canReduceMove(board, move, killers)
                                      ? lateMoveReduction(depth, i + 1, pvNode)
                                      : 0;

            if (reduction > 0) {
                ++control->reducedSearches;
            }

            score =
                searchMove(depth - 1 - reduction, alpha, alpha + 1, &moveHistoryPly, &moveClockPly);

            if (reduction > 0 && score > alpha && !wasStopped(control)) {
                ++control->reducedResearches;
                moveHistoryPly = noHistoryPly;
                moveClockPly = noHistoryPly;
                score = searchMove(depth - 1, alpha, alpha + 1, &moveHistoryPly, &moveClockPly);
            }

            if (score > alpha && score < beta && !wasStopped(control)) {
                moveHistoryPly = noHistoryPly;
                moveClockPly = noHistoryPly;
                score = searchMove(depth - 1, alpha, beta, &moveHistoryPly, &moveClockPly);
            }
        }

        board.unmakeMove(move, undo);

        if (wasStopped(control)) {
            return 0;
        }

        subtreeHistoryPly = std::min(subtreeHistoryPly, moveHistoryPly);
        subtreeClockPly = std::min(subtreeClockPly, moveClockPly);

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

    if (clockPly != nullptr) {
        *clockPly = std::min(*clockPly, subtreeClockPly);
    }

    // Store only scores whose repetitions lie entirely below this node; those hold along any path
    // here. Repeating this position or one above it depends on how it was reached. A fifty-move
    // draw counted from a capture or pawn move at or above this node depends on its clock, so
    // such a score is stored for that clock only.
    if (tt != nullptr && subtreeHistoryPly > ply) {
        TranspositionTable::Bound bound = TranspositionTable::Bound::Exact;

        if (bestScore <= originalAlpha) {
            bound = TranspositionTable::Bound::Upper;
        } else if (bestScore >= beta) {
            bound = TranspositionTable::Bound::Lower;
        }

        const int clock =
            subtreeClockPly <= ply ? board.getHalfmoveClock() : TranspositionTable::noClock;

        tt->store(board.getHash(), depth, bestScore, bound, bestMove, ply, clock);
    }

    return bestScore;
}

namespace {

struct WindowResult {
    int score;
    int moveIndex;
};

// Searches the root moves once with the window (alpha, beta), fail-soft like negamax. The root does
// not read the stop condition itself; the nodes below it read it every stopCheckInterval nodes,
// which bounds the stop latency and lets a stop primed through Control::checkCounter land on any
// node.
WindowResult searchRootWindow(Board& board, const MoveList& moves, int depth, int alpha, int beta,
                              std::uint64_t* nodes, std::uint64_t* qNodes, TranspositionTable* tt,
                              Search::Control* control, Search::MoveOrdering* ordering) {
    if (nodes != nullptr) {
        ++(*nodes); // Count the root
    }

    WindowResult result{std::numeric_limits<int>::min(), 0};

    for (int i = 0; i < moves.count; ++i) {
        const Move& move = moves.moves[i];

        UndoState undo;
        board.makeMove(move, undo);

        auto searchMove = [&](int low, int high) {
            return -Search::negamax(board, depth - 1, 1, -high, -low, nodes, qNodes, tt, nullptr,
                                    control, ordering);
        };

        // Principal variation search, as in negamax
        int score = 0;

        if (i == 0) {
            score = searchMove(alpha, beta);
        } else {
            score = searchMove(alpha, alpha + 1);

            if (score > alpha && score < beta && !wasStopped(control)) {
                score = searchMove(alpha, beta);
            }
        }

        board.unmakeMove(move, undo);

        if (wasStopped(control)) {
            break;
        }

        if (score > result.score) {
            result = WindowResult{score, i};
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    return result;
}

} // namespace

std::optional<Search::RootResult> Search::searchRoot(Board& board, MoveList& moves, int depth,
                                                     std::optional<int> guess, std::uint64_t* nodes,
                                                     std::uint64_t* qNodes, TranspositionTable* tt,
                                                     Control* control, MoveOrdering* ordering) {
    constexpr int infinity = 1000000;
    constexpr int mateScore = 100000;
    constexpr int mateThreshold = mateScore - 1000;

    if (moves.count == 0) {
        return std::nullopt;
    }

    RootResult result{};
    int delta = aspirationWindow;
    int alpha = -infinity;
    int beta = infinity;

    // Narrow bounds stay strictly between the mate scores, so a mate score is only ever found
    // with that side of the window fully open
    if (guess.has_value() && *guess > delta - mateThreshold && *guess < mateThreshold - delta) {
        alpha = *guess - delta;
        beta = *guess + delta;
        result.aspiration = true;
    }

    while (true) {
        const WindowResult window = searchRootWindow(board, moves, depth, alpha, beta, nodes,
                                                     qNodes, tt, control, ordering);

        // Not even a bound is reported, so the last completed iteration stands
        if (wasStopped(control)) {
            return std::nullopt;
        }

        // Each fail moves one bound past the failed score by a doubling margin, so the window
        // becomes full after a bounded number of searches
        delta = std::min(delta * 2, infinity);

        if (window.score <= alpha) {
            ++result.failLows;
            alpha = window.score - delta > -mateThreshold ? window.score - delta : -infinity;
        } else if (window.score >= beta) {
            ++result.failHighs;
            beta = window.score + delta < mateThreshold ? window.score + delta : infinity;

            // Search the move that failed high first
            std::swap(moves.moves[0], moves.moves[window.moveIndex]);
        } else {
            // Search the best move first in the next iteration
            std::swap(moves.moves[0], moves.moves[window.moveIndex]);

            result.bestMove = moves.moves[0];
            result.score = window.score;
            return result;
        }
    }
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

    // Played if the search is stopped before its first iteration completes
    const Move fallbackMove = moves.moves[0];

    // Best move and score of the last completed iteration
    std::optional<Move> bestMove;
    std::optional<int> previousScore;

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
    control.nullMovePruning = limits.nullMovePruning;
    control.lateMoveReductions = limits.lateMoveReductions;

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

        std::uint64_t nodes = 0;
        std::uint64_t qNodes = 0;

        // Early scores change too much between depths for a narrow window to pay off
        const std::optional<int> guess = depth >= aspirationMinDepth ? previousScore : std::nullopt;

        const std::optional<RootResult> root =
            searchRoot(board, moves, depth, guess, &nodes, &qNodes, tt, &control, &ordering);

        // An interrupted iteration may not have searched its best move, so it is discarded
        if (!root.has_value()) {
            break;
        }

        bestMove = root->bestMove;
        previousScore = root->score;

        totalNodes += nodes + qNodes;

        const auto elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - startTime).count();

        const std::uint64_t timeMs = elapsedUs / 1000;

        const std::uint64_t nps = elapsedUs > 0 ? totalNodes * 1000000ULL / elapsedUs : 0;

        if (bestMove.has_value() && onIteration) {
            onIteration(IterationInfo{
                depth, *bestMove, root->score, totalNodes, timeMs, nps, tt->getHits() - initialHits,
                tt->getCutoffs() - initialCutoffs, control.nullMoveTries, control.nullMoveCutoffs,
                root->aspiration, root->failHighs, root->failLows, control.reducedSearches,
                control.reducedResearches});
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