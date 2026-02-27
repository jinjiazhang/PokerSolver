#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <cassert>

namespace poker {

// ============================================================================
// Card representation: 0-51
// rank = card / 4  (0=2, 1=3, ..., 12=A)
// suit = card % 4  (0=club, 1=diamond, 2=heart, 3=spade)
// ============================================================================

constexpr int NUM_CARDS = 52;
constexpr int NUM_RANKS = 13;
constexpr int NUM_SUITS = 4;

// Rank constants
constexpr int RANK_2 = 0;
constexpr int RANK_3 = 1;
constexpr int RANK_4 = 2;
constexpr int RANK_5 = 3;
constexpr int RANK_6 = 4;
constexpr int RANK_7 = 5;
constexpr int RANK_8 = 6;
constexpr int RANK_9 = 7;
constexpr int RANK_T = 8;
constexpr int RANK_J = 9;
constexpr int RANK_Q = 10;
constexpr int RANK_K = 11;
constexpr int RANK_A = 12;

// Suit constants
constexpr int SUIT_CLUB    = 0;
constexpr int SUIT_DIAMOND = 1;
constexpr int SUIT_HEART   = 2;
constexpr int SUIT_SPADE   = 3;

inline constexpr int make_card(int rank, int suit) {
    return rank * 4 + suit;
}

inline constexpr int card_rank(int card) {
    return card / 4;
}

inline constexpr int card_suit(int card) {
    return card % 4;
}

// Card to string conversion
inline std::string card_to_string(int card) {
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "cdhs";
    return {ranks[card_rank(card)], suits[card_suit(card)]};
}

// String to card conversion
int string_to_card(const std::string& s);

// ============================================================================
// 64-bit card mask for fast set operations
// ============================================================================
using CardMask = uint64_t;

inline constexpr CardMask card_mask(int card) {
    return 1ULL << card;
}

inline constexpr bool mask_has_card(CardMask mask, int card) {
    return (mask & card_mask(card)) != 0;
}

inline constexpr CardMask mask_add_card(CardMask mask, int card) {
    return mask | card_mask(card);
}

inline constexpr CardMask mask_remove_card(CardMask mask, int card) {
    return mask & ~card_mask(card);
}

inline int mask_count(CardMask mask) {
#if defined(_MSC_VER)
    return static_cast<int>(__popcnt64(mask));
#else
    return __builtin_popcountll(mask);
#endif
}

// Full 52-card deck mask
constexpr CardMask FULL_DECK_MASK = (1ULL << 52) - 1;

// ============================================================================
// Board representation
// ============================================================================
struct Board {
    std::array<int, 5> cards{};
    int count = 0; // 0=preflop, 3=flop, 4=turn, 5=river
    CardMask mask = 0;

    void add_card(int card) {
        assert(count < 5);
        cards[count++] = card;
        mask = mask_add_card(mask, card);
    }

    bool contains(int card) const {
        return mask_has_card(mask, card);
    }
};

// ============================================================================
// Suit isomorphism - canonical mapping to reduce information sets
// ============================================================================
struct SuitMapping {
    std::array<int, 4> map;  // original suit -> canonical suit

    int apply(int card) const {
        return make_card(card_rank(card), map[card_suit(card)]);
    }
};

// Get canonical suit mapping for a board + hand combination
SuitMapping get_canonical_suit_mapping(const Board& board, int hand_card0, int hand_card1);

// ============================================================================
// Hand (private cards, 2 cards)
// ============================================================================
struct Hand {
    int cards[2];

    Hand() : cards{-1, -1} {}
    Hand(int c0, int c1) {
        // Always store in sorted order (lower first)
        if (c0 < c1) { cards[0] = c0; cards[1] = c1; }
        else          { cards[0] = c1; cards[1] = c0; }
    }

    CardMask mask() const {
        return card_mask(cards[0]) | card_mask(cards[1]);
    }

    bool conflicts_with(CardMask board_mask) const {
        return (mask() & board_mask) != 0;
    }

    // Unique index for hand combo (0 to C(52,2)-1)
    int combo_index() const {
        return cards[0] * (2 * NUM_CARDS - cards[0] - 3) / 2 + cards[1] - 1;
    }

    std::string to_string() const {
        return card_to_string(cards[0]) + card_to_string(cards[1]);
    }
};

// Total number of possible 2-card combos
constexpr int NUM_COMBOS = NUM_CARDS * (NUM_CARDS - 1) / 2;

// Generate all possible hands
std::vector<Hand> generate_all_hands();

// Generate all hands that don't conflict with a given card mask
std::vector<Hand> generate_hands(CardMask dead_cards);

} // namespace poker
