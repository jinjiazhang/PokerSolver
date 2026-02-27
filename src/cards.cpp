#include "cards.h"
#include <algorithm>
#include <unordered_map>

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
