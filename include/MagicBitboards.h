#pragma once 

#include <cstdint>
#include <array>

class MagicBitboards{
public:
    static void init();
    static std::uint64_t getRookAttacks(int square, std::uint64_t occupied);
    static std::uint64_t getBishopAttacks(int square, std::uint64_t occupancy);

private:
    static std::uint64_t maskRookAttacks(int square);
    static std::uint64_t generateRookAttacks(int square, std::uint64_t block);
    static void initRookAttacks();

    static std::uint64_t setOccupancy(int index, int relevantBits, std::uint64_t attackMask);

    static std::uint64_t maskBishopAttacks(int square);
    static std::uint64_t generateBishopAttacks(int square, std::uint64_t block);
    static void initBishopAttacks();
};