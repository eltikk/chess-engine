#pragma once

#include "Move.h"
#include <cstddef>
#include <cstdint>
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
    };

    static constexpr std::size_t defaultSizeMb = 64;

    explicit TranspositionTable(std::size_t sizeMb = defaultSizeMb);

    // Frees the old entries before allocating the new ones; keeps them if the entry count is
    // unchanged
    void resize(std::size_t sizeMb);
    void clear();

    // Returns true when the stored score can be returned for this window. The stored best move is
    // written to bestMove whenever the full key matches, even if the score is unusable.
    bool probe(std::uint64_t key, int depth, int alpha, int beta, int ply, int& score,
               Move& bestMove);
    void store(std::uint64_t key, int depth, int score, Bound bound, const Move& bestMove, int ply);

    std::size_t size() const { return entries.size(); }
    std::uint64_t getHits() const { return hits; }
    std::uint64_t getCutoffs() const { return cutoffs; }

  private:
    std::vector<Entry> entries;
    std::uint64_t hits = 0;
    std::uint64_t cutoffs = 0;
};
