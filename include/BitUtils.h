#pragma once 

#include <cstdint>
#include <bit>

namespace BitUtils{
    inline bool getBit(std::uint64_t bitboard, int square){
        return (bitboard >> square) & 1ULL;
    }
    
    inline void setBit(std::uint64_t& bitboard, int square){
        bitboard |= (1ULL << square);
    }

    inline void popBit(std::uint64_t& bitboard, int square){
        bitboard &= ~(1ULL << square);
    }

    inline int lsb(std::uint64_t bitboard){
        return std::countr_zero(bitboard);
    }

    inline int popLSB(std::uint64_t& bitboard){
        const int square = std::countr_zero(bitboard);
        bitboard &= bitboard - 1;
        return square;
    }

    inline int popcount(std::uint64_t bitboard){
        return std::popcount(bitboard);
    }
}
