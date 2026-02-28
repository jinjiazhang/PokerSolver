#include "hand_eval.h"
#include <algorithm>
#include <numeric>
#include <cstring>
#include <vector>

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
// Using a 209MB flat array for O(1) lookups of ANY 5 cards (matching rank without suit)
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
// Size required: 41^4 * 37 = 104,554,897 elements * 2 bytes = ~209 MB
static std::vector<uint16_t> g_eval_table;

static void init_fast_eval() {
    if (!g_eval_table.empty()) return;
    
    // Allocate 209MB table representing all possible 5-rank combinations
    g_eval_table.resize(104554898, 0);

    for (int c1 = 0; c1 < 13; ++c1) {
        for (int c2 = c1; c2 < 13; ++c2) {
            for (int c3 = c2; c3 < 13; ++c3) {
                for (int c4 = c3; c4 < 13; ++c4) {
                    for (int c5 = c4; c5 < 13; ++c5) {
                        int counts[13] = {0};
                        counts[c1]++; counts[c2]++; counts[c3]++; counts[c4]++; counts[c5]++;
                        
                        bool valid = true;
                        for (int i = 0; i < 13; ++i) if (counts[i] > 4) valid = false;
                        if (!valid) continue;

                        uint32_t pp = (uint32_t)PRIMES[c1] * PRIMES[c2] * PRIMES[c3] * PRIMES[c4] * PRIMES[c5];

                        int unique = 0;
                        for (int i = 0; i < 13; ++i) if (counts[i] > 0) unique++;

                        uint16_t r = 0;
                        if (unique == 5) {
                            uint32_t mask = (1 << c1) | (1 << c2) | (1 << c3) | (1 << c4) | (1 << c5);
                            int str = find_straight(mask);
                            if (str >= 0) {
                                r = make_rank(HAND_STRAIGHT, str);
                            } else {
                                r = make_rank(HAND_HIGH_CARD, (c5 << 8) | (c4 << 6) | (c3 << 4) | (c2 << 2) | c1);
                            }
                        } else if (unique == 4) {
                            int p = 0, k1 = -1, k2 = -1, k3 = -1;
                            for (int i = 12; i >= 0; --i) {
                                if (counts[i] == 2) p = i;
                                else if (counts[i] == 1) {
                                    if (k1 == -1) k1 = i;
                                    else if (k2 == -1) k2 = i;
                                    else k3 = i;
                                }
                            }
                            r = make_rank(HAND_ONE_PAIR, (p << 8) | (k1 << 6) | (k2 << 4) | (k3 << 2));
                        } else if (unique == 3) {
                            bool is_trips = false;
                            for (int i = 0; i < 13; ++i) if (counts[i] == 3) is_trips = true;
                            
                            if (is_trips) {
                                int t = 0, k1 = -1, k2 = -1;
                                for (int i = 12; i >= 0; --i) {
                                    if (counts[i] == 3) t = i;
                                    else if (counts[i] == 1) {
                                        if (k1 == -1) k1 = i;
                                        else k2 = i;
                                    }
                                }
                                r = make_rank(HAND_THREE_OF_A_KIND, (t << 8) | (k1 << 4) | k2);
                            } else {
                                int p1 = -1, p2 = -1, k = -1;
                                for (int i = 12; i >= 0; --i) {
                                    if (counts[i] == 2) {
                                        if (p1 == -1) p1 = i;
                                        else p2 = i;
                                    } else if (counts[i] == 1) {
                                        k = i;
                                    }
                                }
                                r = make_rank(HAND_TWO_PAIR, (p1 << 8) | (p2 << 4) | k);
                            }
                        } else if (unique == 2) {
                            bool is_quads = false;
                            for (int i = 0; i < 13; ++i) if (counts[i] == 4) is_quads = true;

                            if (is_quads) {
                                int q = 0, k = 0;
                                for (int i = 12; i >= 0; --i) {
                                    if (counts[i] == 4) q = i;
                                    if (counts[i] == 1) k = i;
                                }
                                r = make_rank(HAND_FOUR_OF_A_KIND, (q << 4) | k);
                            } else {
                                int t = 0, p = 0;
                                for (int i = 12; i >= 0; --i) {
                                    if (counts[i] == 3) t = i;
                                    if (counts[i] == 2) p = i;
                                }
                                r = make_rank(HAND_FULL_HOUSE, (t << 4) | p);
                            }
                        }
                        g_eval_table[pp] = r;
                    }
                }
            }
        }
    }
}

HandEvaluator::HandEvaluator() {
    init_tables();
}

void HandEvaluator::init_tables() {
    flush_table_.fill(0);
    init_fast_eval();

    // Precalculate all flush combinations ranging from 5 to 7 cards of the same suit
    for (uint32_t mask = 0; mask < 8192; ++mask) {
        if (popcount32(mask) >= 5) {
            uint16_t best = 0;
            // Iterate all 5-bit submasks
            for (uint32_t submask = mask; submask > 0; submask = (submask - 1) & mask) {
                if (popcount32(submask) == 5) {
                    int straight_rank = find_straight(submask);
                    uint16_t r;
                    if (straight_rank >= 0) {
                        r = make_rank(HAND_STRAIGHT_FLUSH, straight_rank);
                    } else {
                        int bits[5], c = 0;
                        for (int i = 0; i < 13; ++i) {
                            if (submask & (1 << i)) bits[c++] = i;
                        }
                        int sub = (bits[4] << 8) | (bits[3] << 6) | (bits[2] << 4) | (bits[1] << 2) | bits[0];
                        r = make_rank(HAND_FLUSH, sub);
                    }
                    if (r > best) best = r;
                }
            }
            flush_table_[mask] = best;
        }
    }
}

uint16_t HandEvaluator::eval_5cards(int c0, int c1, int c2, int c3, int c4) const {
    int s0 = card_suit(c0), s1 = card_suit(c1), s2 = card_suit(c2);
    int s3 = card_suit(c3), s4 = card_suit(c4);

    if (s0 == s1 && s1 == s2 && s2 == s3 && s3 == s4) {
        uint32_t mask = rank_bit(card_rank(c0)) | rank_bit(card_rank(c1)) | 
                        rank_bit(card_rank(c2)) | rank_bit(card_rank(c3)) | rank_bit(card_rank(c4));
        return flush_table_[mask];
    }

    uint32_t pp = (uint32_t)PRIMES[card_rank(c0)] * PRIMES[card_rank(c1)] * 
                  PRIMES[card_rank(c2)] * PRIMES[card_rank(c3)] * PRIMES[card_rank(c4)];
    return g_eval_table[pp];
}

uint16_t HandEvaluator::evaluate5(int c0, int c1, int c2, int c3, int c4) const {
    return eval_5cards(c0, c1, c2, c3, c4);
}

uint16_t HandEvaluator::evaluate(CardMask board_mask, int board_count, const Hand& hand) const {
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
    
    // Fast path: find flush
    int suit_counts[4] = {0, 0, 0, 0};
    int suit_masks[4] = {0, 0, 0, 0};
    uint32_t primes[7];
    
    for (int i = 0; i < total; ++i) {
        int r = card_rank(all_cards[i]);
        int s = card_suit(all_cards[i]);
        suit_counts[s]++;
        suit_masks[s] |= (1 << r);
        primes[i] = PRIMES[r];
    }
    
    // Use multi-card flush check. If >= 5 same suit, use precomputed value for all those rank bits.
    for (int s = 0; s < 4; ++s) {
        if (suit_counts[s] >= 5) {
            return flush_table_[suit_masks[s]];
        }
    }

    // No flush, simply evaluate non-suited combinations.
    if (total == 5) {
        uint32_t pp = primes[0] * primes[1] * primes[2] * primes[3] * primes[4];
        return g_eval_table[pp];
    } else if (total == 6) {
        uint16_t best = 0;
        for (int i = 0; i < 6; ++i) {
            uint32_t pp = 1;
            for (int k = 0; k < 6; ++k) if (k != i) pp *= primes[k];
            if (g_eval_table[pp] > best) best = g_eval_table[pp];
        }
        return best;
    } else {
        uint16_t best = 0;
        for (int i = 0; i < 7 - 4; ++i) {
            for (int j = i + 1; j < 7 - 3; ++j) {
                for (int k = j + 1; k < 7 - 2; ++k) {
                    for (int l = k + 1; l < 7 - 1; ++l) {
                        for (int m = l + 1; m < 7; ++m) {
                            uint32_t pp = primes[i] * primes[j] * primes[k] * primes[l] * primes[m];
                            if (g_eval_table[pp] > best) best = g_eval_table[pp];
                        }
                    }
                }
            }
        }
        return best;
    }
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
