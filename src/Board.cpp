#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "Board.h"
#include "BitUtils.h"
#include "Move.h"
#include "Zobrist.h"

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

    hash = Zobrist::compute(*this);
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

Piece Board::pieceAtSquare(int square) const {
    for (int p = 0; p < PIECE_COUNT; p++) {
        if (BitUtils::getBit(pieces[p], square)) {
            return static_cast<Piece>(p);
        }
    }
    return PIECE_COUNT;
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

    history.clear();

    hash = Zobrist::compute(*this);
    enPassantKey = 0;
}

void Board::makeMove(const Move& move, UndoState& undo){

    Piece movingPiece = pieceAtSquare(move.from);


    int capturedSquare = move.to;

    if(move.type == MoveType::EnPassant){
        capturedSquare = whiteToMove ? move.to - 8 : move.to + 8;
    }

    Piece capturedPiece = pieceAtSquare(capturedSquare);



    undo.movedPiece = movingPiece;
    undo.capturedPiece = capturedPiece;

    undo.capturedSquare = capturedPiece != PIECE_COUNT ? capturedSquare : -1;



    undo.castlingRights = castlingRights;
    undo.enPassantSquare = enPassantSquare;
    undo.halfmoveClock = halfmoveClock;
    undo.fullmoveCounter = fullmoveCounter;
    undo.whiteToMove = whiteToMove;
    undo.hash = hash;
    undo.enPassantKey = enPassantKey;

    history.push_back(hash);

    hash ^= Zobrist::castlingKey(castlingRights);
    hash ^= enPassantKey;

    if(capturedPiece != PIECE_COUNT){
        BitUtils::popBit(pieces[capturedPiece], capturedSquare);
        hash ^= Zobrist::pieceKey(capturedPiece, capturedSquare);
    }

    BitUtils::popBit(pieces[movingPiece], move.from);
    hash ^= Zobrist::pieceKey(movingPiece, move.from);

    if (move.type == MoveType::Promotion ||
        move.type == MoveType::PromotionCapture){

        BitUtils::setBit(pieces[move.promotionPiece], move.to);
        hash ^= Zobrist::pieceKey(move.promotionPiece, move.to);

       } else {
        BitUtils::setBit(pieces[movingPiece], move.to);
        hash ^= Zobrist::pieceKey(movingPiece, move.to);
       }


    if (move.type == MoveType::KingCastle ||
        move.type == MoveType::QueenCastle) {

        bool kingSide = move.type == MoveType::KingCastle;

        int rookFrom;
        int rookTo;

        if (whiteToMove) {
            rookFrom = kingSide ? 7 : 0;
            rookTo = kingSide ? 5 : 3;
        } else {
            rookFrom = kingSide ? 63 : 56;
            rookTo = kingSide ? 61 : 59;
        }

        Piece rook = whiteToMove ? WHITE_ROOK : BLACK_ROOK;

        BitUtils::popBit(pieces[rook], rookFrom);
        BitUtils::setBit(pieces[rook], rookTo);
        hash ^= Zobrist::pieceKey(rook, rookFrom) ^ Zobrist::pieceKey(rook, rookTo);
    }


    constexpr std::uint8_t WK = 1 << 3;
    constexpr std::uint8_t WQ = 1 << 2;
    constexpr std::uint8_t BK = 1 << 1;
    constexpr std::uint8_t BQ = 1 << 0;

    if (movingPiece == WHITE_KING) {
        castlingRights &= ~(WK | WQ);
    }

    if (movingPiece == BLACK_KING) {
        castlingRights &= ~(BK | BQ);
    }

    if (movingPiece == WHITE_ROOK) {
        if (move.from == 0) castlingRights &= ~WQ;
        if (move.from == 7) castlingRights &= ~WK;
    }

    if (movingPiece == BLACK_ROOK) {
        if (move.from == 56) castlingRights &= ~BQ;
        if (move.from == 63) castlingRights &= ~BK;
    }

    if (capturedPiece == WHITE_ROOK) {
        if (capturedSquare == 0) castlingRights &= ~WQ;
        if (capturedSquare == 7) castlingRights &= ~WK;
    }

    if (capturedPiece == BLACK_ROOK) {
        if (capturedSquare == 56) castlingRights &= ~BQ;
        if (capturedSquare == 63) castlingRights &= ~BK;
    }

    enPassantSquare = -1;

    if (move.type == MoveType::DoublePawnPush){
        enPassantSquare = (move.from + move.to) / 2;
    }

    if (movingPiece == WHITE_PAWN ||
        movingPiece == BLACK_PAWN ||
        capturedPiece != PIECE_COUNT){

            halfmoveClock = 0;

    } else {
            halfmoveClock++;
    }

    if(!whiteToMove){
        fullmoveCounter++;
    }

    whiteToMove = !whiteToMove;


    updateOccupancy();

    enPassantKey = Zobrist::enPassantKey(*this);

    hash ^= Zobrist::castlingKey(castlingRights);
    hash ^= enPassantKey;
    hash ^= Zobrist::sideToMoveKey();
}

void Board::unmakeMove(const Move& move, const UndoState& undo) {

    whiteToMove = undo.whiteToMove;

    if (move.type == MoveType::Promotion ||
        move.type == MoveType::PromotionCapture) {

        BitUtils::popBit(pieces[move.promotionPiece], move.to);

    } else {

        BitUtils::popBit(pieces[undo.movedPiece], move.to);

    }

    BitUtils::setBit(pieces[undo.movedPiece], move.from);

    if (undo.capturedPiece != PIECE_COUNT) {

        BitUtils::setBit(
            pieces[undo.capturedPiece],
            undo.capturedSquare
        );

    }

    if (move.type == MoveType::KingCastle ||
        move.type == MoveType::QueenCastle) {

        bool kingSide = move.type == MoveType::KingCastle;

        int rookFrom;
        int rookTo;

        if (whiteToMove) {
            rookFrom = kingSide ? 7 : 0;
            rookTo = kingSide ? 5 : 3;
        } else {
            rookFrom = kingSide ? 63 : 56;
            rookTo = kingSide ? 61 : 59;
        }

        Piece rook = whiteToMove ? WHITE_ROOK : BLACK_ROOK;

        BitUtils::popBit(pieces[rook], rookTo);
        BitUtils::setBit(pieces[rook], rookFrom);
    }

    castlingRights = undo.castlingRights;
    enPassantSquare = undo.enPassantSquare;
    halfmoveClock = undo.halfmoveClock;
    fullmoveCounter = undo.fullmoveCounter;
    hash = undo.hash;
    enPassantKey = undo.enPassantKey;

    history.pop_back();

    updateOccupancy();
}

int Board::repetitionDistance(int searchPly) const {
    const int size = static_cast<int>(history.size());

    // Positions before the last capture or pawn move can never recur
    const int limit = std::min(halfmoveClock, size);

    int occurrences = 0;

    // The same side is to move every second ply, and a position needs at least four plies to recur
    for(int distance = 4; distance <= limit; distance += 2){
        if(history[size - distance] != hash){
            continue;
        }

        // Search heuristic: one earlier occurrence after the root suffices, since the line could
        // repeat it again. Occurrences at or before the root need the full threefold count.
        if(distance < searchPly || ++occurrences >= 2){
            return distance;
        }
    }

    return 0;
}

bool Board::isInsufficientMaterial() const {
    const std::uint64_t majorsAndPawns =
        pieces[WHITE_PAWN] | pieces[BLACK_PAWN] |
        pieces[WHITE_ROOK] | pieces[BLACK_ROOK] |
        pieces[WHITE_QUEEN] | pieces[BLACK_QUEEN];

    if(majorsAndPawns){
        return false;
    }

    const std::uint64_t knights = pieces[WHITE_KNIGHT] | pieces[BLACK_KNIGHT];
    const std::uint64_t bishops = pieces[WHITE_BISHOP] | pieces[BLACK_BISHOP];

    // Bare kings, or a single minor piece
    if(BitUtils::popcount(knights | bishops) <= 1){
        return true;
    }

    // Only bishops, all on squares of the same color
    constexpr std::uint64_t lightSquares = 0x55AA55AA55AA55AAULL;

    return knights == 0 && ((bishops & lightSquares) == 0 || (bishops & ~lightSquares) == 0);
}

//FEN IMPLEMENTATION

void Board::loadFEN(const std::string& fen){
    std::istringstream iss(fen);
    std::string placement, side, castling, enPassant;

    //The first four fields are required
    if(!(iss >> placement >> side >> castling >> enPassant)){
        throw std::invalid_argument("FEN must contain at least four fields: " + fen);
    }

    //En passant target square: '-' or a square on rank 6 (white to move) / rank 3 (black to move)
    int enPassantTarget = -1;

    if(enPassant != "-"){
        const char expectedRank = (side == "b") ? '3' : '6';

        if(enPassant.size() != 2 || enPassant[0] < 'a' || enPassant[0] > 'h' || enPassant[1] != expectedRank){
            throw std::invalid_argument("Invalid en passant square in FEN: " + fen);
        }

        enPassantTarget = (enPassant[1] - '1') * 8 + (enPassant[0] - 'a');
    }

    //Halfmove clock and fullmove counter are optional
    int halfmove = 0;
    int fullmove = 1;
    int value = 0;

    if(iss >> value) halfmove = value;
    if(iss >> value) fullmove = value;

    clearBoard();

    //Piece placement
    int rank = 7;
    int file = 0;

    for(char c : placement){
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
    whiteToMove = (side == "w");


    //Castling ability test
    castlingRights = 0;

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


    enPassantSquare = enPassantTarget;
    halfmoveClock = halfmove;
    fullmoveCounter = fullmove;

    hash = Zobrist::compute(*this);
    enPassantKey = Zobrist::enPassantKey(*this);
}