#pragma once 

#include <cstdint>
#include "Board.h"
#include "MoveList.h"

class MoveGenerator{
public:
    static void init();

    static std::uint64_t getKnightAttacks(int square){return knightAttacks[square];}
    static std::uint64_t getKingAttacks(int square){return kingAttacks[square];}
    static std::uint64_t getPawnAttacks(int square, bool isWhite){return pawnAttacks[isWhite? 0 : 1][square];}

    static void generateKnightMoves(const Board& board, MoveList& moveList);
    static void generateBishopMoves(const Board& board, MoveList& moveList);
    static void generateRookMoves(const Board& board, MoveList& moveList);
    static void generateKingMoves(const Board& board, MoveList& moveList);
    static void generateQueenMoves(const Board& board, MoveList& moveList);
    static void generatePawnMoves(const Board& board, MoveList& moveList){
        if(board.isWhiteToMove()){
            generatePawnMoves<true>(board, moveList);
        }else{
            generatePawnMoves<false>(board, moveList);
        }
    }
private:
    static void initializeKnightAttacks();
    static void initializeKingAttacks();
    static void initializePawnAttacks();

    template<bool isWhite>
    static void generatePawnMoves(const Board& board, MoveList& moveList);

    inline static std::uint64_t knightAttacks[64];
    inline static std::uint64_t kingAttacks[64];
    inline static std::uint64_t pawnAttacks[2][64];
};
