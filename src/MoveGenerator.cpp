#include "MoveGenerator.h"

void MoveGenerator::initializeKnightAttacks(){
    int file;
    int rank;
    const int fileChecker[8]={1,1,-1,-1,2,2,-2,-2};
    const int rankChecker[8]={2,-2,2,-2,1,-1,1,-1};
    for(int i = 0; i < 64; i++){
        file = i % 8;
        rank = i / 8;
        std::uint64_t attack = 0;

        for(int j = 0; j < 8; j++){
            int newFile = file + fileChecker[j];
            int newRank = rank + rankChecker[j];
            if(newFile >=0 && newFile < 8 && newRank >=0 && newRank < 8){
                int destination = newRank * 8 + newFile;
                attack |= 1ULL << destination;
            }
        }
        knightAttacks[i] = attack;   
    }
}


