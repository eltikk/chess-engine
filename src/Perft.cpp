#include "Perft.h"
#include "Board.h"
#include "MoveGenerator.h" 

std::uint64_t Perft::run(Board& board, int depth){
    if(depth == 0){
        return 1;
    }

    MoveList moves;
    MoveGenerator::generateLegalMoves(board, moves);

    std::uint64_t nodes = 0;

    for(int i = 0; i < moves.count; i++){
        const Move& move = moves.moves[i];
        UndoState undo;

        board.makeMove(move, undo);

        nodes += Perft::run(board, depth - 1);

        board.unmakeMove(move, undo);
    }

    return nodes;
}