#pragma once 

#include <cstdint>

class MoveGenerator{
public:
    static void init();

    static std::uint64_t getKnightAttacks(int square){return knightAttacks[square];}
    static std::uint64_t getKingAttacks(int square){return kingAttacks[square];}
    static std::uint64_t getPawnAttacks(int square, bool isWhite){
        return pawnAttacks[isWhite? 0 : 1][square];}

private:
    static void initializeKnightAttacks();
    static void initializeKingAttacks();
    static void initializePawnAttacks();

    inline static std::uint64_t knightAttacks[64];
    inline static std::uint64_t kingAttacks[64];
    inline static std::uint64_t pawnAttacks[2][64];
};
