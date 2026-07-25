#include "MoveGenerator.h"
#include "BitUtils.h"

constexpr std::uint64_t NOT_A  = 0xFEFEFEFEFEFEFEFEULL; // Excludes File A
constexpr std::uint64_t NOT_H  = 0x7F7F7F7F7F7F7F7FULL; // Excludes File H
constexpr std::uint64_t NOT_AB = 0xFCFCFCFCFCFCFCFCULL; // Excludes Files A & B
constexpr std::uint64_t NOT_GH = 0x3F3F3F3F3F3F3F3FULL; // Excludes Files G & H

void MoveGenerator::init(){
    initializeKnightAttacks();
    initializeKingAttacks();
    initializePawnAttacks();
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