#include "cards.h"
#include <algorithm>
#include <unordered_map>
#include <map>

namespace poker {

int string_to_card(const std::string& s) {
    static const std::unordered_map<char, int> rank_map = {
        {'2', 0}, {'3', 1}, {'4', 2}, {'5', 3}, {'6', 4},
        {'7', 5}, {'8', 6}, {'9', 7}, {'T', 8}, {'t', 8},
        {'J', 9}, {'j', 9}, {'Q', 10}, {'q', 10},
        {'K', 11}, {'k', 11}, {'A', 12}, {'a', 12}
    };
    static const std::unordered_map<char, int> suit_map = {
        {'c', 0}, {'C', 0}, {'d', 1}, {'D', 1},
        {'h', 2}, {'H', 2}, {'s', 3}, {'S', 3}
    };

    if (s.size() < 2) return -1;
    auto rit = rank_map.find(s[0]);
    auto sit = suit_map.find(s[1]);
    if (rit == rank_map.end() || sit == suit_map.end()) return -1;
    return make_card(rit->second, sit->second);
}

SuitMapping get_canonical_suit_mapping(const Board& board, int hc0, int hc1) {
    // The idea: relabel suits so that the first suit encountered (in board, then hand)
    // gets the lowest canonical suit number. This ensures isomorphic hands map identically.
    SuitMapping mapping;
    mapping.map.fill(-1);
    int next_suit = 0;

    auto assign = [&](int card) {
        int s = card_suit(card);
        if (mapping.map[s] == -1) {
            mapping.map[s] = next_suit++;
        }
    };

    // Process board cards first (they define suit equivalences)
    for (int i = 0; i < board.count; ++i) {
        assign(board.cards[i]);
    }
    // Then hand cards
    assign(hc0);
    assign(hc1);
    // Fill remaining suits
    for (int s = 0; s < 4; ++s) {
        if (mapping.map[s] == -1) {
            mapping.map[s] = next_suit++;
        }
    }

    return mapping;
}

// ============================================================================
// Suit isomorphism implementation
//
// For a given board, two hands are suit-isomorphic if there exists a
// permutation of suits that:
//   1. Keeps the board invariant (each board card maps to itself)
//   2. Maps one hand to the other
//
// We compute a canonical form for each hand by building a "suit pattern":
//   - Compute a suit signature based on how each hand suit relates to the
//     board's suit distribution
//   - Create a canonical suit relabeling that preserves the board
//   - Map hand cards through this relabeling
//
// The canonical hand is the lexicographically smallest hand achievable
// by any board-preserving suit permutation.
// ============================================================================

// Compute the set of suit permutations that leave the board invariant
static std::vector<std::array<int, 4>> get_board_preserving_permutations(const Board& board) {
    // Count how many board cards have each suit
    std::array<int, 4> suit_counts = {0, 0, 0, 0};
    for (int i = 0; i < board.count; ++i) {
        suit_counts[card_suit(board.cards[i])]++;
    }

    // Group suits by their count (suits with same count are interchangeable)
    // But we must also consider which ranks appear on each suit
    // Two suits are interchangeable iff they have exactly the same set of
    // board-card ranks
    
    // Build rank-set signature for each suit
    // signature[suit] = sorted list of ranks that appear on the board with that suit
    std::array<std::vector<int>, 4> suit_rank_sets;
    for (int i = 0; i < board.count; ++i) {
        int s = card_suit(board.cards[i]);
        int r = card_rank(board.cards[i]);
        suit_rank_sets[s].push_back(r);
    }
    for (int s = 0; s < 4; ++s) {
        std::sort(suit_rank_sets[s].begin(), suit_rank_sets[s].end());
    }

    // Group suits with identical rank-set signatures
    // Suits within the same group can be permuted freely
    std::vector<std::vector<int>> suit_groups;
    std::array<bool, 4> assigned = {false, false, false, false};
    
    for (int s = 0; s < 4; ++s) {
        if (assigned[s]) continue;
        std::vector<int> group;
        group.push_back(s);
        assigned[s] = true;
        for (int s2 = s + 1; s2 < 4; ++s2) {
            if (!assigned[s2] && suit_rank_sets[s2] == suit_rank_sets[s]) {
                group.push_back(s2);
                assigned[s2] = true;
            }
        }
        suit_groups.push_back(group);
    }

    // Generate all permutations by computing cartesian product of
    // permutations within each group
    // Start with identity mapping
    std::vector<std::array<int, 4>> result;
    
    // Helper: generate all permutations of a group
    // For each group, we try all permutations of the suits in that group
    // and combine them into full suit mappings
    
    // We'll build this recursively over groups
    std::vector<std::array<int, 4>> current = {{0, 1, 2, 3}}; // start with identity
    
    for (const auto& group : suit_groups) {
        if (group.size() <= 1) continue; // no permutations possible
        
        std::vector<std::array<int, 4>> next;
        
        // Generate all permutations of this group
        std::vector<int> perm = group;
        std::sort(perm.begin(), perm.end());
        
        do {
            for (const auto& base : current) {
                std::array<int, 4> mapping = base;
                // Apply this group permutation to the base mapping
                for (size_t i = 0; i < group.size(); ++i) {
                    mapping[group[i]] = base[perm[i]];
                }
                next.push_back(mapping);
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
        
        current = next;
    }
    
    return current;
}

Hand get_canonical_hand(const Board& board, const Hand& hand) {
    if (hand.conflicts_with(board.mask)) {
        return hand; // conflicting hands won't be used, return as-is
    }
    
    auto perms = get_board_preserving_permutations(board);
    
    Hand best = hand;
    bool first = true;
    
    for (const auto& perm : perms) {
        // Apply permutation to hand cards
        int c0 = make_card(card_rank(hand.cards[0]), perm[card_suit(hand.cards[0])]);
        int c1 = make_card(card_rank(hand.cards[1]), perm[card_suit(hand.cards[1])]);
        Hand mapped(c0, c1);
        
        // Verify the mapped hand doesn't conflict with the board
        // (it shouldn't if the permutation preserves the board)
        if (mapped.conflicts_with(board.mask)) continue;
        
        if (first || mapped < best) {
            best = mapped;
            first = false;
        }
    }
    
    return best;
}

IsomorphismMap build_isomorphism_map(const Board& board, const std::vector<Hand>& range) {
    IsomorphismMap result;
    result.hand_to_canonical.resize(range.size());
    
    // Compute canonical hand for each hand in range
    // Group hands by their canonical form
    std::map<std::pair<int, int>, int> canonical_to_group; // canonical (c0,c1) -> group index
    
    for (int i = 0; i < static_cast<int>(range.size()); ++i) {
        if (range[i].conflicts_with(board.mask)) {
            // Conflicting hands get their own group (they won't participate in CFR)
            result.hand_to_canonical[i] = -1;
            continue;
        }
        
        Hand canonical = get_canonical_hand(board, range[i]);
        auto key = std::make_pair(canonical.cards[0], canonical.cards[1]);
        
        auto it = canonical_to_group.find(key);
        if (it == canonical_to_group.end()) {
            int group_idx = static_cast<int>(canonical_to_group.size());
            canonical_to_group[key] = group_idx;
            result.hand_to_canonical[i] = group_idx;
        } else {
            result.hand_to_canonical[i] = it->second;
        }
    }
    
    result.num_canonical = static_cast<int>(canonical_to_group.size());
    
    // Build reverse mapping
    result.canonical_to_hands.resize(result.num_canonical);
    result.canonical_representative.resize(result.num_canonical, -1);
    
    for (int i = 0; i < static_cast<int>(range.size()); ++i) {
        int grp = result.hand_to_canonical[i];
        if (grp < 0) continue;
        result.canonical_to_hands[grp].push_back(i);
        if (result.canonical_representative[grp] < 0) {
            result.canonical_representative[grp] = i;
        }
    }
    
    return result;
}

std::vector<Hand> generate_all_hands() {
    std::vector<Hand> hands;
    hands.reserve(NUM_COMBOS);
    for (int c0 = 0; c0 < NUM_CARDS; ++c0) {
        for (int c1 = c0 + 1; c1 < NUM_CARDS; ++c1) {
            hands.emplace_back(c0, c1);
        }
    }
    return hands;
}

std::vector<Hand> generate_hands(CardMask dead_cards) {
    std::vector<Hand> hands;
    for (int c0 = 0; c0 < NUM_CARDS; ++c0) {
        if (mask_has_card(dead_cards, c0)) continue;
        for (int c1 = c0 + 1; c1 < NUM_CARDS; ++c1) {
            if (mask_has_card(dead_cards, c1)) continue;
            hands.emplace_back(c0, c1);
        }
    }
    return hands;
}

} // namespace poker
