#pragma once
#include "cards.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace poker {

// ============================================================================
// Game Tree for No-Limit Texas Hold'em
// Supports custom bet sizing per player per street, including donk bets
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
// Bet sizing configuration (per player per street)
// ============================================================================
struct BetConfig {
    std::vector<double> bet_sizes;   // as fraction of pot (e.g., 0.33, 0.5, 0.75, 1.0)
    std::vector<double> raise_sizes; // as fraction of pot
    std::vector<double> donk_sizes;  // donk bet sizes (OOP betting into previous-street aggressor)
    bool add_allin = true;           // always include all-in option

    // Default aggressive config
    static BetConfig default_config() {
        BetConfig cfg;
        cfg.bet_sizes  = {0.33, 0.67, 1.0};
        cfg.raise_sizes = {0.5, 1.0};
        // donk_sizes empty by default → falls back to bet_sizes
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

    // Bet sizing: separate config per player (0=OOP, 1=IP) per street
    // OOP acts first, IP acts second. IP has position advantage.
    BetConfig flop_config[2];    // [0]=OOP, [1]=IP
    BetConfig turn_config[2];    // [0]=OOP, [1]=IP
    BetConfig river_config[2];   // [0]=OOP, [1]=IP

    // Max number of raises per street
    int max_raises_per_street = 4;

    // All-in threshold: if a bet/raise would leave the player with
    // remaining stack < threshold * pot_after_action, convert to all-in.
    // This prevents tiny-stack branches from bloating the tree.
    // Set to 0.0 to disable. Default 0.67 matches PioSolver convention.
    double allin_threshold = 0.67;

    GameParams() {
        auto def = BetConfig::default_config();
        flop_config[0] = flop_config[1] = def;
        turn_config[0] = turn_config[1] = def;
        river_config[0] = river_config[1] = def;
    }

    // Get bet config for a specific street and player
    const BetConfig& get_bet_config(Street street, int player) const {
        switch (street) {
            case Street::FLOP:  return flop_config[player];
            case Street::TURN:  return turn_config[player];
            case Street::RIVER: return river_config[player];
            default: return flop_config[player];
        }
    }

    // Mutable access for CLI configuration
    BetConfig& get_bet_config_mut(Street street, int player) {
        switch (street) {
            case Street::FLOP:  return flop_config[player];
            case Street::TURN:  return turn_config[player];
            case Street::RIVER: return river_config[player];
            default: return flop_config[player];
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
        bool can_check,
        bool is_donk = false);

    std::unique_ptr<GameTreeNode> build_chance_node(
        Street next_street,
        double pot,
        double stack,
        bool is_donk = false);

    std::unique_ptr<GameTreeNode> make_terminal(
        double pot, bool is_fold, int folder);

    std::vector<Action> generate_actions(
        int acting_player,
        Street street,
        double pot,
        double stack,
        const double bets[2],
        int num_raises,
        bool can_check,
        bool is_donk = false);
};

} // namespace poker
