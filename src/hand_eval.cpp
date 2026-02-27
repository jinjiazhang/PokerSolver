#include "hand_eval.h"
#include <algorithm>
#include <numeric>
#include <cstring>
#include <unordered_map>

#ifdef _MSC_VER
#include <intrin.h>
static inline int popcount32(uint32_t x) { return static_cast<int>(__popcnt(x)); }
#else
static inline int popcount32(uint32_t x) { return __builtin_popcount(x); }
#endif

namespace poker {

// ============================================================================
// Precomputed prime-hash based evaluator
// Each rank is assigned a prime number. The product of primes uniquely 
// identifies any set of ranks (with multiplicity), allowing O(1) lookup.
// ============================================================================

static constexpr int PRIMES[13] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};

// Straight masks (13-bit patterns, A-high to 5-high)
// bit i set = rank i present (0=2, 12=A)
static constexpr uint32_t STRAIGHT_MASKS[10] = {
    0x1F00, // A K Q J T  -> bits 12,11,10,9,8
    0x0F80, // K Q J T 9  -> bits 11,10,9,8,7
    0x07C0, // Q J T 9 8
    0x03E0, // J T 9 8 7
    0x01F0, // T 9 8 7 6
    0x00F8, // 9 8 7 6 5
    0x007C, // 8 7 6 5 4
    0x003E, // 7 6 5 4 3
    0x001F, // 6 5 4 3 2
    0x100F, // 5 4 3 2 A (wheel) -> bits 12,3,2,1,0
};

static int find_straight(uint32_t rank_mask) {
    for (int i = 0; i < 10; ++i) {
        if ((rank_mask & STRAIGHT_MASKS[i]) == STRAIGHT_MASKS[i]) {
            return 9 - i; // 9 = A-high, 0 = wheel
        }
    }
    return -1;
}

// Build hand rank value: category in top 4 bits, sub-rank in lower 12 bits
static uint16_t make_rank(int category, int sub_rank) {
    return static_cast<uint16_t>((category << 12) | (sub_rank & 0xFFF));
}

// Global lookup table: prime product -> hand rank value
// Using unordered_map for correctness (no hash collisions)
static std::unordered_map<uint32_t, uint16_t> g_pairs_lookup;
static bool g_pairs_initialized = false;

static void init_pairs_lookup() {
    if (g_pairs_initialized) return;

    // Four of a kind: rank^4 * kicker
    for (int q = 0; q < 13; ++q) {
        for (int k = 0; k < 13; ++k) {
            if (k == q) continue;
            uint32_t pp = (uint32_t)PRIMES[q] * PRIMES[q] * PRIMES[q] * PRIMES[q] * PRIMES[k];
            uint16_t rv = make_rank(HAND_FOUR_OF_A_KIND, (q << 4) | k);
            g_pairs_lookup[pp] = rv;
        }
    }

    // Full house: rank^3 * pair^2
    for (int t = 0; t < 13; ++t) {
        for (int p = 0; p < 13; ++p) {
            if (p == t) continue;
            uint32_t pp = (uint32_t)PRIMES[t] * PRIMES[t] * PRIMES[t] * PRIMES[p] * PRIMES[p];
            uint16_t rv = make_rank(HAND_FULL_HOUSE, (t << 4) | p);
            g_pairs_lookup[pp] = rv;
        }
    }

    // Three of a kind: rank^3 * k1 * k2
    for (int t = 0; t < 13; ++t) {
        for (int k1 = 0; k1 < 13; ++k1) {
            if (k1 == t) continue;
            for (int k2 = k1 + 1; k2 < 13; ++k2) {
                if (k2 == t) continue;
                uint32_t pp = (uint32_t)PRIMES[t] * PRIMES[t] * PRIMES[t] * PRIMES[k1] * PRIMES[k2];
                uint16_t rv = make_rank(HAND_THREE_OF_A_KIND, (t << 8) | (k2 << 4) | k1);
                g_pairs_lookup[pp] = rv;
            }
        }
    }

    // Two pair: p1^2 * p2^2 * kicker
    for (int p1 = 0; p1 < 13; ++p1) {
        for (int p2 = p1 + 1; p2 < 13; ++p2) {
            for (int k = 0; k < 13; ++k) {
                if (k == p1 || k == p2) continue;
                uint32_t pp = (uint32_t)PRIMES[p1] * PRIMES[p1] * PRIMES[p2] * PRIMES[p2] * PRIMES[k];
                uint16_t rv = make_rank(HAND_TWO_PAIR, (p2 << 8) | (p1 << 4) | k);
                g_pairs_lookup[pp] = rv;
            }
        }
    }

    // One pair: p^2 * k1 * k2 * k3
    for (int p = 0; p < 13; ++p) {
        for (int k1 = 0; k1 < 13; ++k1) {
            if (k1 == p) continue;
            for (int k2 = k1 + 1; k2 < 13; ++k2) {
                if (k2 == p) continue;
                for (int k3 = k2 + 1; k3 < 13; ++k3) {
                    if (k3 == p) continue;
                    uint32_t pp = (uint32_t)PRIMES[p] * PRIMES[p] * PRIMES[k1] * PRIMES[k2] * PRIMES[k3];
                    uint16_t rv = make_rank(HAND_ONE_PAIR, (p << 8) | (k3 << 6) | (k2 << 4) | (k1 << 2));
                    g_pairs_lookup[pp] = rv;
                }
            }
        }
    }

    g_pairs_initialized = true;
}

HandEvaluator::HandEvaluator() {
    init_tables();
}

void HandEvaluator::init_tables() {
    flush_table_.fill(0);
    unique5_table_.fill(0);
    pairs_table_.fill(0);

    // Initialize the global pairs lookup
    init_pairs_lookup();

    // Pre-compute all C(13,5) = 1287 possible 5-card rank combinations
    // for flushes (all same suit) and unique ranks (no pairs)
    for (int r0 = 0; r0 < 13; ++r0) {
        for (int r1 = r0 + 1; r1 < 13; ++r1) {
            for (int r2 = r1 + 1; r2 < 13; ++r2) {
                for (int r3 = r2 + 1; r3 < 13; ++r3) {
                    for (int r4 = r3 + 1; r4 < 13; ++r4) {
                        uint32_t mask = rank_bit(r0) | rank_bit(r1) | rank_bit(r2) | 
                                        rank_bit(r3) | rank_bit(r4);

                        int straight_rank = find_straight(mask);

                        // Flush table entry (5 cards all same suit)
                        if (straight_rank >= 0) {
                            flush_table_[mask & 0x1FFF] = make_rank(HAND_STRAIGHT_FLUSH, straight_rank);
                        } else {
                            // Flush: rank by sorted cards
                            int sub = (r4 << 8) | (r3 << 6) | (r2 << 4) | (r1 << 2) | r0;
                            flush_table_[mask & 0x1FFF] = make_rank(HAND_FLUSH, sub & 0xFFF);
                        }

                        // Non-flush unique5 table (no pairs, not flush)
                        if (straight_rank >= 0) {
                            unique5_table_[mask & 0x1FFF] = make_rank(HAND_STRAIGHT, straight_rank);
                        } else {
                            int sub = (r4 << 8) | (r3 << 6) | (r2 << 4) | (r1 << 2) | r0;
                            unique5_table_[mask & 0x1FFF] = make_rank(HAND_HIGH_CARD, sub & 0xFFF);
                        }
                    }
                }
            }
        }
    }
}

uint16_t HandEvaluator::eval_5cards(int c0, int c1, int c2, int c3, int c4) const {
    int r0 = card_rank(c0), r1 = card_rank(c1), r2 = card_rank(c2);
    int r3 = card_rank(c3), r4 = card_rank(c4);
    int s0 = card_suit(c0), s1 = card_suit(c1), s2 = card_suit(c2);
    int s3 = card_suit(c3), s4 = card_suit(c4);

    // Check for flush
    bool is_flush = (s0 == s1 && s1 == s2 && s2 == s3 && s3 == s4);

    uint32_t rank_mask = rank_bit(r0) | rank_bit(r1) | rank_bit(r2) | rank_bit(r3) | rank_bit(r4);
    int unique_count = popcount32(rank_mask);

    if (is_flush) {
        return flush_table_[rank_mask & 0x1FFF];
    }

    if (unique_count == 5) {
        // No pairs - straight or high card
        return unique5_table_[rank_mask & 0x1FFF];
    }

    // Has pairs - use prime product hash lookup
    uint32_t pp = (uint32_t)PRIMES[r0] * PRIMES[r1] * PRIMES[r2] * PRIMES[r3] * PRIMES[r4];
    auto it = g_pairs_lookup.find(pp);
    if (it != g_pairs_lookup.end()) {
        return it->second;
    }

    // Should never reach here if tables are correctly built
    return 0;
}

uint16_t HandEvaluator::evaluate5(int c0, int c1, int c2, int c3, int c4) const {
    return eval_5cards(c0, c1, c2, c3, c4);
}

uint16_t HandEvaluator::evaluate(CardMask board_mask, int board_count, const Hand& hand) const {
    // Collect all available cards
    int all_cards[7];
    int n = 0;
    all_cards[n++] = hand.cards[0];
    all_cards[n++] = hand.cards[1];
    for (int c = 0; c < NUM_CARDS && n < 7; ++c) {
        if (mask_has_card(board_mask, c)) {
            all_cards[n++] = c;
        }
    }

    int total = n;
    uint16_t best = 0;

    if (total == 5) {
        return eval_5cards(all_cards[0], all_cards[1], all_cards[2], all_cards[3], all_cards[4]);
    }

    // Enumerate all C(total, 5) combinations
    for (int i = 0; i < total - 4; ++i) {
        for (int j = i + 1; j < total - 3; ++j) {
            for (int k = j + 1; k < total - 2; ++k) {
                for (int l = k + 1; l < total - 1; ++l) {
                    for (int m = l + 1; m < total; ++m) {
                        uint16_t r = eval_5cards(all_cards[i], all_cards[j], all_cards[k],
                                                  all_cards[l], all_cards[m]);
                        if (r > best) best = r;
                    }
                }
            }
        }
    }
    return best;
}

const char* HandEvaluator::category_name(int cat) {
    static const char* names[] = {
        "High Card", "One Pair", "Two Pair", "Three of a Kind",
        "Straight", "Flush", "Full House", "Four of a Kind", "Straight Flush"
    };
    if (cat >= 0 && cat <= 8) return names[cat];
    return "Unknown";
}

const HandEvaluator& get_evaluator() {
    static HandEvaluator evaluator;
    return evaluator;
}

} // namespace poker
