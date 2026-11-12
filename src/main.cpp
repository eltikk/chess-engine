#include "Board.h"
#include "MoveGenerator.h"
#include "MoveList.h"
#include "Search.h"

#include <iostream>
#include <sstream>
#include <string>

int main() {
    MoveGenerator::init();

    Board board;
    std::string command;

    while (std::getline(std::cin, command)) {

        // UCI initialization
        if (command == "uci") {
            std::cout << "id name EltiChess\n";
            std::cout << "id author Elti\n";
            std::cout << "uciok\n" << std::flush;
        }

        // Check if engine is ready
        else if (command == "isready") {
            std::cout << "readyok\n" << std::flush;
        }

        // Start a new game
        else if (command == "ucinewgame") {
            board.setStartingPosition();
        }

        // Set up a position
        else if (command.rfind("position ", 0) == 0) {
            std::istringstream input(command);
            std::string token;

            input >> token; // Skip "position"
            input >> token; // Read "startpos" or "fen"

            if (token == "startpos") {
                board.setStartingPosition();
            }

            else if (token == "fen") {
                std::string fen;
                std::string field;

                for (int i = 0; i < 6; ++i) {
                    if (!(input >> field)) {
                        fen.clear();
                        break;
                    }

                    if (i > 0) {
                        fen += " ";
                    }

                    fen += field;
                }

                if (fen.empty()) {
                    std::cerr << "Invalid FEN command\n";
                    continue;
                }

                try {
                    board.loadFEN(fen);
                } catch (const std::exception& e) {
                    std::cerr << "Invalid FEN: " << e.what() << '\n';
                    continue;
                }
            }

            else {
                std::cerr << "Unsupported position format\n";
                continue;
            }

            // Check whether moves follow the position
            if (!(input >> token)) {
                continue;
            }

            if (token != "moves") {
                std::cerr << "Expected 'moves' keyword\n";
                continue;
            }

            // Apply moves one by one
            while (input >> token) {
                MoveList legalMoves;
                MoveGenerator::generateLegalMoves(board, legalMoves);

                bool found = false;

                for (int i = 0; i < legalMoves.count; ++i) {
                    const Move& move = legalMoves.moves[i];

                    if (move.toUCI() == token) {
                        UndoState undo;
                        board.makeMove(move, undo);

                        found = true;
                        break;
                    }
                }

                if (!found) {
                    std::cerr << "Invalid move: " << token << '\n';
                    break;
                }
            }
        }

        else if (command.rfind("go ", 0) == 0) {
            std::istringstream input(command);
            std::string token;
            int depth;

            input >> token; // Skip "go"
            input >> token; // Read "depth"

            if (token != "depth" || !(input >> depth) || depth <= 0) {
                std::cerr << "Unsupported go command\n";
                continue;
            }

            auto bestMove = Search::findBestMove(board, depth);

            if (bestMove.has_value()) {
                std::cout << "bestmove " << bestMove->toUCI() << '\n';
            } else {
                std::cout << "bestmove 0000\n";
            }

            std::cout << std::flush;
        }

        // Exit engine
        else if (command == "quit") {
            break;
        }
    }

    return 0;
}