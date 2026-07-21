#include <iostream>
#include "Board.h"


int main(){
    Board board;
    board.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    board.print();
    board.loadFEN("8/8/8/3k4/8/4K3/8/8 b - - 17 42");
    board.print();
    board.loadFEN("8/8/8/4p3/8/8/8/8 w - e6 0 2");
    
    
    return 0;
}