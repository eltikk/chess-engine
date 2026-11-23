#include "Board.h"
#include "MoveGenerator.h"
#include "MoveList.h"
#include "Search.h"
#include "TranspositionTable.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::mutex outputMutex;

// Both the input loop and the search thread write to stdout, so every line goes through here
void send(const std::string& line) {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::cout << line << '\n' << std::flush;
}

std::string formatInfo(const Search::IterationInfo& info) {
    constexpr int mateScore = 100000;
    constexpr int mateThreshold = mateScore - 1000;

    std::ostringstream output;
    output << "info depth " << info.depth;

    if (std::abs(info.score) >= mateThreshold) {
        int pliesToMate = mateScore - std::abs(info.score);
        int movesToMate = (pliesToMate + 1) / 2;

        if (info.score < 0) {
            movesToMate = -movesToMate;
        }

        output << " score mate " << movesToMate;
    } else {
        output << " score cp " << info.score;
    }

    output << " nodes " << info.nodes << " time " << info.timeMs << " nps " << info.nps << " pv "
           << info.bestMove.toUCI();

    return output.str();
}

struct GoCommand {
    Search::Limits limits;
    bool infinite = false;
};

// Unknown tokens, such as those of unsupported go parameters, are skipped
GoCommand parseGo(const std::string& command, bool whiteToMove,
                  Search::Clock::time_point startTime) {
    std::istringstream input(command);
    std::string token;

    input >> token; // Skip "go"

    GoCommand go;
    std::optional<std::int64_t> depth;
    std::optional<std::int64_t> moveTimeMs;
    std::optional<std::int64_t> whiteTimeMs;
    std::optional<std::int64_t> blackTimeMs;
    std::int64_t whiteIncMs = 0;
    std::int64_t blackIncMs = 0;
    std::int64_t movesToGo = 0;

    while (input >> token) {
        std::int64_t value = 0;

        if (token == "infinite") {
            go.infinite = true;
        } else if (token == "depth" && input >> value) {
            depth = value;
        } else if (token == "movetime" && input >> value) {
            moveTimeMs = value;
        } else if (token == "wtime" && input >> value) {
            whiteTimeMs = value;
        } else if (token == "btime" && input >> value) {
            blackTimeMs = value;
        } else if (token == "winc" && input >> value) {
            whiteIncMs = value;
        } else if (token == "binc" && input >> value) {
            blackIncMs = value;
        } else if (token == "movestogo" && input >> value) {
            movesToGo = value;
        }
    }

    const std::optional<std::int64_t> timeMs = whiteToMove ? whiteTimeMs : blackTimeMs;
    const std::int64_t incMs = whiteToMove ? whiteIncMs : blackIncMs;

    // Without any limit the search only ends on stop
    if (!depth.has_value() && !moveTimeMs.has_value() && !timeMs.has_value()) {
        go.infinite = true;
    }

    if (depth.has_value()) {
        go.limits.depth =
            static_cast<int>(std::clamp<std::int64_t>(*depth, 1, Search::maxSearchDepth));
    }

    if (go.infinite) {
        return go;
    }

    if (moveTimeMs.has_value()) {
        const auto budget = std::chrono::milliseconds(
            std::max<std::int64_t>(*moveTimeMs - Search::moveOverheadMs, 1));

        go.limits.softDeadline = startTime + budget;
        go.limits.hardDeadline = startTime + budget;
    }

    if (timeMs.has_value()) {
        const auto budget =
            std::chrono::milliseconds(Search::allocateTimeMs(*timeMs, incMs, movesToGo));

        go.limits.softDeadline = std::min(go.limits.softDeadline, startTime + budget / 2);
        go.limits.hardDeadline = std::min(go.limits.hardDeadline, startTime + budget);
    }

    return go;
}

// Builds the position on a separate board, so a rejected command leaves the current one untouched
std::optional<Board> parsePosition(const std::string& command) {
    std::istringstream input(command);
    std::string token;

    input >> token; // Skip "position"
    input >> token; // Read "startpos" or "fen"

    Board position;

    if (token == "startpos") {
        if (input >> token && token != "moves") {
            std::cerr << "Expected 'moves' keyword\n";
            return std::nullopt;
        }
    }

    else if (token == "fen") {
        // The FEN runs up to the optional moves keyword; loadFEN validates its four to six fields
        std::string fen;

        while (input >> token && token != "moves") {
            if (!fen.empty()) {
                fen += " ";
            }

            fen += token;
        }

        try {
            position.loadFEN(fen);
        } catch (const std::exception& e) {
            std::cerr << "Invalid FEN: " << e.what() << '\n';
            return std::nullopt;
        }
    }

    else {
        std::cerr << "Unsupported position format\n";
        return std::nullopt;
    }

    while (input >> token) {
        MoveList legalMoves;
        MoveGenerator::generateLegalMoves(position, legalMoves);

        bool found = false;

        for (int i = 0; i < legalMoves.count; ++i) {
            const Move& move = legalMoves.moves[i];

            if (move.toUCI() == token) {
                UndoState undo;
                position.makeMove(move, undo);

                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "Invalid move: " << token << '\n';
            return std::nullopt;
        }
    }

    return position;
}

constexpr std::int64_t maxHashMb = 1024;

// Returns the requested size of "setoption name Hash value <MB>", or nothing for other options.
// Option names are case-insensitive.
std::optional<std::size_t> parseHashOption(const std::string& command) {
    std::istringstream input(command);
    std::string setoption;
    std::string nameKeyword;
    std::string name;
    std::string valueKeyword;
    std::int64_t value = 0;

    if (!(input >> setoption >> nameKeyword >> name >> valueKeyword >> value) ||
        nameKeyword != "name" || valueKeyword != "value") {
        return std::nullopt;
    }

    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (name != "hash") {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::clamp<std::int64_t>(value, 1, maxHashMb));
}

} // namespace

int main() {
    MoveGenerator::init();

    Board board;
    std::string command;

    // Kept across searches and moves of a game; only changed while no search is running
    TranspositionTable tt;

    std::thread searchThread;
    std::atomic<bool> stopRequested = false;
    std::atomic<bool> quitting = false;

    // Returns once the running search, if any, has finished and sent its bestmove
    auto stopSearch = [&]() {
        stopRequested = true;
        stopRequested.notify_all();

        if (searchThread.joinable()) {
            searchThread.join();
        }
    };

    while (std::getline(std::cin, command)) {

        if (command == "uci") {
            send("id name EltiChess");
            send("id author Elti");
            send("option name Hash type spin default " +
                 std::to_string(TranspositionTable::defaultSizeMb) + " min 1 max " +
                 std::to_string(maxHashMb));
            send("uciok");
        }

        else if (command == "isready") {
            send("readyok");
        }

        // Neither is expected during a search, but if one arrives, the search ends first
        else if (command == "ucinewgame") {
            stopSearch();
            board.setStartingPosition();
            tt.clear();
        }

        else if (command.rfind("setoption ", 0) == 0) {
            if (const std::optional<std::size_t> sizeMb = parseHashOption(command)) {
                stopSearch();
                tt.resize(*sizeMb);
            }
        }

        else if (command.rfind("position ", 0) == 0) {
            if (std::optional<Board> position = parsePosition(command)) {
                board = *position;
            }
        }

        else if (command == "go" || command.rfind("go ", 0) == 0) {
            const auto startTime = Search::Clock::now();

            // A go sent while searching ends the previous search first
            stopSearch();
            stopRequested = false;

            const GoCommand go = parseGo(command, board.isWhiteToMove(), startTime);

            // The search gets its own board, so later position commands cannot race with it
            searchThread =
                std::thread([searchBoard = board, go, &stopRequested, &quitting, &tt]() mutable {
                    auto bestMove = Search::findBestMove(
                        searchBoard, go.limits, &stopRequested,
                        [](const Search::IterationInfo& info) { send(formatInfo(info)); }, &tt);

                    // In infinite mode bestmove is only sent once the GUI asks for it
                    if (go.infinite) {
                        while (!stopRequested) {
                            stopRequested.wait(false);
                        }
                    }

                    if (quitting) {
                        return;
                    }

                    send(bestMove.has_value() ? "bestmove " + bestMove->toUCI() : "bestmove 0000");
                });
        }

        else if (command == "stop") {
            stopSearch();
        }

        else if (command == "quit") {
            break;
        }
    }

    // Reached on quit or when input closes; the search is abandoned without a bestmove
    quitting = true;
    stopSearch();

    return 0;
}
