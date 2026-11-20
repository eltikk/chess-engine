#include "Zobrist.h"
#include "Board.h"

#include <array>
#include <bit>

namespace {

std::uint64_t splitmix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;

    std::uint64_t z = state;

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

    return z ^ (z >> 31);
}

struct ZobristKeys {
    std::array<std::array<std::uint64_t, 64>, PIECE_COUNT> pieces{};
    std::array<std::uint64_t, 16> castling{};
    std::array<std::uint64_t, 8> enPassant{};
    std::uint64_t sideToMove{};
};

const ZobristKeys& getKeys() {
    static const ZobristKeys keys = [] {
        ZobristKeys result{};

        std::uint64_t seed = 0x123456789ABCDEF0ULL;

        for (auto& piece : result.pieces) {
            for (auto& key : piece) {
                key = splitmix64(seed);
            }
        }

        for (auto& key : result.castling) {
            key = splitmix64(seed);
        }

        for (auto& key : result.enPassant) {
            key = splitmix64(seed);
        }

        result.sideToMove = splitmix64(seed);

        return result;
    }();

    return keys;
}

bool pieceOn(std::uint64_t pieces, int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8 && ((pieces >> (rank * 8 + file)) & 1ULL);
}

// Whether the first piece along a ray from the square is one of the sliders
bool sliderOnRay(int file, int rank, int fileStep, int rankStep, std::uint64_t occupied,
                 std::uint64_t sliders) {
    for (file += fileStep, rank += rankStep; file >= 0 && file < 8 && rank >= 0 && rank < 8;
         file += fileStep, rank += rankStep) {
        if (pieceOn(occupied, file, rank)) {
            return pieceOn(sliders, file, rank);
        }
    }

    return false;
}

// Whether an en passant capture leaves the mover's king safe. Walks the board directly rather than
// using move generation, which calls back into the hash, or attack tables, which would make the
// hash depend on whether they were initialized.
bool enPassantKeepsKingSafe(const Board& board, int from, int to, int capturedSquare) {
    const bool white = board.isWhiteToMove();
    const std::uint64_t king = board.getPieceBitboard(white ? WHITE_KING : BLACK_KING);

    if (king == 0) {
        return true;
    }

    const int kingSquare = std::countr_zero(king);
    const int kingFile = kingSquare % 8;
    const int kingRank = kingSquare / 8;

    // Both pawns leave their squares and the capturing pawn lands on the target square
    const std::uint64_t occupied =
        (board.getOccupied() & ~(1ULL << from) & ~(1ULL << capturedSquare)) | (1ULL << to);

    // Exclude the captured pawn when checking whether en passant leaves the king in check
    const std::uint64_t pawns =
        board.getPieceBitboard(white ? BLACK_PAWN : WHITE_PAWN) & ~(1ULL << capturedSquare);
    const std::uint64_t knights = board.getPieceBitboard(white ? BLACK_KNIGHT : WHITE_KNIGHT);
    const std::uint64_t queens = board.getPieceBitboard(white ? BLACK_QUEEN : WHITE_QUEEN);
    const std::uint64_t diagonal =
        board.getPieceBitboard(white ? BLACK_BISHOP : WHITE_BISHOP) | queens;
    const std::uint64_t straight = board.getPieceBitboard(white ? BLACK_ROOK : WHITE_ROOK) | queens;

    // Enemy pawns attack from the rank in front of the king
    const int pawnRank = white ? kingRank + 1 : kingRank - 1;

    if (pieceOn(pawns, kingFile - 1, pawnRank) || pieceOn(pawns, kingFile + 1, pawnRank)) {
        return false;
    }

    constexpr int knightSteps[8][2] = {{1, 2},   {2, 1},   {2, -1}, {1, -2},
                                       {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};

    for (const auto& step : knightSteps) {
        if (pieceOn(knights, kingFile + step[0], kingRank + step[1])) {
            return false;
        }
    }

    constexpr int straightSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    constexpr int diagonalSteps[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for (int i = 0; i < 4; ++i) {
        if (sliderOnRay(kingFile, kingRank, straightSteps[i][0], straightSteps[i][1], occupied,
                        straight) ||
            sliderOnRay(kingFile, kingRank, diagonalSteps[i][0], diagonalSteps[i][1], occupied,
                        diagonal)) {
            return false;
        }
    }

    return true;
}

} // namespace

std::uint64_t Zobrist::compute(const Board& board) {
    const auto& keys = getKeys();
    std::uint64_t hash = 0;

    for (int p = 0; p < PIECE_COUNT; ++p) {
        std::uint64_t bitboard = board.getPieceBitboard(static_cast<Piece>(p));

        while (bitboard) {
            int square = std::countr_zero(bitboard);

            hash ^= keys.pieces[p][square];

            bitboard &= bitboard - 1;
        }
    }

    hash ^= keys.castling[board.getCastlingRights()];

    hash ^= enPassantKey(board);

    if (!board.isWhiteToMove()) {
        hash ^= keys.sideToMove;
    }

    return hash;
}

std::uint64_t Zobrist::pieceKey(Piece piece, int square) {
    return getKeys().pieces[piece][square];
}

std::uint64_t Zobrist::castlingKey(std::uint8_t castlingRights) {
    return getKeys().castling[castlingRights];
}

std::uint64_t Zobrist::enPassantKey(const Board& board) {
    const int square = board.getEnPassantSquare();

    if (square == -1) {
        return 0;
    }

    // Only hash the en passant file if the capture is legal: positions differing only in an
    // unusable en passant square count as the same position for repetition
    const bool white = board.isWhiteToMove();
    const int file = square % 8;
    const int pushedPawnSquare = white ? square - 8 : square + 8;
    const std::uint64_t pawns = board.getPieceBitboard(white ? WHITE_PAWN : BLACK_PAWN);

    std::uint64_t attackers = 0;

    if (file > 0) attackers |= 1ULL << (pushedPawnSquare - 1);
    if (file < 7) attackers |= 1ULL << (pushedPawnSquare + 1);

    attackers &= pawns;

    while (attackers) {
        const int from = std::countr_zero(attackers);
        attackers &= attackers - 1;

        if (enPassantKeepsKingSafe(board, from, square, pushedPawnSquare)) {
            return getKeys().enPassant[file];
        }
    }

    return 0;
}

std::uint64_t Zobrist::sideToMoveKey() {
    return getKeys().sideToMove;
}