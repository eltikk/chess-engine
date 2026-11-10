#include "Board.h"
#include "MoveGenerator.h"
#include "Perft.h"

#include <cstdint>
#include <iostream>

int main() {
    MoveGenerator::init();

    Board board;
    int depth = 3;

    auto results = Perft::divide(board, depth);

    std::uint64_t total = 0;

    std::cout << "Perft Divide - Depth " << depth << "\n\n";

    for (const auto& entry : results) {
        std::cout << entry.move.toUCI()
                  << ": " << entry.nodes << '\n';

        total += entry.nodes;
    }

    std::cout << "\nTotal: " << total << '\n';

    return 0;
}