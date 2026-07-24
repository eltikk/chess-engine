#pragma once 

#include <cstdint>

class MoveGenerator{
public:
    static void initializeKnightAttacks();
    static std::uint64_t getKnightAttacks(int square){
        return knightAttacks[square];
    }
private:
    inline static std::uint64_t knightAttacks[64];
};
