#pragma once
#include "cards.h"
#include "hand_eval.h"
#include "game_tree.h"
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <functional>
#include <thread>
#include <condition_variable>
#include <queue>

namespace poker {

// ============================================================================
// CFR Engine - Discounted Counterfactual Regret Minimization
//
// Implements DCFR with the following optimizations:
// - Compact float storage for regrets and strategy sums
// - Cache-friendly memory layout (flat arrays for regrets/strategies)
// - Multi-threaded parallelism over chance node deal-outs
// - Thread-local accumulators to avoid lock contention
// - Suit isomorphism to reduce number of information sets
// ============================================================================

// Information set key: uniquely identifies a player's information at a node
// Combines: node_index + canonical hole cards
struct InfoSetKey {
    int node_index;   // game tree node position
    int hand_index;   // canonical hand combo index

    bool operator==(const InfoSetKey& o) const {
        return node_index == o.node_index && hand_index == o.hand_index;
    }
};

// ============================================================================
// Strategy storage - flat array layout for cache efficiency
// For each player node, stores regrets and strategy sums for each hand/action
// With suit isomorphism: hand_idx is the canonical group index, not range index
// ============================================================================
class StrategyStorage {
public:
    StrategyStorage() = default;

    // Initialize storage for a given tree
    void init(int num_player_nodes, int max_hands, 
              const std::vector<int>& actions_per_node);

    // Access regrets: node_index * max_hands * max_actions + hand * max_actions + action
    float& regret(int node_idx, int hand_idx, int action_idx);
    float  regret(int node_idx, int hand_idx, int action_idx) const;

    // Access strategy sum
    float& strategy_sum(int node_idx, int hand_idx, int action_idx);
    float  strategy_sum(int node_idx, int hand_idx, int action_idx) const;

    // Get current strategy via regret matching
    void get_current_strategy(int node_idx, int hand_idx, int num_actions,
                              float* strategy) const;

    // Get average strategy (the converged equilibrium strategy)
    void get_average_strategy(int node_idx, int hand_idx, int num_actions,
                              float* strategy) const;

    // Apply DCFR discounting at end of each iteration
    void discount(int iteration);

    // Get total allocated memory in bytes
    size_t memory_usage() const;

    // Accessors for parallel accumulation
    int num_nodes() const { return num_nodes_; }
    int max_hands() const { return max_hands_; }
    int max_actions() const { return max_actions_; }
    size_t total_entries() const { return regrets_.size(); }

    // Direct access for merging thread-local accumulators
    float* regret_data() { return regrets_.data(); }
    float* strategy_sum_data() { return strategy_sums_.data(); }
    const float* regret_data() const { return regrets_.data(); }
    const float* strategy_sum_data() const { return strategy_sums_.data(); }

    size_t index(int node, int hand, int action) const {
        return static_cast<size_t>(node) * max_hands_ * max_actions_ + 
               static_cast<size_t>(hand) * max_actions_ + action;
    }

private:
    int num_nodes_ = 0;
    int max_hands_ = 0;
    int max_actions_ = 0;
    std::vector<int> actions_per_node_;
    
    // Flat arrays for cache efficiency
    std::vector<float> regrets_;
    std::vector<float> strategy_sums_;
};

// ============================================================================
// Thread-local accumulator for lock-free parallel CFR
// Each thread writes regret/strategy deltas to its own accumulator,
// which are merged into the global StrategyStorage after traversal.
// ============================================================================
class ThreadLocalAccumulator {
public:
    ThreadLocalAccumulator() = default;

    void init(size_t total_entries);
    void clear();

    float& regret_delta(size_t flat_index) { return regret_deltas_[flat_index]; }
    float& strategy_delta(size_t flat_index) { return strategy_deltas_[flat_index]; }

    const float* regret_data() const { return regret_deltas_.data(); }
    const float* strategy_data() const { return strategy_deltas_.data(); }
    size_t size() const { return regret_deltas_.size(); }

private:
    std::vector<float> regret_deltas_;
    std::vector<float> strategy_deltas_;
};

// ============================================================================
// Showdown evaluator - precomputes hand strength for all combos on a board
// ============================================================================
struct ShowdownResult {
    // For each pair of non-conflicting hands, stores comparison result
    std::vector<std::vector<float>> equity;
    int num_hands;
};

// ============================================================================
// CFR Solver - with multi-threaded parallel iteration + suit isomorphism
// ============================================================================
class CFRSolver {
public:
    struct Config {
        int num_iterations = 200;
        int num_threads = 1;
        bool use_dcfr = true;
        bool use_isomorphism = true;   // enable suit isomorphism
        bool print_progress = true;
        int print_interval = 10;
        
        // DCFR parameters
        double dcfr_alpha = 1.5;   // positive regret discounting
        double dcfr_beta  = 0.0;   // negative regret discounting
        double dcfr_gamma = 2.0;   // strategy sum discounting
    };

    CFRSolver(const GameParams& game_params, const Config& config = Config());

    // Set player ranges
    void set_oop_range(const std::vector<Hand>& range); // player 0
    void set_ip_range(const std::vector<Hand>& range);  // player 1

    // Build tree and solve
    void build();
    void solve();

    // Get results
    // strategy[action] for a given node and hand
    void get_strategy(int node_index, const Hand& hand, int player,
                      std::vector<float>& strategy) const;

    // Compute exploitability as % of pot (best response sum)
    double compute_exploitability() const;

    // Export results to JSON
    std::string export_json() const;

    // Access tree
    const GameTreeNode* get_root() const { return root_.get(); }
    const GameParams& get_params() const { return game_params_; }

private:
    GameParams game_params_;
    Config config_;
    
    std::unique_ptr<GameTreeNode> root_;
    GameTreeBuilder builder_;
    StrategyStorage storage_;

    // Player ranges (hands that each player can hold)
    std::vector<Hand> oop_range_;
    std::vector<Hand> ip_range_;
    
    // Hand index mapping: hand combo_index -> range index
    std::vector<int> oop_hand_map_;
    std::vector<int> ip_hand_map_;

    // Suit isomorphism mappings (one per player)
    // Maps range index -> canonical index for strategy storage
    IsomorphismMap oop_iso_;
    IsomorphismMap ip_iso_;
    bool isomorphism_enabled_ = false;

    // Precomputed showdown equities for river evaluation
    struct BoardEquity {
        CardMask board_mask;
        std::vector<uint16_t> oop_ranks;
        std::vector<uint16_t> ip_ranks;
    };

    // Precomputed hand ranks for the current board
    std::vector<uint16_t> oop_hand_ranks_;
    std::vector<uint16_t> ip_hand_ranks_;
    
    // Precomputed matchup results: matchup_cache_[oop_idx * ip_size + ip_idx]
    std::vector<int8_t> matchup_cache_;
    bool has_precomputed_matchups_ = false;
    
    // Thread-local accumulators (one per thread)
    std::vector<ThreadLocalAccumulator> thread_accumulators_;
    
    // Precompute hand ranks and matchups
    void precompute_matchups(CardMask board_mask);
    
    // Build suit isomorphism maps
    void build_isomorphism();
    
    // Get canonical hand index for strategy storage lookup
    // With isomorphism: returns canonical group index
    // Without: returns range index directly
    int get_canonical_index(int player, int hand_range_idx) const {
        if (!isomorphism_enabled_) return hand_range_idx;
        const auto& iso = (player == 0) ? oop_iso_ : ip_iso_;
        int ci = iso.hand_to_canonical[hand_range_idx];
        return (ci >= 0) ? ci : hand_range_idx;
    }
    
    // Get isomorphism map for a player
    const IsomorphismMap& get_iso(int player) const {
        return (player == 0) ? oop_iso_ : ip_iso_;
    }

    // ---- Single-threaded CFR traversal ----
    void cfr_iteration(int iteration);
    
    void cfr_traverse(
        const GameTreeNode* node,
        int traversing_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards,
        int iteration);

    void cfr_traverse_terminal(
        const GameTreeNode* node,
        int traversing_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards);

    void cfr_traverse_chance(
        const GameTreeNode* node,
        int traversing_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards,
        int iteration);

    // ---- Multi-threaded CFR traversal ----
    void cfr_iteration_parallel(int iteration);

    void cfr_traverse_threaded(
        const GameTreeNode* node,
        int traversing_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards,
        int iteration,
        ThreadLocalAccumulator& accum);

    void cfr_traverse_terminal_threaded(
        const GameTreeNode* node,
        int traversing_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards);

    void cfr_traverse_chance_parallel(
        const GameTreeNode* node,
        int traversing_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards,
        int iteration);

    // Merge all thread-local accumulators into global storage
    void merge_accumulators();

    // Best response traversal for exploitability computation
    // Traverses game tree: at br_player's nodes picks max-value action,
    // at opponent's nodes follows their average strategy
    void best_response_traverse(
        const GameTreeNode* node,
        int br_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards) const;

    void best_response_chance(
        const GameTreeNode* node,
        int br_player,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards) const;

    // Compute showdown payoffs
    void compute_showdown_payoffs(
        int traversing_player,
        double pot,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards) const;

    // Compute fold payoffs
    void compute_fold_payoffs(
        int traversing_player,
        int folder,
        double pot,
        const std::vector<float>& oop_reach,
        const std::vector<float>& ip_reach,
        std::vector<float>& hand_values,
        CardMask dead_cards) const;

    // Build hand index maps
    void build_hand_maps();
    
    // Get the range and map for a player
    const std::vector<Hand>& get_range(int player) const {
        return player == 0 ? oop_range_ : ip_range_;
    }
    const std::vector<int>& get_hand_map(int player) const {
        return player == 0 ? oop_hand_map_ : ip_hand_map_;
    }
};

} // namespace poker
