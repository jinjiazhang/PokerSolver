#pragma once
#include "cards.h"
#include <array>

namespace poker {

// ============================================================================
// Fast hand evaluator using precomputed lookup tables
// Returns a hand rank value where HIGHER = BETTER hand
//
// Hand categories (from high nibble of rank):
//   0 = High Card
//   1 = One Pair
//   2 = Two Pair
//   3 = Three of a Kind
//   4 = Straight
//   5 = Flush
//   6 = Full House
//   7 = Four of a Kind
//   8 = Straight Flush
//
// The full rank value can be compared directly to determine winners.
// ============================================================================

constexpr int HAND_HIGH_CARD       = 0;
constexpr int HAND_ONE_PAIR        = 1;
constexpr int HAND_TWO_PAIR        = 2;
constexpr int HAND_THREE_OF_A_KIND = 3;
constexpr int HAND_STRAIGHT        = 4;
constexpr int HAND_FLUSH           = 5;
constexpr int HAND_FULL_HOUSE      = 6;
constexpr int HAND_FOUR_OF_A_KIND  = 7;
constexpr int HAND_STRAIGHT_FLUSH  = 8;

class HandEvaluator {
public:
    HandEvaluator();

    // Evaluate best 5-card hand from any number of cards (5, 6, or 7)
    // board_mask: bitmask of community cards
    // hand: the player's hole cards
    // Returns: hand rank (higher = better)
    uint16_t evaluate(CardMask board_mask, int board_count, const Hand& hand) const;

    // Evaluate exactly 5 cards
    uint16_t evaluate5(int c0, int c1, int c2, int c3, int c4) const;

    // Get category from rank
    static int rank_category(uint16_t rank) {
        return rank >> 12;
    }

    // Compare two hand ranks: >0 = a wins, <0 = b wins, 0 = tie  
    static int compare(uint16_t a, uint16_t b) {
        return static_cast<int>(a) - static_cast<int>(b);
    }

    static const char* category_name(int cat);

private:
    // Lookup tables
    // flush_table: indexed by bit pattern of 5 ranks -> hand rank
    std::array<uint16_t, 8192>  flush_table_;
    // unique5_table: indexed by a hash of 5 unique ranks -> hand rank
    std::array<uint16_t, 8192>  unique5_table_;
    // pairs_table: indexed by rank pattern hash -> hand rank for paired boards
    // Uses a hash table for fast lookup
    static constexpr int PAIRS_TABLE_SIZE = 1 << 16;
    std::array<uint16_t, PAIRS_TABLE_SIZE> pairs_table_;

    void init_tables();
    uint16_t eval_5cards(int c0, int c1, int c2, int c3, int c4) const;

    // Rank bitmask for flush/straight detection
    static uint32_t rank_bit(int rank) { return 1u << rank; }
};

// Global evaluator instance (initialized once)
const HandEvaluator& get_evaluator();

} // namespace poker
