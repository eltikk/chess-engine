#include "TranspositionTable.h"
#include <algorithm>

namespace {

constexpr int mateScore = 100000;
constexpr int mateThreshold = mateScore - 1000;

// Mate scores are stored relative to the node rather than the root, so they stay correct when the
// same position is reached at a different ply.
int scoreToTable(int score, int ply) {
    if (score >= mateThreshold) {
        return score + ply;
    }

    if (score <= -mateThreshold) {
        return score - ply;
    }

    return score;
}

int scoreFromTable(int score, int ply) {
    if (score >= mateThreshold) {
        return score - ply;
    }

    if (score <= -mateThreshold) {
        return score + ply;
    }

    return score;
}

} // namespace

TranspositionTable::TranspositionTable(std::size_t sizeMb) { resize(sizeMb); }

void TranspositionTable::resize(std::size_t sizeMb) {
    const std::size_t maxEntries = sizeMb * 1024 * 1024 / sizeof(Entry);

    std::size_t count = 1;

    while (count * 2 <= maxEntries) {
        count *= 2;
    }

    if (count == entries.size()) {
        return;
    }

    entries = std::vector<Entry>();
    entries.resize(count);
    hits = 0;
    cutoffs = 0;
}

void TranspositionTable::clear() {
    std::fill(entries.begin(), entries.end(), Entry{});
    hits = 0;
    cutoffs = 0;
}

bool TranspositionTable::probe(std::uint64_t key, int depth, int alpha, int beta, int ply,
                               int& score, Move& bestMove) {
    const Entry& entry = entries[key & (entries.size() - 1)];

    if (!entry.valid || entry.key != key) {
        return false;
    }

    ++hits;
    bestMove = entry.bestMove;

    if (entry.depth < depth) {
        return false;
    }

    const int storedScore = scoreFromTable(entry.score, ply);

    if (entry.bound == Bound::Exact || (entry.bound == Bound::Lower && storedScore >= beta) ||
        (entry.bound == Bound::Upper && storedScore <= alpha)) {
        ++cutoffs;
        score = storedScore;
        return true;
    }

    return false;
}

void TranspositionTable::store(std::uint64_t key, int depth, int score, Bound bound,
                               const Move& bestMove, int ply) {
    Entry& entry = entries[key & (entries.size() - 1)];

    // Different positions always replace; the same position keeps its deeper result.
    if (entry.valid && entry.key == key && entry.depth > depth) {
        return;
    }

    entry = Entry{key, scoreToTable(score, ply), bestMove, depth, bound, true};
}
