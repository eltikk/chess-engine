#pragma once 

#include "Move.h"

struct MoveList{
    Move moves[256];
    int count = 0;

    void add(const Move& move){
        moves[count] = move;
        count++;
    }

    void clear(){
        count = 0;
    }
};