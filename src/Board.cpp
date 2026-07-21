#include <iostream> 
#include "Board.h"




Board::Board(){
    setStartingPosition();
}

void Board::setStartingPosition(){

    clearBoard();

    //White pawns
    for(int square = 8; square <= 15; square++){
        pieces[WHITE_PAWN] |= 1ULL << square;
    }

    //White pieces
    pieces[WHITE_ROOK] |= (1ULL << 0) | (1ULL << 7);
    pieces[WHITE_KNIGHT] |= (1ULL << 1) | (1ULL << 6);
    pieces[WHITE_BISHOP] |= (1ULL << 2) | (1ULL << 5);
    pieces[WHITE_QUEEN] |= 1ULL<< 3;
    pieces[WHITE_KING] |= 1ULL << 4;

    //Black pawns
    for(int square = 48; square <=55; square++){
        pieces[BLACK_PAWN] |= 1ULL << square;
    }

    //Black pieces
    pieces[BLACK_ROOK] |= (1ULL << 56) | (1ULL<< 63);
    pieces[BLACK_KNIGHT] |= (1ULL << 57) | (1ULL<< 62);
    pieces[BLACK_BISHOP] |= (1ULL << 58) | (1ULL<< 61);
    pieces[BLACK_QUEEN] |= 1ULL << 59;
    pieces[BLACK_KING] |= 1ULL << 60;

    whiteToMove = true;
    updateOccupancy();

    castlingRights = 0b1111; 
    enPassantSquare = -1;
    halfmoveClock = 0;
    fullmoveCounter = 1;
}



void Board::updateOccupancy(){
    whitePieces = 0;
    blackPieces = 0;

    for(int piece = WHITE_PAWN; piece <= WHITE_KING; piece++){
        whitePieces |= pieces[piece];
    }

    for(int piece = BLACK_PAWN; piece <= BLACK_KING; piece++){
        blackPieces |= pieces[piece];
    }

    occupied = whitePieces | blackPieces;
}


char Board::pieceAt(int square) const {
    const std::uint64_t mask = 1ULL << square;

    const char symbols[PIECE_COUNT] = {
        'P', 'N', 'B', 'R', 'Q', 'K',
        'p', 'n', 'b', 'r', 'q', 'k'
    };

    for(int piece = 0; piece < PIECE_COUNT; piece++){
        if(pieces[piece] & mask){
            return symbols[piece];
        }
    }

    return '.';
}


void Board::print() const {
    std::cout << '\n';

    for(int rank = 7; rank >=0; rank--){
        std::cout << rank + 1 << "  ";

        for(int file = 0; file < 8; file ++){
            const int square = rank * 8 + file;
            std::cout << pieceAt(square) << ' ';
        }

        std::cout << '\n';
    }

    std::cout << "\n   a b c d e f g h\n\n";
}


void Board::clearBoard(){
    for(std::uint64_t& bitboard : pieces){
        bitboard = 0;
    }

    whiteToMove = true;
    halfmoveClock = 0;
    fullmoveCounter = 1;
    castlingRights = 0;
    enPassantSquare = -1;
    whitePieces = 0;
    blackPieces = 0;
    occupied = 0;

} 



//FEN IMPLEMENTATION

void Board::loadFEN(const std::string& fen){
    clearBoard();

    //Piece placement
    int rank = 7;
    int file = 0;

    for(char c : fen){
        if(c == ' ')break;
        
        if(c == '/'){
            rank--;
            file = 0;
            continue;
        }
        if(c>= '1' && c<='8'){
            file += c -'0';
            continue;
        }

        const int square = rank * 8 + file;

        switch(c){
            case 'P':
                pieces[WHITE_PAWN] |= 1ULL << square;
                break;

            case 'N':
                pieces[WHITE_KNIGHT] |= 1ULL << square;
                break;

            case 'B':
                pieces[WHITE_BISHOP] |= 1ULL << square;
                break;

            case 'R':
                pieces[WHITE_ROOK] |= 1ULL << square;
                break;

            case 'Q':
                pieces[WHITE_QUEEN] |= 1ULL << square;
                break;

            case 'K':
                pieces[WHITE_KING] |= 1ULL << square;
                break;

            case 'p':
                pieces[BLACK_PAWN] |= 1ULL << square;
                break;

            case 'n':
                pieces[BLACK_KNIGHT] |= 1ULL << square;
                break;

            case 'b':
                pieces[BLACK_BISHOP] |= 1ULL << square;
                break;

            case 'r':
                pieces[BLACK_ROOK] |= 1ULL << square;
                break;

            case 'q':
                pieces[BLACK_QUEEN] |= 1ULL << square;
                break;

            case 'k':
                pieces[BLACK_KING] |= 1ULL << square;
                break;
        }
        file++;
    }
    updateOccupancy();


    //Side to move
    std::size_t firstSpace = fen.find(' ');

    if(firstSpace != std::string::npos && firstSpace + 1 < fen.size()){
        whiteToMove = (fen[firstSpace+1] == 'w');
    }


    //Castling ability test
    castlingRights = 0;
    std::string castling;
    std::size_t secondSpace = fen.find(' ', firstSpace + 1);
    std::size_t thirdSpace = fen.find(' ', secondSpace + 1);
    if(secondSpace != std::string::npos){
        castling = fen.substr(secondSpace + 1, thirdSpace - secondSpace - 1);
    }
    
    for(char c : castling){
        switch(c){
            case 'K':
            castlingRights |= 1 << 3;
            break;

            case 'Q':
            castlingRights |= 1 << 2;
            break; 

            case 'k':
            castlingRights |= 1 << 1;
            break; 

            case 'q':
            castlingRights |= 1 << 0;
            break; 
        }
    }

    //En passant target square

    enPassantSquare = -1;
    
    if(fen[thirdSpace+1] != '-'){
        int enPassantFile = fen[thirdSpace+1] - 'a';
        int enPassantRank = fen[thirdSpace+2] - '1';
        enPassantSquare = enPassantRank* 8 + enPassantFile;
    }



    //Halfmove clock
    std::size_t fourthSpace = fen.find(' ', thirdSpace+1);
    std::size_t fifthSpace = fen.find(' ', fourthSpace+1);
    
    std::string halfMoveString = fen.substr(fourthSpace+1, fifthSpace - fourthSpace -1);
    halfmoveClock = std::stoi(halfMoveString);


    //FullMove
    std::string fullMoveString = fen.substr(fifthSpace + 1);
    fullmoveCounter = std::stoi(fullMoveString);

}