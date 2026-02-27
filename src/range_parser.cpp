#include "range_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace poker {

int RangeParser::parse_rank_char(char c) {
    switch (std::toupper(c)) {
        case '2': return RANK_2;
        case '3': return RANK_3;
        case '4': return RANK_4;
        case '5': return RANK_5;
        case '6': return RANK_6;
        case '7': return RANK_7;
        case '8': return RANK_8;
        case '9': return RANK_9;
        case 'T': return RANK_T;
        case 'J': return RANK_J;
        case 'Q': return RANK_Q;
        case 'K': return RANK_K;
        case 'A': return RANK_A;
        default:  return -1;
    }
}

int RangeParser::parse_suit_char(char c) {
    switch (std::tolower(c)) {
        case 'c': return SUIT_CLUB;
        case 'd': return SUIT_DIAMOND;
        case 'h': return SUIT_HEART;
        case 's': return SUIT_SPADE;
        default:  return -1;
    }
}

std::vector<Hand> RangeParser::expand_pair(int rank, CardMask dead) {
    std::vector<Hand> hands;
    for (int s1 = 0; s1 < 4; ++s1) {
        for (int s2 = s1 + 1; s2 < 4; ++s2) {
            int c1 = make_card(rank, s1);
            int c2 = make_card(rank, s2);
            if (!mask_has_card(dead, c1) && !mask_has_card(dead, c2)) {
                hands.emplace_back(c1, c2);
            }
        }
    }
    return hands;
}

std::vector<Hand> RangeParser::expand_suited(int r1, int r2, CardMask dead) {
    std::vector<Hand> hands;
    for (int s = 0; s < 4; ++s) {
        int c1 = make_card(r1, s);
        int c2 = make_card(r2, s);
        if (!mask_has_card(dead, c1) && !mask_has_card(dead, c2)) {
            hands.emplace_back(c1, c2);
        }
    }
    return hands;
}

std::vector<Hand> RangeParser::expand_offsuit(int r1, int r2, CardMask dead) {
    std::vector<Hand> hands;
    for (int s1 = 0; s1 < 4; ++s1) {
        for (int s2 = 0; s2 < 4; ++s2) {
            if (s1 == s2) continue;
            int c1 = make_card(r1, s1);
            int c2 = make_card(r2, s2);
            if (!mask_has_card(dead, c1) && !mask_has_card(dead, c2)) {
                hands.emplace_back(c1, c2);
            }
        }
    }
    return hands;
}

std::vector<Hand> RangeParser::expand_all(int r1, int r2, CardMask dead) {
    if (r1 == r2) return expand_pair(r1, dead);
    auto s = expand_suited(r1, r2, dead);
    auto o = expand_offsuit(r1, r2, dead);
    s.insert(s.end(), o.begin(), o.end());
    return s;
}

std::vector<Hand> RangeParser::expand_specific(int c1, int c2, CardMask dead) {
    std::vector<Hand> hands;
    if (!mask_has_card(dead, c1) && !mask_has_card(dead, c2) && c1 != c2) {
        hands.emplace_back(c1, c2);
    }
    return hands;
}

std::vector<Hand> RangeParser::parse_single(const std::string& token, CardMask dead_cards) {
    std::vector<Hand> result;
    
    if (token.empty()) return result;

    // Check for "random" keyword
    if (token == "random") {
        return full_range(dead_cards);
    }

    // 4-char specific hand: AhKh
    if (token.size() == 4) {
        int r1 = parse_rank_char(token[0]);
        int s1 = parse_suit_char(token[1]);
        int r2 = parse_rank_char(token[2]);
        int s2 = parse_suit_char(token[3]);
        if (r1 >= 0 && s1 >= 0 && r2 >= 0 && s2 >= 0) {
            return expand_specific(make_card(r1, s1), make_card(r2, s2), dead_cards);
        }
    }

    // Check for + suffix (e.g., "TT+", "ATs+")
    bool plus = false;
    std::string tok = token;
    if (!tok.empty() && tok.back() == '+') {
        plus = true;
        tok.pop_back();
    }

    // Check for range dash (e.g., "22-55")
    auto dash_pos = tok.find('-');
    if (dash_pos != std::string::npos && dash_pos > 0) {
        std::string from_str = tok.substr(0, dash_pos);
        std::string to_str = tok.substr(dash_pos + 1);
        
        if (from_str.size() == 2 && to_str.size() == 2) {
            int r1 = parse_rank_char(from_str[0]);
            int r2 = parse_rank_char(from_str[1]);
            int r3 = parse_rank_char(to_str[0]);
            int r4 = parse_rank_char(to_str[1]);
            
            // Pair range: 22-55
            if (r1 >= 0 && r1 == r2 && r3 >= 0 && r3 == r4) {
                int lo = std::min(r1, r3);
                int hi = std::max(r1, r3);
                for (int r = lo; r <= hi; ++r) {
                    auto h = expand_pair(r, dead_cards);
                    result.insert(result.end(), h.begin(), h.end());
                }
                return result;
            }
        }
    }

    if (tok.size() < 2) return result;

    int r1 = parse_rank_char(tok[0]);
    int r2 = parse_rank_char(tok[1]);
    if (r1 < 0 || r2 < 0) return result;

    // Always ensure r1 >= r2 for canonical form
    if (r1 < r2) std::swap(r1, r2);

    bool suited = false, offsuit = false;
    if (tok.size() >= 3) {
        char mod = std::tolower(tok[2]);
        if (mod == 's') suited = true;
        else if (mod == 'o') offsuit = true;
    }

    if (r1 == r2) {
        // Pair
        if (plus) {
            for (int r = r1; r < NUM_RANKS; ++r) {
                auto h = expand_pair(r, dead_cards);
                result.insert(result.end(), h.begin(), h.end());
            }
        } else {
            result = expand_pair(r1, dead_cards);
        }
    } else if (suited) {
        if (plus) {
            // e.g., ATs+ -> ATs, AJs, AQs, AKs
            for (int r = r2; r < r1; ++r) {
                auto h = expand_suited(r1, r, dead_cards);
                result.insert(result.end(), h.begin(), h.end());
            }
        } else {
            result = expand_suited(r1, r2, dead_cards);
        }
    } else if (offsuit) {
        if (plus) {
            for (int r = r2; r < r1; ++r) {
                auto h = expand_offsuit(r1, r, dead_cards);
                result.insert(result.end(), h.begin(), h.end());
            }
        } else {
            result = expand_offsuit(r1, r2, dead_cards);
        }
    } else {
        // No modifier: all combos
        if (plus) {
            for (int r = r2; r < r1; ++r) {
                auto h = expand_all(r1, r, dead_cards);
                result.insert(result.end(), h.begin(), h.end());
            }
        } else {
            result = expand_all(r1, r2, dead_cards);
        }
    }

    return result;
}

std::vector<Hand> RangeParser::parse(const std::string& range_str, CardMask dead_cards) {
    std::vector<Hand> result;
    
    // Split by comma
    std::istringstream iss(range_str);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        if (token.empty()) continue;
        
        auto hands = parse_single(token, dead_cards);
        result.insert(result.end(), hands.begin(), hands.end());
    }

    // Remove duplicates
    std::sort(result.begin(), result.end(), [](const Hand& a, const Hand& b) {
        if (a.cards[0] != b.cards[0]) return a.cards[0] < b.cards[0];
        return a.cards[1] < b.cards[1];
    });
    result.erase(std::unique(result.begin(), result.end(), [](const Hand& a, const Hand& b) {
        return a.cards[0] == b.cards[0] && a.cards[1] == b.cards[1];
    }), result.end());

    return result;
}

std::vector<Hand> RangeParser::full_range(CardMask dead_cards) {
    return generate_hands(dead_cards);
}

std::string range_to_string(const std::vector<Hand>& range) {
    std::ostringstream oss;
    for (size_t i = 0; i < range.size(); ++i) {
        if (i > 0) oss << ",";
        oss << range[i].to_string();
    }
    return oss.str();
}

} // namespace poker
