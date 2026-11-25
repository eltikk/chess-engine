#pragma once

#include "Move.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class TranspositionTable {
  public:
    enum class Bound : std::uint8_t { Exact, Lower, Upper };

    struct Entry {
        std::uint64_t key = 0;
        int score = 0;
        Move bestMove{};
        int depth = 0;
        Bound bound = Bound::Exact;
        bool valid = false;
        // The halfmove clock the score was searched with if it relied on a fifty-move draw,
        // otherwise anyClock
        std::uint8_t halfmoveClock = anyClock;
    };

    static constexpr std::size_t defaultSizeMb = 64;

    explicit TranspositionTable(std::size_t sizeMb = defaultSizeMb);

    // Frees the old entries before allocating the new ones; keeps them if the entry count is
    // unchanged
    void resize(std::size_t sizeMb);
    void clear();

    // Stored in Entry::halfmoveClock for scores that hold at any clock
    static constexpr std::uint8_t anyClock = std::numeric_limits<std::uint8_t>::max();
    // Passed to store for a score that did not rely on a fifty-move draw
    static constexpr int noClock = -1;
    static constexpr int fiftyMoveLimit = 100;

    // Returns true when the stored score can be returned for this window at this halfmove clock.
    // The stored best move is written to bestMove whenever the full key matches, even if the score
    // is unusable.
    //
    // The clock is not part of the key. A score that relied on a fifty-move draw is only returned
    // at the clock it was searched with, which clockDependent then reports. A mate score is only
    // returned if its mate lands by the fifty-move limit, a sufficient but conservative condition:
    // lines that capture or move a pawn could reach further. Any other score is only returned if
    // a search of the requested depth stays short of the limit.
    bool probe(std::uint64_t key, int depth, int alpha, int beta, int ply, int& score,
               Move& bestMove, int halfmoveClock = 0, bool* clockDependent = nullptr);
    // A score that relied on a fifty-move draw passes the clock it was searched with; clocks from
    // anyClock up are not stored.
    void store(std::uint64_t key, int depth, int score, Bound bound, const Move& bestMove, int ply,
               int halfmoveClock = noClock);

    std::size_t size() const { return entries.size(); }
    // Probes that found an entry for the position, whether or not its score was usable
    std::uint64_t getHits() const { return hits; }
    // Probes whose stored score was returned
    std::uint64_t getCutoffs() const { return cutoffs; }

  private:
    std::vector<Entry> entries;
    std::uint64_t hits = 0;
    std::uint64_t cutoffs = 0;
};
