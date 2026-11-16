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

        // Generate piece-square keys
        for (auto& piece : result.pieces) {
            for (auto& key : piece) {
                key = splitmix64(seed);
            }
        }

        // Generate castling keys
        for (auto& key : result.castling) {
            key = splitmix64(seed);
        }

        // Generate en passant keys
        for (auto& key : result.enPassant) {
            key = splitmix64(seed);
        }

        // Generate side-to-move key
        result.sideToMove = splitmix64(seed);

        return result;
    }();

    return keys;
}

} // namespace

std::uint64_t Zobrist::compute(const Board& board) {
    const auto& keys = getKeys();
    std::uint64_t hash = 0;

    // Hash every piece on the board
    for (int p = 0; p < PIECE_COUNT; ++p) {
        std::uint64_t bitboard = board.getPieceBitboard(static_cast<Piece>(p));

        while (bitboard) {
            int square = std::countr_zero(bitboard);

            hash ^= keys.pieces[p][square];

            bitboard &= bitboard - 1;
        }
    }

    // Castling rights
    hash ^= keys.castling[board.getCastlingRights()];

    // En passant
    hash ^= enPassantKey(board);

    // Side to move
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

    // Only hash the en passant file if a pawn of the side to move can capture onto it
    const bool white = board.isWhiteToMove();
    const int file = square % 8;
    const int pushedPawnSquare = white ? square - 8 : square + 8;
    const std::uint64_t pawns = board.getPieceBitboard(white ? WHITE_PAWN : BLACK_PAWN);

    std::uint64_t attackers = 0;

    if (file > 0) attackers |= 1ULL << (pushedPawnSquare - 1);
    if (file < 7) attackers |= 1ULL << (pushedPawnSquare + 1);

    return (pawns & attackers) ? getKeys().enPassant[file] : 0;
}

std::uint64_t Zobrist::sideToMoveKey() {
    return getKeys().sideToMove;
}