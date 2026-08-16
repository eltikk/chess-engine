#include "MagicBitboards.h"
#include "BitUtils.h"
namespace
{
    constexpr std::array<int, 64> rookRelevantBits = {
        12, 11, 11, 11, 11, 11, 11, 12,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        12, 11, 11, 11, 11, 11, 11, 12};

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
        0x00020030A0244872ULL, 0x0012001008414402ULL, 0x02006104900A0804ULL, 0x0001004081002402ULL};

    constexpr std::array<int, 64> bishopRelevantBits = {
        6, 5, 5, 5, 5, 5, 5, 6,
        5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 7, 7, 7, 7, 5, 5,
        5, 5, 7, 9, 9, 7, 5, 5,
        5, 5, 7, 9, 9, 7, 5, 5,
        5, 5, 7, 7, 7, 7, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5,
        6, 5, 5, 5, 5, 5, 5, 6};

    constexpr std::array<std::uint64_t, 64> bishopMagics = {
        0x40040844404084ULL,
        0x2004208a004208ULL,
        0x10190041080202ULL,
        0x108060845042010ULL,
        0x581104180800210ULL,
        0x2112080446200010ULL,
        0x1080820820060210ULL,
        0x3c0808410220200ULL,
        0x4050404440404ULL,
        0x21001420088ULL,
        0x24d0080801082102ULL,
        0x1020a0a020400ULL,
        0x40308200402ULL,
        0x4011002100800ULL,
        0x401484104104005ULL,
        0x801010402020200ULL,
        0x400210c3880100ULL,
        0x404022024108200ULL,
        0x810018200204102ULL,
        0x4002801a02003ULL,
        0x85040820080400ULL,
        0x810102c808880400ULL,
        0xe900410884800ULL,
        0x8002020480840102ULL,
        0x220200865090201ULL,
        0x2010100a02021202ULL,
        0x152048408022401ULL,
        0x20080002081110ULL,
        0x4001001021004000ULL,
        0x800040400a011002ULL,
        0xe4004081011002ULL,
        0x1c004001012080ULL,
        0x8004200962a00220ULL,
        0x8422100208500202ULL,
        0x2000402200300c08ULL,
        0x8646020080080080ULL,
        0x80020a0200100808ULL,
        0x2010004880111000ULL,
        0x623000a080011400ULL,
        0x42008c0340209202ULL,
        0x209188240001000ULL,
        0x400408a884001800ULL,
        0x110400a6080400ULL,
        0x1840060a44020800ULL,
        0x90080104000041ULL,
        0x201011000808101ULL,
        0x1a2208080504f080ULL,
        0x8012020600211212ULL,
        0x500861011240000ULL,
        0x180806108200800ULL,
        0x4000020e01040044ULL,
        0x300000261044000aULL,
        0x802241102020002ULL,
        0x20906061210001ULL,
        0x5a84841004010310ULL,
        0x4010801011c04ULL,
        0xa010109502200ULL,
        0x4a02012000ULL,
        0x500201010098b028ULL,
        0x8040002811040900ULL,
        0x28000010020204ULL,
        0x6000020202d0240ULL,
        0x8918844842082200ULL,
        0x4010011029020020ULL};

    static std::uint64_t rookMasks[64];
    static std::uint64_t rookAttacks[64][4096];
    static std::uint64_t bishopMasks[64];
    static std::uint64_t bishopAttacks[64][512];
}


std::uint64_t MagicBitboards::maskRookAttacks(int square)
{
    std::uint64_t attacks = 0ULL;
    int targetRank = square / 8,
        targetFile = square % 8;

    for (int rank = targetRank + 1; rank <= 6; rank++)
        attacks |= (1ULL << (rank * 8 + targetFile));
    for (int rank = targetRank - 1; rank >= 1; rank--)
        attacks |= (1ULL << (rank * 8 + targetFile));
    for (int file = targetFile + 1; file <= 6; file++)
        attacks |= (1ULL << (targetRank * 8 + file));
    for (int file = targetFile - 1; file >= 1; file--)
        attacks |= (1ULL << (targetRank * 8 + file));

    return attacks;
}

std::uint64_t MagicBitboards::maskBishopAttacks(int square)
{
    std::uint64_t attacks = 0ULL;
    int targetRank = square / 8,
        targetFile = square % 8;

    for (int rank = targetRank + 1, file = targetFile + 1; rank <= 6 && file <= 6; rank++, file++)
        attacks |= 1ULL << (rank * 8 + file);
    for (int rank = targetRank + 1, file = targetFile - 1; rank <= 6 && file >= 1; rank++, file--)
        attacks |= 1ULL << (rank * 8 + file);
    for (int rank = targetRank - 1, file = targetFile + 1; rank >= 1 && file <= 6; rank--, file++)
        attacks |= 1ULL << (rank * 8 + file);
    for (int rank = targetRank - 1, file = targetFile - 1; rank >= 1 && file >= 1; rank--, file--)
        attacks |= 1ULL << (rank * 8 + file);

    return attacks;
}


std::uint64_t MagicBitboards::generateRookAttacks(int square, std::uint64_t block)
{
    std::uint64_t attacks = 0ULL;
    int targetRank = square / 8,
        targetFile = square % 8;

    for (int rank = targetRank + 1; rank <= 7; rank++)
    {
        attacks |= (1ULL << (rank * 8 + targetFile));
        if (block & (1ULL << (rank * 8 + targetFile)))
            break;
    }
    for (int rank = targetRank - 1; rank >= 0; rank--)
    {
        attacks |= (1ULL << (rank * 8 + targetFile));
        if (block & (1ULL << (rank * 8 + targetFile)))
            break;
    }
    for (int file = targetFile + 1; file <= 7; file++)
    {
        attacks |= (1ULL << (targetRank * 8 + file));
        if (block & (1ULL << (targetRank * 8 + file)))
            break;
    }
    for (int file = targetFile - 1; file >= 0; file--)
    {
        attacks |= (1ULL << (targetRank * 8 + file));
        if (block & (1ULL << (targetRank * 8 + file)))
            break;
    }

    return attacks;
}

std::uint64_t MagicBitboards::generateBishopAttacks(int square, std::uint64_t block)
{
    std::uint64_t attacks = 0ULL;
    int targetRank = square / 8,
        targetFile = square % 8;

    for (int rank = targetRank + 1, file = targetFile + 1; rank <= 7 && file <= 7; rank++, file++)
    {
        attacks |= (1ULL << (rank * 8 + file));
        if (block & (1ULL << (rank * 8 + file)))
            break;
    }
    for (int rank = targetRank + 1, file = targetFile - 1; rank <= 7 && file >= 0; rank++, file--)
    {
        attacks |= (1ULL << (rank * 8 + file));
        if (block & (1ULL << (rank * 8 + file)))
            break;
    }
    for (int rank = targetRank - 1, file = targetFile + 1; rank >= 0 && file <= 7; rank--, file++)
    {
        attacks |= (1ULL << (rank * 8 + file));
        if (block & (1ULL << (rank * 8 + file)))
            break;
    }
    for (int rank = targetRank - 1, file = targetFile - 1; rank >= 0 && file >= 0; rank--, file--)
    {
        attacks |= (1ULL << (rank * 8 + file));
        if (block & (1ULL << (rank * 8 + file)))
            break;
    }

    return attacks;
}


std::uint64_t MagicBitboards::setOccupancy(int index, int relevantBits, std::uint64_t attackMask)
{
    std::uint64_t occupancy = 0ULL;
    for (int i = 0; i < relevantBits; i++)
    {
        int square = BitUtils::popLSB(attackMask);

        if (index & (1ULL << i))
        {
            occupancy |= (1ULL << square);
        }
    }
    return occupancy;
}


void MagicBitboards::initRookAttacks()
{
    for (int square = 0; square < 64; square++)
    {
        std::uint64_t mask = maskRookAttacks(square);
        rookMasks[square] = mask;

        int relevantBits = rookRelevantBits[square];
        int occupancyIndices = 1 << relevantBits;

        for (int index = 0; index < occupancyIndices; index++)
        {
            std::uint64_t occupancy = setOccupancy(index, relevantBits, mask);
            std::uint64_t magicIndex = (occupancy * rookMagics[square]) >> (64 - relevantBits);

            rookAttacks[square][magicIndex] = generateRookAttacks(square, occupancy);
        }
    }
}

void MagicBitboards::initBishopAttacks()
{
    for (int square = 0; square < 64; square++)
    {
        std::uint64_t mask = maskBishopAttacks(square);
        bishopMasks[square] = mask;

        int relevantBits = bishopRelevantBits[square];
        int occupancyIndices = 1 << relevantBits;

        for (int index = 0; index < occupancyIndices; index++)
        {
            std::uint64_t occupancy = setOccupancy(index, relevantBits, mask);
            std::uint64_t magicIndex = (occupancy * bishopMagics[square]) >> (64 - relevantBits);

            bishopAttacks[square][magicIndex] = generateBishopAttacks(square, occupancy);
        }
    }
}


void MagicBitboards::init()
{
    initRookAttacks();
    initBishopAttacks();
}


std::uint64_t MagicBitboards::getRookAttacks(int square, std::uint64_t occupied)
{
    std::uint64_t relevantOccupancy = occupied & rookMasks[square];
    std::uint64_t magicIndex = (relevantOccupancy * rookMagics[square]) >> (64 - rookRelevantBits[square]);

    return rookAttacks[square][magicIndex];
}

std::uint64_t MagicBitboards::getBishopAttacks(int square, std::uint64_t occupied)
{
    std::uint64_t relevantOccupancy = occupied & bishopMasks[square];
    std::uint64_t magicIndex = (relevantOccupancy * bishopMagics[square]) >> (64 - bishopRelevantBits[square]);

    return bishopAttacks[square][magicIndex];
}