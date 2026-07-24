#include "MoveGenerator.h"


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