import re
import subprocess
import sys

engine = sys.argv[1]


def run_uci(commands):
    result = subprocess.run(
        [engine],
        input="\n".join(commands + ["quit"]) + "\n",
        text=True,
        capture_output=True,
        timeout=30,
        check=True,
    )

    return result.stdout.strip().splitlines()


# Test 1: UCI handshake
output = run_uci(["uci", "isready"])

assert output == [
    "id name EltiChess",
    "id author Elti",
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

print("All UCI integration tests passed!")
