import os
import queue
import re
import subprocess
import sys
import threading
import time

engine = sys.argv[1]

# Allowed overrun of a time limit; slow builds such as sanitizer builds may need more
TIME_MARGIN = float(os.environ.get("UCI_TIME_MARGIN", "0.5"))

BESTMOVE = re.compile(r"bestmove (?:[a-h][1-8][a-h][1-8][qrbn]?|0000)")


class Engine:
    # stderr can be silenced for tests that send deliberately invalid commands
    def __init__(self, stderr=None):
        self.process = subprocess.Popen(
            [engine],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=stderr,
            text=True,
            bufsize=1,
        )
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.process.stdout:
            self.lines.put(line.rstrip("\n"))
        self.lines.put(None)

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    # Returns every line up to and including the first one matching pattern
    def read_until(self, pattern, timeout=10):
        deadline = time.monotonic() + timeout
        output = []

        while True:
            remaining = deadline - time.monotonic()
            assert remaining > 0, f"timed out waiting for {pattern!r}, got {output}"

            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                continue

            assert line is not None, f"engine exited waiting for {pattern!r}, got {output}"
            output.append(line)

            if re.fullmatch(pattern, line):
                return output

    # Output that arrives within the given time, without waiting for anything in particular
    def read_available(self, wait=0.2):
        time.sleep(wait)
        output = []

        while not self.lines.empty():
            line = self.lines.get()
            if line is not None:
                output.append(line)

        return output

    def search(self, command, timeout=10):
        self.send(command)
        return self.read_until(BESTMOVE.pattern, timeout)

    def quit(self, command="quit", timeout=5):
        if command is None:
            self.process.stdin.close()
        else:
            self.send(command)

        assert self.process.wait(timeout=timeout) == 0
        self.reader.join(timeout=timeout)

        output = []
        while not self.lines.empty():
            line = self.lines.get()
            if line is not None:
                output.append(line)

        return output


# Sends the commands one at a time, waiting for each command's reply before the next
def run_uci(commands):
    session = Engine()
    output = []

    for command in commands:
        if command == "uci":
            session.send(command)
            output += session.read_until("uciok")
        elif command == "isready":
            session.send(command)
            output += session.read_until("readyok")
        elif command.startswith("go"):
            output += session.search(command)
        else:
            session.send(command)

    output += session.quit()
    return output


# Test 1: UCI handshake
output = run_uci(["uci", "isready"])

assert output == [
    "id name EltiChess",
    "id author Elti",
    "option name Hash type spin default 64 min 1 max 1024",
    "uciok",
    "readyok",
]

# Test 2: Starting position
output = run_uci(
    [
        "position startpos",
        "go depth 3",
    ]
)


def assert_search_output(output, depth):
    assert len(output) == depth + 1

    for i in range(depth):
        assert re.fullmatch(
            rf"info depth {i + 1} score (?:cp|mate) -?\d+ nodes \d+ time \d+ nps \d+ pv [a-h][1-8][a-h][1-8][qrbn]?",
            output[i],
        )

    assert re.fullmatch(
        r"bestmove [a-h][1-8][a-h][1-8][qrbn]?",
        output[-1],
    )


assert_search_output(output, 3)

# Test 3: Capture a free queen
output = run_uci(
    [
        "position fen 4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1",
        "go depth 3",
    ]
)

assert_search_output(output, 3)
assert output[-1] == "bestmove e2e4"

# Test 4: Fool's Mate
output = run_uci(
    [
        "position startpos moves f2f3 e7e5 g2g4 d8h4",
        "go depth 3",
    ]
)

assert output == ["bestmove 0000"]

# Test 5: Search statistics
output = run_uci(
    [
        "position startpos",
        "go depth 4",
    ]
)

assert_search_output(output, 4)

nodes = []
times = []

for line in output[:-1]:
    match = re.search(
        r"nodes (\d+) time (\d+) nps (\d+)",
        line,
    )

    assert match is not None

    nodes.append(int(match.group(1)))
    times.append(int(match.group(2)))

# Cumulative nodes must increase with each iteration
assert all(nodes[i] > nodes[i - 1] for i in range(1, len(nodes)))

# Cumulative elapsed time must not decrease
assert all(times[i] >= times[i - 1] for i in range(1, len(times)))

# Test 6: Checkmate takes precedence over the fifty-move rule
# Every move reaches the hundredth halfmove; only g6g7 mates instead of drawing
output = run_uci(
    [
        "position fen 7k/8/5KQ1/8/8/8/8/8 w - - 99 1",
        "go depth 2",
    ]
)

assert_search_output(output, 2)

assert "score mate 1 " in output[-2]
assert output[-1] == "bestmove g6g7"

# Test 7: Threefold repetition through game moves
# The bare king is lost, but g1h1 repeats a position for the third time
output = run_uci(
    [
        "position fen 4k3/8/8/8/8/8/q7/7K w - - 0 1 moves "
        "h1g1 e8d8 g1h1 d8e8 h1g1 e8d8 g1h1 d8e8 h1g1 e8d8",
        "go depth 3",
    ]
)

assert_search_output(output, 3)

assert "score cp 0 " in output[-2]
assert output[-1] == "bestmove g1h1"

# The same position without its history is lost
output = run_uci(
    [
        "position fen 3k4/8/8/8/8/8/q7/6K1 w - - 0 1",
        "go depth 3",
    ]
)

assert_search_output(output, 3)

score = int(re.search(r"score cp (-?\d+)", output[-2]).group(1))
assert score < -500

# Test 8: Fifty-move rule
output = run_uci(
    [
        "position fen 7k/8/8/8/8/8/1Q6/K7 w - - 99 80",
        "go depth 2",
    ]
)

assert_search_output(output, 2)
assert "score cp 0 " in output[-2]



STARTPOS_MOVES = {
    "a2a3", "a2a4", "b2b3", "b2b4", "c2c3", "c2c4", "d2d3", "d2d4", "e2e3", "e2e4",
    "f2f3", "f2f4", "g2g3", "g2g4", "h2h3", "h2h4", "b1a3", "b1c3", "g1f3", "g1h3",
}


def bestmoves(output):
    return [line for line in output if BESTMOVE.fullmatch(line)]


# The final move must be the one reported by the last completed iteration
def assert_completed_search(output):
    assert bestmoves(output) == [output[-1]]

    infos = [line for line in output if line.startswith("info ")]
    if infos:
        assert output[-1].split()[1] == infos[-1].split(" pv ")[1]


def timed_search(session, command):
    start = time.monotonic()
    output = session.search(command)
    return output, time.monotonic() - start


# Test 9: Fixed move time
session = Engine()
session.send("position startpos")
output, elapsed = timed_search(session, "go movetime 300")

assert elapsed < 0.3 + TIME_MARGIN
assert_completed_search(output)
assert output[-1].split()[1] in STARTPOS_MOVES
assert session.quit() == []

# Test 10: The clock of the side to move is used
session = Engine()

session.send("position startpos")
output, elapsed = timed_search(session, "go wtime 200 btime 100000")
assert elapsed < TIME_MARGIN
assert_completed_search(output)

session.send("position startpos moves e2e4")
output, elapsed = timed_search(session, "go wtime 100000 btime 200")
assert elapsed < TIME_MARGIN
assert_completed_search(output)

assert session.quit() == []

# Test 11: Tiny, empty and negative clocks still return a legal move in time
session = Engine()

for command in [
    "go wtime 0 btime 0",
    "go wtime 1 btime 1",
    "go wtime 30 btime 30 winc 0 binc 0",
    "go wtime 10 btime 10 winc 5000 binc 5000",
    "go wtime -100 btime -100",
    "go movetime 0",
    "go movetime 1",
]:
    session.send("position startpos")
    output, elapsed = timed_search(session, command)

    assert elapsed < TIME_MARGIN, command
    assert_completed_search(output)
    assert output[-1].split()[1] in STARTPOS_MOVES, command

assert session.quit() == []

# Test 12: movestogo spends the whole remaining time on the last move before the time control
session = Engine()
session.send("position startpos")
output, elapsed = timed_search(session, "go wtime 300 btime 300 movestogo 1")

assert elapsed < 0.3 + TIME_MARGIN
assert_completed_search(output)
assert session.quit() == []

# Test 13: Depth and time limits combine, whichever is reached first
output = run_uci(["position startpos", "go depth 2 movetime 10000"])
assert_search_output(output, 2)

session = Engine()
session.send("position startpos")
output, elapsed = timed_search(session, "go depth 64 movetime 200")

assert elapsed < 0.2 + TIME_MARGIN
assert_completed_search(output)
assert session.quit() == []

# Unsupported go parameters are ignored
output = run_uci(["position startpos", "go searchmoves e2e4 nodes 100 depth 1"])
assert_search_output(output, 1)

# Test 14: isready is answered during an infinite search, which ends only on stop
session = Engine()
session.send("position startpos")
session.send("go infinite")
time.sleep(0.3)

session.send("isready")
output = session.read_until("readyok", timeout=1)
assert bestmoves(output) == []

session.send("stop")
output += session.read_until(BESTMOVE.pattern, timeout=TIME_MARGIN)
assert_completed_search(output)

session.send("isready")
assert session.read_until("readyok") == ["readyok"]
assert session.quit() == []

# Test 15: An infinite search that reaches its depth limit still waits for stop
session = Engine()
session.send("position fen 8/8/4k3/8/8/4K3/8/8 w - - 0 1")
session.send("go infinite")

output = session.read_available(0.5)
assert "info depth 64 " in output[-1]
assert bestmoves(output) == []

session.send("stop")
output += session.read_until(BESTMOVE.pattern, timeout=1)
assert_completed_search(output)
assert session.quit() == []

# Test 16: An infinite search without legal moves also waits for stop
session = Engine()
session.send("position startpos moves f2f3 e7e5 g2g4 d8h4")
session.send("go infinite")

assert session.read_available(0.3) == []

session.send("stop")
assert session.read_until(BESTMOVE.pattern, timeout=1) == ["bestmove 0000"]
assert session.quit() == []

# Test 17: Stopping immediately still returns exactly one legal move per search
session = Engine()

for i in range(25):
    session.send("position startpos")
    session.send("go infinite" if i % 2 == 0 else "go")
    session.send("stop")

    output = session.read_until(BESTMOVE.pattern, timeout=1)
    assert_completed_search(output)
    assert output[-1].split()[1] in STARTPOS_MOVES

session.send("isready")
assert session.read_until("readyok") == ["readyok"]
assert session.quit() == []

# Test 18: A go during a search stops the previous search first
session = Engine()
session.send("position startpos")
session.send("go infinite")
time.sleep(0.1)
session.send("go depth 1")

first = session.read_until(BESTMOVE.pattern, timeout=1)
second = session.read_until(BESTMOVE.pattern, timeout=1)

assert_completed_search(first)
assert_search_output(second, 1)

session.send("isready")
assert session.read_until("readyok") == ["readyok"]
assert session.quit() == []

# Test 19: stop without a search does nothing
session = Engine()
session.send("stop")
session.send("stop")
session.send("isready")

assert session.read_until("readyok") == ["readyok"]
assert session.quit() == []

# Test 20: A search uses the position it was started with
session = Engine()
session.send("position startpos")
session.send("go infinite")
session.send("position fen 4k3/8/8/8/4q3/8/4R3/4K3 w - - 0 1")
time.sleep(0.1)
session.send("stop")

output = session.read_until(BESTMOVE.pattern, timeout=1)
assert output[-1].split()[1] in STARTPOS_MOVES

assert session.search("go depth 3")[-1] == "bestmove e2e4"
assert session.quit() == []

# Test 21: A mate in one is found with little time
session = Engine()
session.send("position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1")
output, elapsed = timed_search(session, "go movetime 200")

assert elapsed < 0.2 + TIME_MARGIN
assert output[-1] == "bestmove g6g7"
assert session.quit() == []

# Test 22: quit or closed input during a search exits promptly without a bestmove
for command in ["quit", None]:
    for go in ["go infinite", "go movetime 10000", "go depth 64"]:
        session = Engine()
        session.send("position startpos")
        session.send(go)
        time.sleep(0.1)

        start = time.monotonic()
        output = session.quit(command)

        assert time.monotonic() - start < TIME_MARGIN, (command, go)
        assert bestmoves(output) == [], (command, go)

# Test 23: A go without limits is an infinite search, even once it reaches the maximum depth
session = Engine()
session.send("position fen 8/8/4k3/8/8/4K3/8/8 w - - 0 1")
session.send("go")

output = session.read_available(0.5)
assert "info depth 64 " in output[-1]
assert bestmoves(output) == []

session.send("stop")
output += session.read_until(BESTMOVE.pattern, timeout=1)
assert_completed_search(output)

session.send("position startpos moves f2f3 e7e5 g2g4 d8h4")
session.send("go")
assert session.read_available(0.3) == []

session.send("stop")
assert session.read_until(BESTMOVE.pattern, timeout=1) == ["bestmove 0000"]

# An explicit depth limit still ends the search without stop
session.send("position fen 8/8/4k3/8/8/4K3/8/8 w - - 0 1")
output = session.search("go depth 64", timeout=1)
assert_search_output(output, 64)

assert session.quit() == []

# Test 24: FENs with four, five or six fields, with and without moves
FREE_QUEEN = "4k3/8/8/8/4q3/8/4R3/4K3 w - -"

for fen in [FREE_QUEEN, FREE_QUEEN + " 0", FREE_QUEEN + " 0 1"]:
    # An empty move list after the keyword is allowed
    for command in [f"position fen {fen}", f"position fen {fen} moves"]:
        output = run_uci([command, "go depth 3"])
        assert_search_output(output, 3)
        assert output[-1] == "bestmove e2e4", command

    # After Rxe4 black is to move, so the best move must be a move of the black king
    output = run_uci([f"position fen {fen} moves e2e4", "go depth 3"])
    assert_search_output(output, 3)
    assert re.fullmatch(r"bestmove e8[a-h][1-8]", output[-1]), (fen, output[-1])

# A four-field FEN with a whole game history: g1h1 repeats a position for the third time
output = run_uci(
    [
        "position fen 4k3/8/8/8/8/8/q7/7K w - - moves "
        "h1g1 e8d8 g1h1 d8e8 h1g1 e8d8 g1h1 d8e8 h1g1 e8d8",
        "go depth 3",
    ]
)

assert_search_output(output, 3)
assert "score cp 0 " in output[-2]
assert output[-1] == "bestmove g1h1"

# Mate in one from a four-field FEN
output = run_uci(["position fen 7k/8/5KQ1/8/8/8/8/8 w - -", "go depth 2"])
assert_search_output(output, 2)
assert output[-1] == "bestmove g6g7"

# Test 25: Malformed position commands are rejected and leave the previous position in place
session = Engine(stderr=subprocess.DEVNULL)

for command in [
    # Malformed FENs
    "position fen",
    "position fen moves e2e4",
    "position fen 4k3/8/8/8/4q3/8/4R3",
    "position fen 4k3/8/8/8/4q3/8/4R3/4K3 w -",
    "position fen 4k3/8/8/8/8/8/8/8 w - - 0 1",
    "position fen 4k3/8/8/8/4q3/8/4R3/4K3/8 w - - 0 1",
    "position fen 4k3/8/8/8/4q3/8/4R3/4K3 x - - 0 1",
    # Moves without the moves keyword are not taken as move counters
    "position fen 4k3/8/8/8/4q3/8/4R3/4K3 b - - e4e2",
    "position fen 4k3/8/8/8/4q3/8/4R3/4K3 b - - 0 1 e4e2",
    "position startpos e2e4",
    # An illegal move discards the moves before it as well
    "position startpos moves e2e4 e7e5 e1e3",
    "position fen 4k3/8/8/8/4q3/8/4R3/4K3 b - - moves e8d8 e2e4 e4e1",
    "position unknown",
]:
    session.send(f"position fen {FREE_QUEEN} 0 1")
    session.send(command)

    output = session.search("go depth 3")
    assert output[-1] == "bestmove e2e4", (command, output[-1])

    session.send("isready")
    assert session.read_until("readyok") == ["readyok"], command

assert session.quit() == []


def last_nodes(output):
    return int(re.search(r" nodes (\d+) ", [line for line in output if line.startswith("info ")][-1]).group(1))


def ready(session):
    session.send("isready")
    assert session.read_until("readyok") == ["readyok"]


# Test 26: The transposition table is kept between searches and cleared by ucinewgame
session = Engine()

session.send("position startpos")
first = session.search("go depth 6")
second = session.search("go depth 6")

session.send("ucinewgame")
ready(session)
session.send("position startpos")
after_new_game = session.search("go depth 6")

assert_search_output(first, 6)
assert_search_output(second, 6)
assert_search_output(after_new_game, 6)
assert last_nodes(second) < last_nodes(first)
assert last_nodes(after_new_game) == last_nodes(first)
assert after_new_game[-1] == first[-1]
assert session.quit() == []

# Test 27: The Hash option resizes the table, which starts empty unless its size is unchanged
KIWIPETE = "position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"


def fresh_search(options, position, go):
    session = Engine()
    for option in options:
        session.send(option)
    ready(session)
    session.send(position)
    output = session.search(go)
    assert session.quit() == []
    return output


default = fresh_search([], KIWIPETE, "go depth 4")
small = fresh_search(["setoption name Hash value 1"], KIWIPETE, "go depth 4")
assert last_nodes(small) != last_nodes(default)

# Option names are case-insensitive, and resizing drops the entries of earlier searches
session = Engine()
session.send("position startpos")
session.search("go depth 4")
session.send("setoption name hash value 1")
ready(session)
session.send(KIWIPETE)
resized = session.search("go depth 4")
assert last_nodes(resized) == last_nodes(small)

# The same size again keeps the entries
session.send("setoption name Hash value 1")
ready(session)
kept = session.search("go depth 4")
assert last_nodes(kept) < last_nodes(resized)

# Out-of-range values are clamped; malformed or unknown options are ignored
for command in [
    "setoption name Hash value 0",
    "setoption name Hash value 100000",
    "setoption name Hash value abc",
    "setoption name Hash",
    "setoption name Threads value 4",
    "setoption",
]:
    session.send(command)
    ready(session)
    session.send("position startpos")
    output = session.search("go depth 3")
    assert_search_output(output, 3)

assert session.quit() == []

session = Engine()
session.send("setoption name Hash value 64")
ready(session)
session.send("position startpos")
output = session.search("go depth 6")
assert_search_output(output, 6)
assert session.quit() == []

# Test 28: ucinewgame or setoption during a search ends the search before changing the table
for command in ["ucinewgame", "setoption name Hash value 32"]:
    session = Engine()
    session.send("position startpos")
    session.send("go infinite")
    session.read_until(r"info depth 2 .*")
    session.send(command)

    output = session.read_until(BESTMOVE.pattern)
    assert output[-1].split()[1] in STARTPOS_MOVES, command
    ready(session)

    session.send("position startpos")
    output = session.search("go depth 3")
    assert_search_output(output, 3)
    assert session.quit() == []

# Test 29: Entries from earlier searches keep mate distances and draws correct as the game moves on
session = Engine()

session.send("position fen k7/8/2K5/8/8/8/8/7R w - - 0 1")
output = session.search("go depth 5")
assert "score mate 2 " in output[-2]

session.send("position fen k7/8/2K5/8/8/8/8/7R w - - 0 1 moves c6b6 a8b8")
output = session.search("go depth 5")
assert "score mate 1 " in output[-2]
assert output[-1] == "bestmove h1h8"

session.send("position fen 4k3/8/8/8/8/8/q7/7K w - - 0 1")
output = session.search("go depth 4")
assert int(re.search(r"score cp (-?\d+)", output[-2]).group(1)) < -500

session.send(
    "position fen 4k3/8/8/8/8/8/q7/7K w - - 0 1 moves "
    "h1g1 e8d8 g1h1 d8e8 h1g1 e8d8 g1h1 d8e8 h1g1 e8d8"
)
output = session.search("go depth 4")
assert "score cp 0 " in output[-2]
assert output[-1] == "bestmove g1h1"

session.send("position fen 7k/8/8/8/8/8/1Q6/K7 w - - 0 80")
output = session.search("go depth 4")
assert int(re.search(r"score cp (-?\d+)", output[-2]).group(1)) > 500

session.send("position fen 7k/8/8/8/8/8/1Q6/K7 w - - 99 80")
output = session.search("go depth 4")
assert "score cp 0 " in output[-2]

assert session.quit() == []

# Test 30: A table filled at a low halfmove clock reports no mate that the fifty-move rule prevents
session = Engine()

# Black is mated in three moves by king and rook, none of them a capture or pawn move
session.send("position fen k7/8/2K5/8/8/8/8/1R6 b - - 0 1")
output = session.search("go depth 8")
assert "score mate -3 " in output[-2]

# From clock 95 the fifty-move rule ends the game before the mate
session.send("position fen k7/8/2K5/8/8/8/8/1R6 b - - 95 1")
output = session.search("go depth 4")
assert not any("score mate" in line for line in output if line.startswith("info")), output

# From clock 94 the mate lands on the hundredth halfmove and takes precedence
session.send("position fen k7/8/2K5/8/8/8/8/1R6 b - - 94 1")
output = session.search("go depth 6")
assert "score mate -3 " in output[-2]

# The same position reached through moves, with the clock counted from the game
session.send("position fen k7/8/2K5/8/8/8/8/1R6 b - - 91 1 moves a8a7 c6c7 a7a8 c7c6")
output = session.search("go depth 4")
assert not any("score mate" in line for line in output if line.startswith("info")), output

assert session.quit() == []

print("All UCI integration tests passed!")
