#include "Evaluation.h"
#include "BitUtils.h"
#include "Board.h"
#include "MagicBitboards.h"
#include "MoveGenerator.h"

#include <algorithm>

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

// KINGS (ENDGAME)
constexpr int kingEndgameTable[64] = {
    -50, -30, -30, -30, -30, -30, -30, -50, // Rank 1
    -30, -20, -10, 0,   0,   -10, -20, -30, // Rank 2
    -30, -10, 20,  30,  30,  20,  -10, -30, // Rank 3
    -30, -10, 30,  40,  40,  30,  -10, -30, // Rank 4
    -30, -10, 30,  40,  40,  30,  -10, -30, // Rank 5
    -30, -10, 20,  30,  30,  20,  -10, -30, // Rank 6
    -30, -20, -10, 0,   0,   -10, -20, -30, // Rank 7
    -50, -30, -30, -30, -30, -30, -30, -50  // Rank 8
};

constexpr int maxPhase = 24;

// King safety penalties, in middlegame centipawns
constexpr int shieldPawnAdvancedPenalty = 10; // Shield pawn two squares in front of the king
constexpr int shieldPawnMissingPenalty = 20;  // No shield pawn within two squares
constexpr int semiOpenFilePenalty = 10;       // File next to the king without a friendly pawn
constexpr int openFilePenalty = 20;           // File next to the king without any pawn

// Per attacked square around the king
constexpr int knightAttackWeight = 8;
constexpr int bishopAttackWeight = 8;
constexpr int rookAttackWeight = 10;
constexpr int queenAttackWeight = 16;

// Percentage of the attack weight that counts, by number of attackers. A single piece rarely
// mates on its own, so it counts for nothing.
constexpr int attackerScale[8] = {0, 0, 50, 75, 88, 94, 97, 99};

constexpr std::uint64_t fileA = 0x0101010101010101ULL;

int attackPenalty(const Board& board, int kingSquare, bool white) {
    Piece knightPiece = white ? Piece::BLACK_KNIGHT : Piece::WHITE_KNIGHT;
    Piece bishopPiece = white ? Piece::BLACK_BISHOP : Piece::WHITE_BISHOP;
    Piece rookPiece = white ? Piece::BLACK_ROOK : Piece::WHITE_ROOK;
    Piece queenPiece = white ? Piece::BLACK_QUEEN : Piece::WHITE_QUEEN;

    std::uint64_t knights = board.getPieceBitboard(knightPiece);
    std::uint64_t bishops = board.getPieceBitboard(bishopPiece);
    std::uint64_t rooks = board.getPieceBitboard(rookPiece);
    std::uint64_t queens = board.getPieceBitboard(queenPiece);

    std::uint64_t occupied = board.getOccupied();
    std::uint64_t zone = MoveGenerator::getKingAttacks(kingSquare) | (1ULL << kingSquare);

    int attackers = 0;
    int weight = 0;

    while (knights) {
        int square = BitUtils::popLSB(knights);
        int zoneSquares = BitUtils::popcount(MoveGenerator::getKnightAttacks(square) & zone);

        if (zoneSquares > 0) {
            ++attackers;
            weight += zoneSquares * knightAttackWeight;
        }
    }

    while (bishops) {
        int square = BitUtils::popLSB(bishops);
        int zoneSquares =
            BitUtils::popcount(MagicBitboards::getBishopAttacks(square, occupied) & zone);

        if (zoneSquares > 0) {
            ++attackers;
            weight += zoneSquares * bishopAttackWeight;
        }
    }

    while (rooks) {
        int square = BitUtils::popLSB(rooks);
        int zoneSquares =
            BitUtils::popcount(MagicBitboards::getRookAttacks(square, occupied) & zone);

        if (zoneSquares > 0) {
            ++attackers;
            weight += zoneSquares * rookAttackWeight;
        }
    }

    while (queens) {
        int square = BitUtils::popLSB(queens);
        std::uint64_t attacks = MagicBitboards::getBishopAttacks(square, occupied) |
                                MagicBitboards::getRookAttacks(square, occupied);
        int zoneSquares = BitUtils::popcount(attacks & zone);

        if (zoneSquares > 0) {
            ++attackers;
            weight += zoneSquares * queenAttackWeight;
        }
    }

    return weight * attackerScale[std::min(attackers, 7)] / 100;
}

} // namespace

int Evaluation::gamePhase(const Board& board) {
    std::uint64_t minors = board.getPieceBitboard(WHITE_KNIGHT) |
                           board.getPieceBitboard(BLACK_KNIGHT) |
                           board.getPieceBitboard(WHITE_BISHOP) |
                           board.getPieceBitboard(BLACK_BISHOP);
    std::uint64_t rooks = board.getPieceBitboard(WHITE_ROOK) | board.getPieceBitboard(BLACK_ROOK);
    std::uint64_t queens =
        board.getPieceBitboard(WHITE_QUEEN) | board.getPieceBitboard(BLACK_QUEEN);

    int phase = BitUtils::popcount(minors) + 2 * BitUtils::popcount(rooks) +
                4 * BitUtils::popcount(queens);

    // Promotions can push the count past the starting material
    return std::min(phase, maxPhase);
}

int Evaluation::kingSafety(const Board& board, bool white) {
    std::uint64_t king = board.getPieceBitboard(white ? WHITE_KING : BLACK_KING);

    if (king == 0) {
        return 0;
    }

    int kingSquare = BitUtils::lsb(king);
    int kingFile = kingSquare % 8;
    int kingRank = kingSquare / 8;

    std::uint64_t ownPawns = board.getPieceBitboard(white ? WHITE_PAWN : BLACK_PAWN);
    std::uint64_t enemyPawns = board.getPieceBitboard(white ? BLACK_PAWN : WHITE_PAWN);

    // A king in the center is usually waiting to castle, so its pawns are free to advance. A
    // shield is only expected once the king has moved to a wing.
    bool onWing = kingFile <= 2 || kingFile >= 5;

    int oneAhead = white ? kingRank + 1 : kingRank - 1;
    int twoAhead = white ? kingRank + 2 : kingRank - 2;

    // The king's file and the files beside it, moved inward for a king on the edge
    int middleFile = std::clamp(kingFile, 1, 6);

    int penalty = 0;

    for (int file = middleFile - 1; file <= middleFile + 1; ++file) {
        std::uint64_t fileMask = fileA << file;

        if (onWing) {
            bool pawnOneAhead = oneAhead >= 0 && oneAhead < 8 &&
                                BitUtils::getBit(ownPawns, oneAhead * 8 + file);
            bool pawnTwoAhead = twoAhead >= 0 && twoAhead < 8 &&
                                BitUtils::getBit(ownPawns, twoAhead * 8 + file);

            if (!pawnOneAhead) {
                penalty += pawnTwoAhead ? shieldPawnAdvancedPenalty : shieldPawnMissingPenalty;
            }
        }

        if ((ownPawns & fileMask) == 0) {
            penalty += (enemyPawns & fileMask) ? semiOpenFilePenalty : openFilePenalty;
        }
    }

    penalty += attackPenalty(board, kingSquare, white);

    return -penalty;
}

int Evaluation::evaluate(const Board& board) {
    constexpr int pieceValues[6] = {100, 320, 330, 500, 900, 0};
    int score = 0;

    int middlegame = 0;
    int endgame = 0;

    for (int i = WHITE_PAWN; i < PIECE_COUNT; ++i) {
        Piece piece = static_cast<Piece>(i);
        bool isWhite = i < BLACK_PAWN;
        int pieceType = i % 6;

        std::uint64_t pieces = board.getPieceBitboard(piece);

        while (pieces) {
            int square = BitUtils::popLSB(pieces);
            int tableSquare = isWhite ? square : (square ^ 56);

            if (piece == WHITE_KING || piece == BLACK_KING) {
                middlegame += (isWhite ? 1 : -1) * pieceSquareTables[pieceType][tableSquare];
                endgame += (isWhite ? 1 : -1) * kingEndgameTable[tableSquare];
                continue;
            }

            int material = pieceValues[pieceType];
            int positional = pieceSquareTables[pieceType][tableSquare];
            
            score += (isWhite ? 1 : -1) * (material + positional);
            
        }
    }

    middlegame += kingSafety(board, true) - kingSafety(board, false);

    int phase = gamePhase(board);
    score += (middlegame * phase + endgame * (maxPhase - phase)) / maxPhase;

    return score;
}

