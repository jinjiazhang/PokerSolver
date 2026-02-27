#pragma once
#include "cards.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace poker {

// ============================================================================
// Game Tree for No-Limit Texas Hold'em
// Supports custom bet sizing on each street
// ============================================================================

enum class NodeType : uint8_t {
    PLAYER,    // Decision node for a player
    CHANCE,    // Chance node (dealing community cards)
    TERMINAL,  // Showdown or fold
    FOLD       // Player folded
};

enum class Street : uint8_t {
    PREFLOP = 0,
    FLOP = 1,
    TURN = 2,
    RIVER = 3
};

enum class ActionType : uint8_t {
    FOLD = 0,
    CHECK = 1,
    CALL = 2,
    BET = 3,    // open bet
    RAISE = 4,
    ALLIN = 5
};

struct Action {
    ActionType type;
    double amount;  // chip amount (0 for fold/check/call when pot-relative)

    std::string to_string() const;
};

// ============================================================================
// Game tree node - compact representation for memory efficiency
// ============================================================================
struct GameTreeNode {
    NodeType type;
    uint8_t  player;       // 0 = OOP (out-of-position), 1 = IP (in-position)
    Street   street;
    
    // Pot and stack info
    double pot;            // current pot size
    double stack;          // effective remaining stack
    double player_bets[2]; // how much each player has committed this street

    // Actions available at this node
    std::vector<Action> actions;
    
    // Children nodes (one per action, or one per deal card for chance nodes)
    std::vector<std::unique_ptr<GameTreeNode>> children;

    // For terminal nodes
    double payoff_multiplier; // +1 or -1 based on who wins the pot

    // Node index for strategy storage
    int node_index = -1;

    GameTreeNode() : type(NodeType::TERMINAL), player(0), street(Street::FLOP),
                     pot(0), stack(0), player_bets{0, 0}, payoff_multiplier(0) {}

    bool is_terminal() const { return type == NodeType::TERMINAL || type == NodeType::FOLD; }
    bool is_chance() const { return type == NodeType::CHANCE; }
    bool is_player() const { return type == NodeType::PLAYER; }
    int num_actions() const { return static_cast<int>(actions.size()); }
};

// ============================================================================
// Bet sizing configuration
// ============================================================================
struct BetConfig {
    std::vector<double> bet_sizes;   // as fraction of pot (e.g., 0.33, 0.5, 0.75, 1.0)
    std::vector<double> raise_sizes; // as fraction of pot
    bool add_allin = true;           // always include all-in option

    // Default aggressive config
    static BetConfig default_config() {
        BetConfig cfg;
        cfg.bet_sizes  = {0.33, 0.67, 1.0};
        cfg.raise_sizes = {0.5, 1.0};
        cfg.add_allin = true;
        return cfg;
    }
};

// ============================================================================
// Game parameters
// ============================================================================
struct GameParams {
    double initial_pot = 10.0;   // pot at start of solving
    double effective_stack = 100.0; // effective stack size
    Board  board;                // community cards

    // Bet sizing for each street (flop, turn, river)
    BetConfig flop_bet_config;
    BetConfig turn_bet_config;
    BetConfig river_bet_config;

    // OOP acts first, IP acts second
    // IP has position advantage

    // Max number of raises per street
    int max_raises_per_street = 4;

    GameParams() {
        flop_bet_config  = BetConfig::default_config();
        turn_bet_config  = BetConfig::default_config();
        river_bet_config = BetConfig::default_config();
    }

    const BetConfig& get_bet_config(Street street) const {
        switch (street) {
            case Street::FLOP:  return flop_bet_config;
            case Street::TURN:  return turn_bet_config;
            case Street::RIVER: return river_bet_config;
            default: return flop_bet_config;
        }
    }
};

// ============================================================================
// Game Tree Builder
// ============================================================================
class GameTreeBuilder {
public:
    explicit GameTreeBuilder(const GameParams& params);

    // Build the complete game tree, returns root node
    std::unique_ptr<GameTreeNode> build();

    // Get total number of decision nodes
    int num_player_nodes() const { return player_node_count_; }
    int num_total_nodes() const { return total_node_count_; }

private:
    GameParams params_;
    int player_node_count_ = 0;
    int total_node_count_ = 0;

    // Recursive build
    std::unique_ptr<GameTreeNode> build_action_node(
        int acting_player,
        Street street,
        double pot,
        double stack,
        double bets[2],
        int num_raises,
        bool can_check);

    std::unique_ptr<GameTreeNode> build_chance_node(
        Street next_street,
        double pot,
        double stack);

    std::unique_ptr<GameTreeNode> make_terminal(
        double pot, bool is_fold, int folder);

    std::vector<Action> generate_actions(
        int acting_player,
        Street street,
        double pot,
        double stack,
        const double bets[2],
        int num_raises,
        bool can_check);
};

} // namespace poker
