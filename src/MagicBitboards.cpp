#include "MagicBitboards.h"
#include "BitUtils.h"

constexpr std::array<int, 64> rookRelevantBits = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};

constexpr std::array<std::uint64_t, 64> rookMagics = {
    0x8A80104000800020ULL, 0x0140002000100040ULL, 0x02801880A0017001ULL, 0x0100081001000420ULL,
    0x0200020010080420ULL, 0x03001C0002010008ULL, 0x8480008002000100ULL, 0x2080088004402900ULL,
    0x0000800098204000ULL, 0x2024401000200040ULL, 0x0100802000801000ULL, 0x0120800800801000ULL,
    0x0208808088000400ULL, 0x0002802200800400ULL, 0x2200800100020080ULL, 0x0801000060821100ULL,
    0x0080044006422000ULL, 0x0100808020004000ULL, 0x12108A0010204200ULL, 0x0140848010000802ULL,
    0x0481828014002800ULL, 0x8094004002004100ULL, 0x4010040010010802ULL, 0x0000020008806104ULL,
    0x0100400080208000ULL, 0x2040002120081000ULL, 0x0021200680100081ULL, 0x0020100080080080ULL,
    0x0002000A00200410ULL, 0x0000020080800400ULL, 0x0080088400100102ULL, 0x0080004600042881ULL,
    0x4040008040800020ULL, 0x0440003000200801ULL, 0x0004200011004500ULL, 0x0188020010100100ULL,
    0x0014800401802800ULL, 0x2080040080800200ULL, 0x0124080204001001ULL, 0x0200046502000484ULL,
    0x0480400080088020ULL, 0x1000422010034000ULL, 0x0030200100110040ULL, 0x0000100021010009ULL,
    0x2002080100110004ULL, 0x0202008004008002ULL, 0x0020020004010100ULL, 0x2048440040820001ULL,
    0x0101002200408200ULL, 0x841000A000C00240ULL, 0x4008142004410100ULL, 0x02060820C0120200ULL,
    0x0001001004080100ULL, 0x020C020080040080ULL, 0x2935610830022400ULL, 0x0044440041009200ULL,
    0x0280001040802101ULL, 0x2100190040002085ULL, 0x80C0084100102001ULL, 0x4024081001000421ULL,
    0x00020030A0244872ULL, 0x0012001008414402ULL, 0x02006104900A0804ULL, 0x0001004081002402ULL
};


static std::uint64_t rookMasks[64];
static std::uint64_t rookAttacks[64][4096];


std::uint64_t MagicBitboards::maskRookAttacks(int square){
    std::uint64_t attacks = 0ULL;
    int targetRank = square / 8,
        targetFile = square % 8;
    
    for(int rank = targetRank + 1; rank <= 6; rank++) attacks |= (1ULL << (rank * 8 + targetFile));
    for(int rank = targetRank - 1; rank >= 1; rank--) attacks |= (1ULL << (rank * 8 + targetFile));
    for(int file = targetFile + 1; file <= 6; file++) attacks |= (1ULL << (targetRank * 8 + file));
    for(int file = targetFile - 1; file >= 1; file--) attacks |= (1ULL << (targetRank * 8 + file));

    return attacks;
}


std::uint64_t MagicBitboards::generateRookAttacks(int square, std::uint64_t block){
    std::uint64_t attacks = 0ULL;
    int targetRank = square / 8,
        targetFile = square % 8;

    for(int rank = targetRank + 1; rank <= 7; rank++){
        attacks |= (1ULL << (rank * 8 + targetFile));
        if(block & (1ULL << (rank * 8 + targetFile))) break;
    }
    for(int rank = targetRank - 1; rank >= 0; rank--){
        attacks |= (1ULL << (rank * 8 + targetFile));
        if(block & (1ULL << (rank * 8 + targetFile))) break;
    }
    for(int file = targetFile + 1; file <= 7; file++){
        attacks |= (1ULL << (targetRank * 8 + file));
        if(block & (1ULL << (targetRank * 8 + file))) break;
    }
    for(int file = targetFile - 1; file >= 0; file--){
        attacks |= (1ULL << (targetRank * 8 + file));
        if(block & (1ULL << (targetRank * 8 + file))) break;
    }

    return attacks;
}


std::uint64_t MagicBitboards::setOccupancy(int index, int relevantBits, std::uint64_t attackMask){
    std::uint64_t occupancy = 0ULL;
    for(int i = 0; i < relevantBits; i++){
        int square = BitUtils::popLSB(attackMask);
        
        if(index & (1ULL << i)){
            occupancy |= (1ULL << square);
        }
    }
    return occupancy;
}


void MagicBitboards::initRookAttacks(){
    for(int square = 0; square < 64; square++){
        std::uint64_t mask = maskRookAttacks(square);
        rookMasks[square] = mask;

        int relevantBits = rookRelevantBits[square];
        int occupancyIndices = 1 << relevantBits;
        
        for(int index = 0; index < occupancyIndices; index++){
            std::uint64_t occupancy = setOccupancy(index, relevantBits, mask);
            std::uint64_t magicIndex = (occupancy * rookMagics[square]) >> (64 - relevantBits);

            rookAttacks[square][magicIndex] = generateRookAttacks(square, occupancy);            
        }
    }
}


void MagicBitboards::init(){
    initRookAttacks();
}


std::uint64_t MagicBitboards::getRookAttacks(int square, std::uint64_t occupied){
    std::uint64_t relevantOccupancy = occupied & rookMasks[square];
    std::uint64_t magicIndex =(relevantOccupancy * rookMagics[square]) >> (64 - rookRelevantBits[square]);

    return rookAttacks[square][magicIndex];
}