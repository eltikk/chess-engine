#include "Board.h"
#include "MoveGenerator.h"
#include "MoveList.h"
#include "Search.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    MoveGenerator::init();

    Board board;
    std::string command;

    while (std::getline(std::cin, command)) {

        if (command == "uci") {
            std::cout << "id name EltiChess\n";
            std::cout << "id author Elti\n";
            std::cout << "uciok\n" << std::flush;
        }

        else if (command == "isready") {
            std::cout << "readyok\n" << std::flush;
        }

        else if (command == "ucinewgame") {
            board.setStartingPosition();
        }

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

            if (!(input >> token)) {
                continue;
            }

            if (token != "moves") {
                std::cerr << "Expected 'moves' keyword\n";
                continue;
            }

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

            auto bestMove =
                Search::findBestMove(board, depth, [](const Search::IterationInfo& info) {
                    constexpr int mateScore = 100000;
                    constexpr int mateThreshold = mateScore - 1000;

                    std::cout << "info depth " << info.depth;

                    if (std::abs(info.score) >= mateThreshold) {
                        int pliesToMate = mateScore - std::abs(info.score);
                        int movesToMate = (pliesToMate + 1) / 2;

                        if (info.score < 0) {
                            movesToMate = -movesToMate;
                        }

                        std::cout << " score mate " << movesToMate;
                    } else {
                        std::cout << " score cp " << info.score;
                    }

                    std::cout << " nodes " << info.nodes << " time " << info.timeMs << " nps "
                              << info.nps << " pv " << info.bestMove.toUCI() << '\n'
                              << std::flush;
                });

            if (bestMove.has_value()) {
                std::cout << "bestmove " << bestMove->toUCI() << '\n';
            } else {
                std::cout << "bestmove 0000\n";
            }

            std::cout << std::flush;
        }

        else if (command == "quit") {
            break;
        }
    }

    return 0;
}