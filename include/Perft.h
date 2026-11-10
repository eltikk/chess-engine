#pragma once

#include <cstdint>


class Board;

class Perft{
public:
    static std::uint64_t run(Board& board, int depth);

};