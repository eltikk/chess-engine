#include "MoveGenerator.h"
#include "BitUtils.h"
#include "MagicBitboards.h"

constexpr std::uint64_t NOT_A  = 0xFEFEFEFEFEFEFEFEULL; // Excludes File A
constexpr std::uint64_t NOT_H  = 0x7F7F7F7F7F7F7F7FULL; // Excludes File H
constexpr std::uint64_t NOT_AB = 0xFCFCFCFCFCFCFCFCULL; // Excludes Files A & B
constexpr std::uint64_t NOT_GH = 0x3F3F3F3F3F3F3F3FULL; // Excludes Files G & H

void MoveGenerator::init(){
    initializeKnightAttacks();
    initializeKingAttacks();
    initializePawnAttacks();
    MagicBitboards::init();
}


void MoveGenerator::initializeKnightAttacks(){
    for(int i = 0; i < 64;i++){
        std::uint64_t b = 1ULL << i;
        
        knightAttacks[i] = (((b << 17) | (b >> 15)) & NOT_A)
                         | (((b << 10) | (b >> 6)) & NOT_AB)
                         | (((b << 15) | (b >> 17)) & NOT_H)
                         | (((b << 6)  | (b >> 10)) & NOT_GH);     
    }
}

void MoveGenerator::initializeKingAttacks(){
    for(int i = 0; i < 64; i++){
        std::uint64_t b = 1ULL << i;

        kingAttacks[i] = ((b << 8) | (b >> 8))
                       | (((b << 1) | (b << 9) | (b >> 7)) & NOT_A)
                       | (((b >> 1) | (b >> 9) | (b << 7)) & NOT_H);
    }
}

void MoveGenerator::initializePawnAttacks(){
    for(int i =0 ; i < 64; i++){
        std::uint64_t b = 1ULL << i;

        //White Pawn Attacks
        pawnAttacks[0][i] = ((b << 7) & NOT_H) | ((b<<9) & NOT_A);

        //Black Pawn Attacks
        pawnAttacks[1][i] = ((b >> 7) & NOT_A) | ((b>>9) & NOT_H);
     }
}


void MoveGenerator::generateKnightMoves(const Board& board, MoveList& moveList) {
    Piece knightPiece = board.isWhiteToMove() ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT;

    std::uint64_t knights = board.getPieceBitboard(knightPiece);
    std::uint64_t friendly = board.getFriendlyPieces();
    std::uint64_t enemy = board.getEnemyPieces();

    while (knights) {
        int fromSquare = BitUtils::popLSB(knights);
        
        // Attack table masked with non-friendly squares
        std::uint64_t targets = getKnightAttacks(fromSquare) & ~friendly;

        while (targets) {
            int toSquare = BitUtils::popLSB(targets);

            MoveType type = BitUtils::getBit(enemy, toSquare) ? MoveType::Capture : MoveType::Quiet;

            moveList.add({
                static_cast<std::uint8_t>(fromSquare),
                static_cast<std::uint8_t>(toSquare),
                type
            });
        }
    }
}

void MoveGenerator::generateBishopMoves(const Board& board, MoveList& moveList) {
    Piece bishopPiece = board.isWhiteToMove() ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP;

    std::uint64_t bishops = board.getPieceBitboard(bishopPiece);
    std::uint64_t friendly = board.getFriendlyPieces();
    std::uint64_t enemy = board.getEnemyPieces();
    std::uint64_t occupied = board.getOccupied();

    while(bishops){
        int fromSquare = BitUtils::popLSB(bishops);

        std::uint64_t targets = MagicBitboards::getBishopAttacks(fromSquare, occupied) & ~friendly;

        while(targets){
            int toSquare = BitUtils::popLSB(targets);

            MoveType type = BitUtils::getBit(enemy, toSquare) ? MoveType::Capture : MoveType::Quiet;

            moveList.add({
                static_cast<std::uint8_t>(fromSquare),
                static_cast<std::uint8_t>(toSquare),
                type
            });
        }
    }
}

void MoveGenerator::generateRookMoves(const Board& board, MoveList& moveList) {
    Piece rookPiece =board.isWhiteToMove() ? Piece::WHITE_ROOK : Piece::BLACK_ROOK;

    std::uint64_t rooks = board.getPieceBitboard(rookPiece);
    std::uint64_t friendly = board.getFriendlyPieces();
    std::uint64_t enemy = board.getEnemyPieces();
    std::uint64_t occupied = board.getOccupied();

    while (rooks) {
        int fromSquare = BitUtils::popLSB(rooks);

        std::uint64_t targets =
            MagicBitboards::getRookAttacks(fromSquare, occupied)
            & ~friendly;

        while (targets) {
            int toSquare = 
                BitUtils::popLSB(targets);

            MoveType type =
                BitUtils::getBit(enemy, toSquare) ? MoveType::Capture : MoveType::Quiet;

            moveList.add({
                static_cast<std::uint8_t>(fromSquare),
                static_cast<std::uint8_t>(toSquare),
                type
            });
        }
    }
}

void MoveGenerator::generateQueenMoves(const Board& board, MoveList& moveList){
    Piece queenPiece = board.isWhiteToMove() ? Piece::WHITE_QUEEN : Piece::BLACK_QUEEN;

    std::uint64_t queens = board.getPieceBitboard(queenPiece);
    std::uint64_t friendly = board.getFriendlyPieces();
    std::uint64_t enemy = board.getEnemyPieces();
    std::uint64_t occupied = board.getOccupied();

    while(queens){
        int fromSquare = BitUtils::popLSB(queens);

        std::uint64_t targets = 
            (MagicBitboards::getRookAttacks(fromSquare, occupied) |
             MagicBitboards::getBishopAttacks(fromSquare, occupied))
            & ~friendly;
        
        while(targets){
            int toSquare = BitUtils::popLSB(targets);

            MoveType type = BitUtils::getBit(enemy, toSquare) ? MoveType::Capture : MoveType::Quiet;

            moveList.add({
                static_cast<std::uint8_t>(fromSquare),
                static_cast<std::uint8_t>(toSquare),
                type
            });
        }
    }
}


void MoveGenerator::generateKingMoves(const Board& board, MoveList& moveList){
    Piece kingPiece = board.isWhiteToMove() ? Piece::WHITE_KING : Piece::BLACK_KING;
    std::uint64_t king = board.getPieceBitboard(kingPiece);

    if(king == 0)return;

    std::uint64_t friendly = board.getFriendlyPieces();
    std::uint64_t enemy = board.getEnemyPieces();

    int fromSquare = BitUtils::lsb(king);

    std::uint64_t targets = getKingAttacks(fromSquare) & ~friendly;

    while(targets){
        int toSquare = BitUtils::popLSB(targets);

        MoveType type = BitUtils::getBit(enemy, toSquare) ? MoveType::Capture : MoveType::Quiet;

        moveList.add({
            static_cast<std::uint8_t>(fromSquare),
            static_cast<std::uint8_t>(toSquare),
            type
        });
    }
}

template <bool isWhite>
void MoveGenerator::generatePawnMoves(const Board& board, MoveList& moveList) {
    
    // Compile-time Constants
    constexpr Piece pawnPiece   = isWhite ? Piece::WHITE_PAWN   : Piece::BLACK_PAWN;
    constexpr Piece queenPiece  = isWhite ? Piece::WHITE_QUEEN  : Piece::BLACK_QUEEN;
    constexpr Piece rookPiece   = isWhite ? Piece::WHITE_ROOK   : Piece::BLACK_ROOK;
    constexpr Piece bishopPiece = isWhite ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP;
    constexpr Piece knightPiece = isWhite ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT;

    constexpr uint64_t promotionRank    = isWhite ? 0xFF00000000000000ULL : 0x00000000000000FFULL;
    constexpr uint64_t intermediateRank = isWhite ? 0x0000000000FF0000ULL : 0x0000FF0000000000ULL; // Rank 3 (White) / Rank 6 (Black)

    constexpr int pushOffset            = isWhite ? 8 : -8;
    constexpr int captureLeftOffset     = isWhite ? 7 : -9;
    constexpr int captureRightOffset    = isWhite ? 9 : -7;

    
    // Board State Extraction
    uint64_t pawns           = board.getPieceBitboard(pawnPiece);
    uint64_t occupied        = board.getOccupied();
    uint64_t enemy           = board.getEnemyPieces();
    int enPassantSquare      = board.getEnPassantSquare();

    
    // 1. PUSHES (Single & Double)
    uint64_t singlePushes = (isWhite ? (pawns << 8) : (pawns >> 8)) & ~occupied;

    // Non-promotion single pushes 
    uint64_t quietPushes = singlePushes & ~promotionRank;
    while (quietPushes) {
        int to = BitUtils::popLSB(quietPushes);
        moveList.add({static_cast<uint8_t>(to - pushOffset), static_cast<uint8_t>(to), MoveType::Quiet});
    }

    // Double pushes
    uint64_t doublePushes = (isWhite ? ((singlePushes & intermediateRank) << 8) 
                                     : ((singlePushes & intermediateRank) >> 8)) & ~occupied;
    while (doublePushes) {
        int to = BitUtils::popLSB(doublePushes);
        moveList.add({static_cast<uint8_t>(to - 2 * pushOffset), static_cast<uint8_t>(to), MoveType::DoublePawnPush});
    }

    
    // 2. CAPTURES (Left & Right)    
    uint64_t leftCaptures  = (isWhite ? ((pawns & NOT_A) << 7) : ((pawns & NOT_A) >> 9)) & enemy;
    uint64_t rightCaptures = (isWhite ? ((pawns & NOT_H) << 9) : ((pawns & NOT_H) >> 7)) & enemy;

    uint64_t quietLeftCaptures  = leftCaptures & ~promotionRank;
    uint64_t quietRightCaptures = rightCaptures & ~promotionRank;
    uint64_t promoLeftCaps      = leftCaptures & promotionRank;
    uint64_t promoRightCaps     = rightCaptures & promotionRank;

    while (quietLeftCaptures) {
        int to = BitUtils::popLSB(quietLeftCaptures);
        moveList.add({static_cast<uint8_t>(to - captureLeftOffset), static_cast<uint8_t>(to), MoveType::Capture});
    }

    while (quietRightCaptures) {
        int to = BitUtils::popLSB(quietRightCaptures);
        moveList.add({static_cast<uint8_t>(to - captureRightOffset), static_cast<uint8_t>(to), MoveType::Capture});
    }


    
    // 3. PROMOTIONS (Push & Capture)    
    uint64_t promotionPushes = singlePushes & promotionRank;

    auto addPromotions = [&](int from, int to, MoveType type) {
        moveList.add({static_cast<uint8_t>(from), static_cast<uint8_t>(to), type, queenPiece});
        moveList.add({static_cast<uint8_t>(from), static_cast<uint8_t>(to), type, rookPiece});
        moveList.add({static_cast<uint8_t>(from), static_cast<uint8_t>(to), type, bishopPiece});
        moveList.add({static_cast<uint8_t>(from), static_cast<uint8_t>(to), type, knightPiece});
    };

    while (promotionPushes) {
        int to = BitUtils::popLSB(promotionPushes);
        addPromotions(to - pushOffset, to, MoveType::Promotion);
    }

    while (promoLeftCaps) {
        int to = BitUtils::popLSB(promoLeftCaps);
        addPromotions(to - captureLeftOffset, to, MoveType::PromotionCapture);
    }

    while (promoRightCaps) {
        int to = BitUtils::popLSB(promoRightCaps);
        addPromotions(to - captureRightOffset, to, MoveType::PromotionCapture);
    }


    
    // 4. en Passant    
    if (enPassantSquare != -1) {
        uint64_t enPassantAttackers = getPawnAttacks(enPassantSquare, !isWhite) & pawns;
        while (enPassantAttackers) {
            int from = BitUtils::popLSB(enPassantAttackers);
            moveList.add({static_cast<uint8_t>(from), static_cast<uint8_t>(enPassantSquare), MoveType::EnPassant});
        }
    }
}



template void MoveGenerator::generatePawnMoves<true>(const Board& board, MoveList& moveList);
template void MoveGenerator::generatePawnMoves<false>(const Board& board, MoveList& moveList);