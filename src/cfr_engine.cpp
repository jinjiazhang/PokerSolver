#include "cfr_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>
#include <numeric>
#include <future>
#include <atomic>

namespace poker {

// ============================================================================
// StrategyStorage implementation
// ============================================================================

void StrategyStorage::init(int num_player_nodes, int max_hands,
                           const std::vector<int>& actions_per_node) {
    num_nodes_ = num_player_nodes;
    max_hands_ = max_hands;
    actions_per_node_ = actions_per_node;

    // Find max actions
    max_actions_ = 0;
    for (int a : actions_per_node_) {
        max_actions_ = std::max(max_actions_, a);
    }

    size_t total = static_cast<size_t>(num_nodes_) * max_hands_ * max_actions_;
    regrets_.assign(total, 0.0f);
    strategy_sums_.assign(total, 0.0f);
}

float& StrategyStorage::regret(int node_idx, int hand_idx, int action_idx) {
    return regrets_[index(node_idx, hand_idx, action_idx)];
}

float StrategyStorage::regret(int node_idx, int hand_idx, int action_idx) const {
    return regrets_[index(node_idx, hand_idx, action_idx)];
}

float& StrategyStorage::strategy_sum(int node_idx, int hand_idx, int action_idx) {
    return strategy_sums_[index(node_idx, hand_idx, action_idx)];
}

float StrategyStorage::strategy_sum(int node_idx, int hand_idx, int action_idx) const {
    return strategy_sums_[index(node_idx, hand_idx, action_idx)];
}

void StrategyStorage::get_current_strategy(int node_idx, int hand_idx, 
                                            int num_actions, float* strategy) const {
    // Regret matching: strategy proportional to positive regrets
    float sum = 0;
    for (int a = 0; a < num_actions; ++a) {
        float r = regrets_[index(node_idx, hand_idx, a)];
        strategy[a] = (r > 0) ? r : 0;
        sum += strategy[a];
    }
    
    if (sum > 0) {
        float inv = 1.0f / sum;
        for (int a = 0; a < num_actions; ++a) {
            strategy[a] *= inv;
        }
    } else {
        // Uniform strategy if no positive regrets
        float uniform = 1.0f / num_actions;
        for (int a = 0; a < num_actions; ++a) {
            strategy[a] = uniform;
        }
    }
}

void StrategyStorage::get_average_strategy(int node_idx, int hand_idx,
                                            int num_actions, float* strategy) const {
    float sum = 0;
    for (int a = 0; a < num_actions; ++a) {
        float s = strategy_sums_[index(node_idx, hand_idx, a)];
        strategy[a] = (s > 0) ? s : 0;
        sum += strategy[a];
    }

    if (sum > 0) {
        float inv = 1.0f / sum;
        for (int a = 0; a < num_actions; ++a) {
            strategy[a] *= inv;
        }
    } else {
        float uniform = 1.0f / num_actions;
        for (int a = 0; a < num_actions; ++a) {
            strategy[a] = uniform;
        }
    }
}

void StrategyStorage::discount(int iteration) {
    // DCFR discounting scheme:
    // Positive regrets: multiply by t^alpha / (t^alpha + 1)
    // Negative regrets: multiply by t^beta / (t^beta + 1)  
    // Strategy sums:   multiply by (t / (t+1))^gamma
    
    double t = static_cast<double>(iteration);
    
    // Default DCFR params: alpha=1.5, beta=0, gamma=2
    double pos_discount = std::pow(t, 1.5) / (std::pow(t, 1.5) + 1.0);
    double neg_discount = 0.0; // beta=0 means forget negative regrets completely
    double strat_discount = std::pow(t / (t + 1.0), 2.0);

    size_t total = regrets_.size();
    
    for (size_t i = 0; i < total; ++i) {
        float r = regrets_[i];
        if (r > 0) {
            regrets_[i] = static_cast<float>(r * pos_discount);
        } else {
            regrets_[i] = static_cast<float>(r * neg_discount);
        }
    }

    for (size_t i = 0; i < total; ++i) {
        strategy_sums_[i] = static_cast<float>(strategy_sums_[i] * strat_discount);
    }
}

size_t StrategyStorage::memory_usage() const {
    return regrets_.size() * sizeof(float) + strategy_sums_.size() * sizeof(float);
}

// ============================================================================
// ThreadLocalAccumulator implementation
// ============================================================================

void ThreadLocalAccumulator::init(size_t total_entries) {
    regret_deltas_.assign(total_entries, 0.0f);
    strategy_deltas_.assign(total_entries, 0.0f);
}

void ThreadLocalAccumulator::clear() {
    std::fill(regret_deltas_.begin(), regret_deltas_.end(), 0.0f);
    std::fill(strategy_deltas_.begin(), strategy_deltas_.end(), 0.0f);
}

// ============================================================================
// CFRSolver implementation
// ============================================================================

CFRSolver::CFRSolver(const GameParams& game_params, const Config& config)
    : game_params_(game_params), config_(config), builder_(game_params) {}

void CFRSolver::set_oop_range(const std::vector<Hand>& range) {
    oop_range_ = range;
}

void CFRSolver::set_ip_range(const std::vector<Hand>& range) {
    ip_range_ = range;
}

void CFRSolver::build_hand_maps() {
    oop_hand_map_.assign(NUM_COMBOS, -1);
    ip_hand_map_.assign(NUM_COMBOS, -1);
    
    for (int i = 0; i < static_cast<int>(oop_range_.size()); ++i) {
        oop_hand_map_[oop_range_[i].combo_index()] = i;
    }
    for (int i = 0; i < static_cast<int>(ip_range_.size()); ++i) {
        ip_hand_map_[ip_range_[i].combo_index()] = i;
    }
}

void CFRSolver::build() {
    std::cout << "Building game tree..." << std::endl;
    root_ = builder_.build();
    
    std::cout << "  Player nodes: " << builder_.num_player_nodes() << std::endl;
    std::cout << "  Total nodes:  " << builder_.num_total_nodes() << std::endl;

    build_hand_maps();

    // Count actions per node
    std::vector<int> actions_per_node(builder_.num_player_nodes(), 0);
    
    std::function<void(const GameTreeNode*)> count_actions = [&](const GameTreeNode* node) {
        if (!node) return;
        if (node->is_player() && node->node_index >= 0) {
            actions_per_node[node->node_index] = node->num_actions();
        }
        for (const auto& child : node->children) {
            count_actions(child.get());
        }
    };
    count_actions(root_.get());

    int max_hands = std::max(static_cast<int>(oop_range_.size()), 
                             static_cast<int>(ip_range_.size()));
    
    storage_.init(builder_.num_player_nodes(), max_hands, actions_per_node);

    // Precompute hand ranks and matchups for river boards
    if (game_params_.board.count == 5) {
        precompute_matchups(game_params_.board.mask);
    }

    // Initialize thread-local accumulators
    int num_threads = std::max(1, config_.num_threads);
    if (num_threads > 1) {
        thread_accumulators_.resize(num_threads);
        for (auto& accum : thread_accumulators_) {
            accum.init(storage_.total_entries());
        }
        std::cout << "  Threads:   " << num_threads << std::endl;
        std::cout << "  Thread accumulator memory: " 
                  << (num_threads * storage_.total_entries() * sizeof(float) * 2) / (1024 * 1024) 
                  << " MB" << std::endl;
    }

    std::cout << "  OOP range: " << oop_range_.size() << " combos" << std::endl;
    std::cout << "  IP range:  " << ip_range_.size() << " combos" << std::endl;
    std::cout << "  Memory:    " << storage_.memory_usage() / (1024 * 1024) << " MB" << std::endl;
}

void CFRSolver::precompute_matchups(CardMask board_mask) {
    const auto& eval = get_evaluator();
    int num_oop = static_cast<int>(oop_range_.size());
    int num_ip = static_cast<int>(ip_range_.size());
    int board_count = mask_count(board_mask & FULL_DECK_MASK);

    // Precompute hand ranks
    oop_hand_ranks_.resize(num_oop);
    ip_hand_ranks_.resize(num_ip);

    for (int i = 0; i < num_oop; ++i) {
        if (oop_range_[i].conflicts_with(board_mask)) {
            oop_hand_ranks_[i] = 0;
        } else {
            oop_hand_ranks_[i] = eval.evaluate(board_mask, board_count, oop_range_[i]);
        }
    }
    for (int i = 0; i < num_ip; ++i) {
        if (ip_range_[i].conflicts_with(board_mask)) {
            ip_hand_ranks_[i] = 0;
        } else {
            ip_hand_ranks_[i] = eval.evaluate(board_mask, board_count, ip_range_[i]);
        }
    }

    // Precompute all OOP vs IP matchup results
    matchup_cache_.resize(static_cast<size_t>(num_oop) * num_ip, 0);
    for (int t = 0; t < num_oop; ++t) {
        if (oop_hand_ranks_[t] == 0) continue;
        for (int o = 0; o < num_ip; ++o) {
            if (ip_hand_ranks_[o] == 0) continue;
            if (oop_range_[t].mask() & ip_range_[o].mask()) continue;

            int cmp = HandEvaluator::compare(oop_hand_ranks_[t], ip_hand_ranks_[o]);
            matchup_cache_[static_cast<size_t>(t) * num_ip + o] = 
                (cmp > 0) ? 1 : (cmp < 0) ? -1 : 0;
        }
    }

    has_precomputed_matchups_ = true;
    std::cout << "  Precomputed " << num_oop << "x" << num_ip << " matchup table" << std::endl;
}

// ============================================================================
// Solve entry point - dispatches to single-threaded or parallel
// ============================================================================

void CFRSolver::solve() {
    int num_threads = std::max(1, config_.num_threads);
    bool use_parallel = (num_threads > 1);

    std::cout << "\nStarting DCFR solver (" << config_.num_iterations << " iterations";
    if (use_parallel) {
        std::cout << ", " << num_threads << " threads";
    }
    std::cout << ")...\n";
    std::cout << std::string(60, '=') << std::endl;

    auto total_start = std::chrono::high_resolution_clock::now();

    for (int iter = 1; iter <= config_.num_iterations; ++iter) {
        auto iter_start = std::chrono::high_resolution_clock::now();

        if (use_parallel) {
            cfr_iteration_parallel(iter);
        } else {
            cfr_iteration(iter);
        }

        if (config_.use_dcfr) {
            storage_.discount(iter);
        }

        auto iter_end = std::chrono::high_resolution_clock::now();
        double iter_ms = std::chrono::duration<double, std::milli>(iter_end - iter_start).count();

        if (config_.print_progress && (iter % config_.print_interval == 0 || iter == 1)) {
            double elapsed = std::chrono::duration<double>(iter_end - total_start).count();
            double avg_ms = elapsed * 1000.0 / iter;
            double eta = avg_ms * (config_.num_iterations - iter) / 1000.0;
            
            std::cout << "  Iter " << std::setw(4) << iter 
                      << " | " << std::fixed << std::setprecision(1) 
                      << iter_ms << " ms"
                      << " | avg " << avg_ms << " ms"
                      << " | ETA " << std::setprecision(0) << eta << "s"
                      << std::endl;
        }
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    double total_s = std::chrono::duration<double>(total_end - total_start).count();

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Solving complete in " << std::fixed << std::setprecision(2) 
              << total_s << " seconds";
    if (use_parallel) {
        std::cout << " (" << num_threads << " threads)";
    }
    std::cout << std::endl;
}

// ============================================================================
// Single-threaded CFR iteration (unchanged original logic)
// ============================================================================

void CFRSolver::cfr_iteration(int iteration) {
    // Run CFR traversal for both players
    for (int traverser = 0; traverser < 2; ++traverser) {
        const auto& trav_range = get_range(traverser);
        int num_trav_hands = static_cast<int>(trav_range.size());
        int num_oop_hands = static_cast<int>(oop_range_.size());
        int num_ip_hands = static_cast<int>(ip_range_.size());

        // Initialize reach probabilities to 1.0 for all hands
        std::vector<float> oop_reach(num_oop_hands, 1.0f);
        std::vector<float> ip_reach(num_ip_hands, 1.0f);

        // Output hand values
        std::vector<float> hand_values(num_trav_hands, 0.0f);

        cfr_traverse(root_.get(), traverser, oop_reach, ip_reach, 
                     hand_values, game_params_.board.mask, iteration);
    }
}

void CFRSolver::cfr_traverse(
    const GameTreeNode* node,
    int traversing_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards,
    int iteration)
{
    if (!node) return;

    if (node->is_terminal() || node->type == NodeType::FOLD) {
        cfr_traverse_terminal(node, traversing_player, oop_reach, ip_reach,
                              hand_values, dead_cards);
        return;
    }

    if (node->is_chance()) {
        cfr_traverse_chance(node, traversing_player, oop_reach, ip_reach,
                            hand_values, dead_cards, iteration);
        return;
    }

    // Player node
    int acting_player = node->player;
    const auto& acting_range = get_range(acting_player);
    const auto& trav_range = get_range(traversing_player);
    int num_actions = node->num_actions();
    int num_acting_hands = static_cast<int>(acting_range.size());
    int num_trav_hands = static_cast<int>(trav_range.size());

    // Get current strategy for all hands of the acting player
    std::vector<std::vector<float>> strategies(num_acting_hands, std::vector<float>(num_actions));
    for (int h = 0; h < num_acting_hands; ++h) {
        storage_.get_current_strategy(node->node_index, h, num_actions, strategies[h].data());
    }

    if (acting_player == traversing_player) {
        // Traverser's node: compute action values and update regrets
        std::vector<std::vector<float>> action_values(num_actions, 
            std::vector<float>(num_trav_hands, 0.0f));

        for (int a = 0; a < num_actions; ++a) {
            // Compute new reach probs for this action
            std::vector<float> new_oop_reach, new_ip_reach;
            
            if (traversing_player == 0) {
                new_oop_reach.resize(oop_reach.size());
                for (int h = 0; h < num_trav_hands; ++h) {
                    new_oop_reach[h] = oop_reach[h] * strategies[h][a];
                }
                new_ip_reach = ip_reach;
            } else {
                new_oop_reach = oop_reach;
                new_ip_reach.resize(ip_reach.size());
                for (int h = 0; h < num_trav_hands; ++h) {
                    new_ip_reach[h] = ip_reach[h] * strategies[h][a];
                }
            }

            cfr_traverse(node->children[a].get(), traversing_player,
                         new_oop_reach, new_ip_reach, action_values[a],
                         dead_cards, iteration);
        }

        // Compute node values and update regrets
        hand_values.assign(num_trav_hands, 0.0f);
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                hand_values[h] += strategies[h][a] * action_values[a][h];
            }
        }

        // Update regrets
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                float regret_delta = action_values[a][h] - hand_values[h];
                storage_.regret(node->node_index, h, a) += regret_delta;
            }
        }

        // Update strategy sums (weighted by reach probability)
        const auto& reach = (traversing_player == 0) ? oop_reach : ip_reach;
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                storage_.strategy_sum(node->node_index, h, a) += 
                    reach[h] * strategies[h][a];
            }
        }

    } else {
        // Opponent's node: sum over all opponent actions weighted by strategy
        std::vector<std::vector<float>> action_values(num_actions,
            std::vector<float>(num_trav_hands, 0.0f));

        for (int a = 0; a < num_actions; ++a) {
            std::vector<float> new_oop_reach, new_ip_reach;

            if (acting_player == 0) {
                new_oop_reach.resize(oop_reach.size());
                for (int h = 0; h < num_acting_hands; ++h) {
                    new_oop_reach[h] = oop_reach[h] * strategies[h][a];
                }
                new_ip_reach = ip_reach;
            } else {
                new_oop_reach = oop_reach;
                new_ip_reach.resize(ip_reach.size());
                for (int h = 0; h < num_acting_hands; ++h) {
                    new_ip_reach[h] = ip_reach[h] * strategies[h][a];
                }
            }

            cfr_traverse(node->children[a].get(), traversing_player,
                         new_oop_reach, new_ip_reach, action_values[a],
                         dead_cards, iteration);
        }

        // Sum action values (they already incorporate opponent's strategy via reach)
        hand_values.assign(num_trav_hands, 0.0f);
        for (int a = 0; a < num_actions; ++a) {
            for (int h = 0; h < num_trav_hands; ++h) {
                hand_values[h] += action_values[a][h];
            }
        }
    }
}

void CFRSolver::cfr_traverse_terminal(
    const GameTreeNode* node,
    int traversing_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards)
{
    if (node->type == NodeType::FOLD) {
        compute_fold_payoffs(traversing_player, node->player, node->pot,
                             oop_reach, ip_reach, hand_values, dead_cards);
    } else {
        // Showdown
        compute_showdown_payoffs(traversing_player, node->pot,
                                 oop_reach, ip_reach, hand_values, dead_cards);
    }
}

void CFRSolver::cfr_traverse_chance(
    const GameTreeNode* node,
    int traversing_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards,
    int iteration)
{
    // Chance node: deal a new community card
    const auto& trav_range = get_range(traversing_player);
    int num_trav_hands = static_cast<int>(trav_range.size());
    hand_values.assign(num_trav_hands, 0.0f);

    int num_deals = 0;
    
    for (int card = 0; card < NUM_CARDS; ++card) {
        if (mask_has_card(dead_cards, card)) continue;

        CardMask new_dead = mask_add_card(dead_cards, card);
        
        // Filter hands that conflict with the dealt card
        std::vector<float> new_oop_reach(oop_reach.size());
        for (int h = 0; h < static_cast<int>(oop_range_.size()); ++h) {
            if (mask_has_card(oop_range_[h].mask(), card)) {
                new_oop_reach[h] = 0.0f;
            } else {
                new_oop_reach[h] = oop_reach[h];
            }
        }

        std::vector<float> new_ip_reach(ip_reach.size());
        for (int h = 0; h < static_cast<int>(ip_range_.size()); ++h) {
            if (mask_has_card(ip_range_[h].mask(), card)) {
                new_ip_reach[h] = 0.0f;
            } else {
                new_ip_reach[h] = ip_reach[h];
            }
        }

        std::vector<float> card_values(num_trav_hands, 0.0f);
        
        cfr_traverse(node->children[0].get(), traversing_player,
                     new_oop_reach, new_ip_reach, card_values,
                     new_dead, iteration);

        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] += card_values[h];
        }
        num_deals++;
    }

    // Average over deals
    if (num_deals > 0) {
        float inv = 1.0f / num_deals;
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] *= inv;
        }
    }
}

// ============================================================================
// Multi-threaded parallel CFR iteration
//
// Strategy: Parallelize at the FIRST chance node encountered in the tree.
// Each deal card's subtree is independent and assigned to a thread.
// Thread-local accumulators collect regret/strategy-sum deltas.
// After all threads finish, accumulators are merged into global storage.
// ============================================================================

void CFRSolver::cfr_iteration_parallel(int iteration) {
    for (int traverser = 0; traverser < 2; ++traverser) {
        const auto& trav_range = get_range(traverser);
        int num_trav_hands = static_cast<int>(trav_range.size());
        int num_oop_hands = static_cast<int>(oop_range_.size());
        int num_ip_hands = static_cast<int>(ip_range_.size());

        std::vector<float> oop_reach(num_oop_hands, 1.0f);
        std::vector<float> ip_reach(num_ip_hands, 1.0f);
        std::vector<float> hand_values(num_trav_hands, 0.0f);

        // Clear all accumulators
        for (auto& accum : thread_accumulators_) {
            accum.clear();
        }

        // Check if root is a chance node -> parallelize deal cards
        // Otherwise, check if we encounter a chance node in the first 
        // few levels (common in turn/river solve from flop)
        if (root_->is_chance()) {
            // Root is chance node: parallelize directly over deal cards
            cfr_traverse_chance_parallel(root_.get(), traverser, oop_reach, ip_reach,
                                         hand_values, game_params_.board.mask, iteration);
        } else {
            // Root is a player node: use threaded traversal that 
            // parallelizes at the first chance node encountered deeper in the tree
            // For now, traverse single-threaded until chance, then parallelize
            
            // Use thread-local accumulators with single traversal
            // that spawns threads at chance nodes
            cfr_traverse_threaded(root_.get(), traverser, oop_reach, ip_reach,
                                  hand_values, game_params_.board.mask, iteration,
                                  thread_accumulators_[0]);
        }

        // Merge all thread-local accumulators into global storage
        merge_accumulators();
    }
}

// ============================================================================
// Thread-local CFR traversal
// Same logic as cfr_traverse, but writes deltas to a ThreadLocalAccumulator
// instead of directly to StrategyStorage. When it encounters a chance node,
// it dispatches to the parallel chance traversal.
// ============================================================================

void CFRSolver::cfr_traverse_threaded(
    const GameTreeNode* node,
    int traversing_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards,
    int iteration,
    ThreadLocalAccumulator& accum)
{
    if (!node) return;

    if (node->is_terminal() || node->type == NodeType::FOLD) {
        cfr_traverse_terminal_threaded(node, traversing_player, oop_reach, ip_reach,
                                        hand_values, dead_cards);
        return;
    }

    if (node->is_chance()) {
        // Dispatch to parallel chance traversal (spawns threads)
        cfr_traverse_chance_parallel(node, traversing_player, oop_reach, ip_reach,
                                     hand_values, dead_cards, iteration);
        return;
    }

    // Player node
    int acting_player = node->player;
    const auto& acting_range = get_range(acting_player);
    const auto& trav_range = get_range(traversing_player);
    int num_actions = node->num_actions();
    int num_acting_hands = static_cast<int>(acting_range.size());
    int num_trav_hands = static_cast<int>(trav_range.size());

    // Get current strategy (reads from global storage - safe, read-only)
    std::vector<std::vector<float>> strategies(num_acting_hands, std::vector<float>(num_actions));
    for (int h = 0; h < num_acting_hands; ++h) {
        storage_.get_current_strategy(node->node_index, h, num_actions, strategies[h].data());
    }

    if (acting_player == traversing_player) {
        std::vector<std::vector<float>> action_values(num_actions, 
            std::vector<float>(num_trav_hands, 0.0f));

        for (int a = 0; a < num_actions; ++a) {
            std::vector<float> new_oop_reach, new_ip_reach;
            
            if (traversing_player == 0) {
                new_oop_reach.resize(oop_reach.size());
                for (int h = 0; h < num_trav_hands; ++h) {
                    new_oop_reach[h] = oop_reach[h] * strategies[h][a];
                }
                new_ip_reach = ip_reach;
            } else {
                new_oop_reach = oop_reach;
                new_ip_reach.resize(ip_reach.size());
                for (int h = 0; h < num_trav_hands; ++h) {
                    new_ip_reach[h] = ip_reach[h] * strategies[h][a];
                }
            }

            cfr_traverse_threaded(node->children[a].get(), traversing_player,
                                  new_oop_reach, new_ip_reach, action_values[a],
                                  dead_cards, iteration, accum);
        }

        // Compute node values
        hand_values.assign(num_trav_hands, 0.0f);
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                hand_values[h] += strategies[h][a] * action_values[a][h];
            }
        }

        // Write regret deltas to thread-local accumulator
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                float regret_delta = action_values[a][h] - hand_values[h];
                size_t idx = storage_.index(node->node_index, h, a);
                accum.regret_delta(idx) += regret_delta;
            }
        }

        // Write strategy sum deltas to thread-local accumulator
        const auto& reach = (traversing_player == 0) ? oop_reach : ip_reach;
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                size_t idx = storage_.index(node->node_index, h, a);
                accum.strategy_delta(idx) += reach[h] * strategies[h][a];
            }
        }

    } else {
        std::vector<std::vector<float>> action_values(num_actions,
            std::vector<float>(num_trav_hands, 0.0f));

        for (int a = 0; a < num_actions; ++a) {
            std::vector<float> new_oop_reach, new_ip_reach;

            if (acting_player == 0) {
                new_oop_reach.resize(oop_reach.size());
                for (int h = 0; h < num_acting_hands; ++h) {
                    new_oop_reach[h] = oop_reach[h] * strategies[h][a];
                }
                new_ip_reach = ip_reach;
            } else {
                new_oop_reach = oop_reach;
                new_ip_reach.resize(ip_reach.size());
                for (int h = 0; h < num_acting_hands; ++h) {
                    new_ip_reach[h] = ip_reach[h] * strategies[h][a];
                }
            }

            cfr_traverse_threaded(node->children[a].get(), traversing_player,
                                  new_oop_reach, new_ip_reach, action_values[a],
                                  dead_cards, iteration, accum);
        }

        hand_values.assign(num_trav_hands, 0.0f);
        for (int a = 0; a < num_actions; ++a) {
            for (int h = 0; h < num_trav_hands; ++h) {
                hand_values[h] += action_values[a][h];
            }
        }
    }
}

void CFRSolver::cfr_traverse_terminal_threaded(
    const GameTreeNode* node,
    int traversing_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards)
{
    // Terminal nodes have no regrets to update, just compute payoffs
    if (node->type == NodeType::FOLD) {
        compute_fold_payoffs(traversing_player, node->player, node->pot,
                             oop_reach, ip_reach, hand_values, dead_cards);
    } else {
        compute_showdown_payoffs(traversing_player, node->pot,
                                 oop_reach, ip_reach, hand_values, dead_cards);
    }
}

// ============================================================================
// Parallel chance node traversal
// Each deal card's subtree gets assigned to a thread from the pool.
// Each thread has its own accumulator for regret/strategy-sum deltas.
// ============================================================================

void CFRSolver::cfr_traverse_chance_parallel(
    const GameTreeNode* node,
    int traversing_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards,
    int iteration)
{
    const auto& trav_range = get_range(traversing_player);
    int num_trav_hands = static_cast<int>(trav_range.size());
    hand_values.assign(num_trav_hands, 0.0f);

    int num_threads = std::max(1, config_.num_threads);

    // Collect all valid deal cards
    std::vector<int> deal_cards;
    deal_cards.reserve(52);
    for (int card = 0; card < NUM_CARDS; ++card) {
        if (!mask_has_card(dead_cards, card)) {
            deal_cards.push_back(card);
        }
    }

    int num_deals = static_cast<int>(deal_cards.size());
    if (num_deals == 0) return;

    // Each thread processes a contiguous chunk of deal cards
    // and produces thread-local hand values + writes to its own accumulator
    struct ThreadResult {
        std::vector<float> partial_values;
        int num_deals_processed = 0;
    };

    std::vector<ThreadResult> thread_results(num_threads);
    for (auto& tr : thread_results) {
        tr.partial_values.assign(num_trav_hands, 0.0f);
    }

    // Worker function for each thread
    auto worker = [&](int thread_id, int start_deal, int end_deal) {
        ThreadLocalAccumulator& accum = thread_accumulators_[thread_id];
        auto& result = thread_results[thread_id];

        for (int d = start_deal; d < end_deal; ++d) {
            int card = deal_cards[d];
            CardMask new_dead = mask_add_card(dead_cards, card);

            // Filter hands that conflict with the dealt card
            std::vector<float> new_oop_reach(oop_reach.size());
            for (int h = 0; h < static_cast<int>(oop_range_.size()); ++h) {
                if (mask_has_card(oop_range_[h].mask(), card)) {
                    new_oop_reach[h] = 0.0f;
                } else {
                    new_oop_reach[h] = oop_reach[h];
                }
            }

            std::vector<float> new_ip_reach(ip_reach.size());
            for (int h = 0; h < static_cast<int>(ip_range_.size()); ++h) {
                if (mask_has_card(ip_range_[h].mask(), card)) {
                    new_ip_reach[h] = 0.0f;
                } else {
                    new_ip_reach[h] = ip_reach[h];
                }
            }

            std::vector<float> card_values(num_trav_hands, 0.0f);

            // Traverse the child subtree using thread-local accumulator
            // Note: children[0] is the shared subtree for all deal cards at a chance node
            cfr_traverse_threaded(node->children[0].get(), traversing_player,
                                  new_oop_reach, new_ip_reach, card_values,
                                  new_dead, iteration, accum);

            for (int h = 0; h < num_trav_hands; ++h) {
                result.partial_values[h] += card_values[h];
            }
            result.num_deals_processed++;
        }
    };

    // Distribute deal cards across threads
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    int deals_per_thread = num_deals / num_threads;
    int remaining = num_deals % num_threads;
    int current = 0;

    for (int t = 0; t < num_threads; ++t) {
        int count = deals_per_thread + (t < remaining ? 1 : 0);
        int start = current;
        int end = current + count;
        current = end;

        if (count > 0) {
            threads.emplace_back(worker, t, start, end);
        }
    }

    // Wait for all threads
    for (auto& th : threads) {
        th.join();
    }

    // Aggregate hand values from all threads
    int total_deals = 0;
    for (const auto& tr : thread_results) {
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] += tr.partial_values[h];
        }
        total_deals += tr.num_deals_processed;
    }

    // Average over all deals
    if (total_deals > 0) {
        float inv = 1.0f / total_deals;
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] *= inv;
        }
    }
}

// ============================================================================
// Merge all thread-local accumulators into global StrategyStorage
// ============================================================================

void CFRSolver::merge_accumulators() {
    size_t total = storage_.total_entries();
    float* global_regrets = storage_.regret_data();
    float* global_strategy = storage_.strategy_sum_data();

    for (const auto& accum : thread_accumulators_) {
        if (accum.size() == 0) continue;
        const float* reg_data = accum.regret_data();
        const float* strat_data = accum.strategy_data();

        for (size_t i = 0; i < total; ++i) {
            global_regrets[i] += reg_data[i];
            global_strategy[i] += strat_data[i];
        }
    }
}

// ============================================================================
// Payoff computations (unchanged, read-only, thread-safe)
// ============================================================================

void CFRSolver::compute_showdown_payoffs(
    int traversing_player,
    double pot,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards) const
{
    const auto& trav_range = get_range(traversing_player);
    const auto& opp_range = get_range(1 - traversing_player);
    const auto& opp_reach = (traversing_player == 0) ? ip_reach : oop_reach;
    
    int num_trav = static_cast<int>(trav_range.size());
    int num_opp = static_cast<int>(opp_range.size());
    int num_ip = static_cast<int>(ip_range_.size());
    
    hand_values.assign(num_trav, 0.0f);
    float half_pot = static_cast<float>(pot / 2.0);

    if (has_precomputed_matchups_ && dead_cards == game_params_.board.mask) {
        // Fast path: use precomputed matchup cache
        for (int t = 0; t < num_trav; ++t) {
            const Hand& trav_hand = trav_range[t];
            if (trav_hand.conflicts_with(dead_cards)) continue;

            float ev = 0.0f;
            for (int o = 0; o < num_opp; ++o) {
                const Hand& opp_hand = opp_range[o];
                if (opp_hand.conflicts_with(dead_cards)) continue;
                if (trav_hand.mask() & opp_hand.mask()) continue;

                float opp_r = opp_reach[o];
                if (opp_r <= 0) continue;

                int8_t result;
                if (traversing_player == 0) {
                    result = matchup_cache_[static_cast<size_t>(t) * num_ip + o];
                } else {
                    result = -matchup_cache_[static_cast<size_t>(o) * num_ip + t];
                }

                if (result > 0) {
                    ev += opp_r * half_pot;
                } else if (result < 0) {
                    ev -= opp_r * half_pot;
                }
            }
            hand_values[t] = ev;
        }
    } else {
        // Slow path: evaluate hands on the fly
        const auto& eval = get_evaluator();
        int board_count = mask_count(dead_cards & FULL_DECK_MASK);

        for (int t = 0; t < num_trav; ++t) {
            const Hand& trav_hand = trav_range[t];
            if (trav_hand.conflicts_with(dead_cards)) continue;

            uint16_t trav_rank = eval.evaluate(dead_cards, board_count, trav_hand);

            float ev = 0.0f;
            for (int o = 0; o < num_opp; ++o) {
                const Hand& opp_hand = opp_range[o];
                if (opp_hand.conflicts_with(dead_cards)) continue;
                if (trav_hand.mask() & opp_hand.mask()) continue;

                float opp_r = opp_reach[o];
                if (opp_r <= 0) continue;
                
                uint16_t opp_rank = eval.evaluate(dead_cards, board_count, opp_hand);

                int cmp = HandEvaluator::compare(trav_rank, opp_rank);
                if (cmp > 0) {
                    ev += opp_r * half_pot;
                } else if (cmp < 0) {
                    ev -= opp_r * half_pot;
                }
            }
            hand_values[t] = ev;
        }
    }
}

void CFRSolver::compute_fold_payoffs(
    int traversing_player,
    int folder,
    double pot,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards) const
{
    const auto& trav_range = get_range(traversing_player);
    const auto& opp_range = get_range(1 - traversing_player);
    const auto& opp_reach = (traversing_player == 0) ? ip_reach : oop_reach;
    
    int num_trav = static_cast<int>(trav_range.size());
    int num_opp = static_cast<int>(opp_range.size());
    
    hand_values.assign(num_trav, 0.0f);
    double half_pot = pot / 2.0;

    for (int t = 0; t < num_trav; ++t) {
        const Hand& trav_hand = trav_range[t];
        if (trav_hand.conflicts_with(dead_cards)) continue;

        float ev = 0.0f;
        for (int o = 0; o < num_opp; ++o) {
            const Hand& opp_hand = opp_range[o];
            if (opp_hand.conflicts_with(dead_cards)) continue;
            if (trav_hand.mask() & opp_hand.mask()) continue;

            float opp_r = opp_reach[o];
            if (opp_r <= 0) continue;

            if (folder == traversing_player) {
                ev -= opp_r * static_cast<float>(half_pot);
            } else {
                ev += opp_r * static_cast<float>(half_pot);
            }
        }

        hand_values[t] = ev;
    }
}

// ============================================================================
// Query / export (unchanged from original)
// ============================================================================

void CFRSolver::get_strategy(int node_index, const Hand& hand, int player,
                              std::vector<float>& strategy) const {
    const auto& range = get_range(player);
    int hand_idx = -1;
    for (int i = 0; i < static_cast<int>(range.size()); ++i) {
        if (range[i].cards[0] == hand.cards[0] && range[i].cards[1] == hand.cards[1]) {
            hand_idx = i;
            break;
        }
    }

    if (hand_idx < 0) {
        return;
    }

    std::function<const GameTreeNode*(const GameTreeNode*)> find_node = 
        [&](const GameTreeNode* n) -> const GameTreeNode* {
        if (!n) return nullptr;
        if (n->is_player() && n->node_index == node_index) return n;
        for (const auto& child : n->children) {
            auto result = find_node(child.get());
            if (result) return result;
        }
        return nullptr;
    };

    const GameTreeNode* node = find_node(root_.get());
    if (!node) return;

    int num_actions = node->num_actions();
    strategy.resize(num_actions);
    storage_.get_average_strategy(node_index, hand_idx, num_actions, strategy.data());
}

double CFRSolver::get_exploitability() const {
    return -1.0; // TODO: implement full best response computation
}

std::string CFRSolver::export_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"solver\": \"PokerSolver DCFR\",\n";
    json << "  \"iterations\": " << config_.num_iterations << ",\n";
    json << "  \"threads\": " << config_.num_threads << ",\n";
    json << "  \"pot\": " << game_params_.initial_pot << ",\n";
    json << "  \"stack\": " << game_params_.effective_stack << ",\n";
    json << "  \"board\": \"";
    for (int i = 0; i < game_params_.board.count; ++i) {
        json << card_to_string(game_params_.board.cards[i]);
    }
    json << "\",\n";

    // Export strategies for root node
    json << "  \"root_strategy\": {\n";
    
    const auto& root = root_;
    if (root && root->is_player()) {
        int player = root->player;
        const auto& range = get_range(player);
        int num_actions = root->num_actions();
        
        json << "    \"player\": " << player << ",\n";
        json << "    \"actions\": [";
        for (int a = 0; a < num_actions; ++a) {
            if (a > 0) json << ", ";
            json << "\"" << root->actions[a].to_string() << "\"";
        }
        json << "],\n";
        json << "    \"hands\": {\n";
        
        std::vector<float> strategy(num_actions);
        for (int h = 0; h < static_cast<int>(range.size()); ++h) {
            const Hand& hand = range[h];
            if (hand.conflicts_with(game_params_.board.mask)) continue;
            
            storage_.get_average_strategy(root->node_index, h, num_actions, strategy.data());
            
            if (h > 0) json << ",\n";
            json << "      \"" << hand.to_string() << "\": [";
            for (int a = 0; a < num_actions; ++a) {
                if (a > 0) json << ", ";
                json << std::fixed << std::setprecision(4) << strategy[a];
            }
            json << "]";
        }
        json << "\n    }\n";
    }
    json << "  }\n";
    json << "}\n";
    
    return json.str();
}

} // namespace poker
