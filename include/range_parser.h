#pragma once
#include "cards.h"
#include <string>
#include <vector>

namespace poker {

// ============================================================================
// Range Parser
// Parses standard poker range notation into a list of hands
//
// Supported formats:
//   "AA"       - pocket aces (all 6 combos)
//   "AKs"      - ace-king suited (4 combos)
//   "AKo"      - ace-king offsuit (12 combos)
//   "AK"       - all ace-king combos (16 combos)
//   "TT+"      - tens and above (TT, JJ, QQ, KK, AA)
//   "ATs+"     - AT suited and above (ATs, AJs, AQs, AKs)
//   "AhKh"     - specific hand combo
//   "22-55"    - pair range
//   "AA,KK,QQ" - comma separated
//   "random"   - all possible hands
// ============================================================================

class RangeParser {
public:
    // Parse a range string into a list of hands
    // dead_cards: mask of cards that cannot be in the range (e.g. board cards)
    static std::vector<Hand> parse(const std::string& range_str, 
                                   CardMask dead_cards = 0);

    // Generate a full range (all possible hands not conflicting with dead cards)
    static std::vector<Hand> full_range(CardMask dead_cards = 0);

    // Parse a single hand notation to all matching combos
    static std::vector<Hand> parse_single(const std::string& token, 
                                          CardMask dead_cards = 0);

private:
    static int parse_rank_char(char c);
    static int parse_suit_char(char c);
    
    static std::vector<Hand> expand_pair(int rank, CardMask dead);
    static std::vector<Hand> expand_suited(int r1, int r2, CardMask dead);
    static std::vector<Hand> expand_offsuit(int r1, int r2, CardMask dead);
    static std::vector<Hand> expand_all(int r1, int r2, CardMask dead);
    static std::vector<Hand> expand_specific(int c1, int c2, CardMask dead);
};

// Utility: convert range to string representation
std::string range_to_string(const std::vector<Hand>& range);

} // namespace poker
