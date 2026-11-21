#pragma once 

#include "Move.h"

struct MoveList{
    static constexpr int capacity = 256;

    Move moves[capacity];
    int count = 0;

    void add(const Move& move){
        moves[count] = move;
        count++;
    }

    void clear(){
        count = 0;
    }
};