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
    double t = static_cast<double>(iteration);
    double pos_discount = std::pow(t, 1.5) / (std::pow(t, 1.5) + 1.0);
    double neg_discount = 0.0;
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

// ============================================================================
// Build suit isomorphism maps
// ============================================================================

void CFRSolver::build_isomorphism() {
    oop_iso_ = build_isomorphism_map(game_params_.board, oop_range_);
    ip_iso_ = build_isomorphism_map(game_params_.board, ip_range_);
    isomorphism_enabled_ = true;

    std::cout << "  Suit isomorphism:" << std::endl;
    std::cout << "    OOP: " << oop_range_.size() << " hands -> " 
              << oop_iso_.num_canonical << " canonical groups ("
              << std::fixed << std::setprecision(0)
              << (100.0 * (1.0 - static_cast<double>(oop_iso_.num_canonical) / 
                  std::max(1, static_cast<int>(oop_range_.size()))))
              << "% reduction)" << std::endl;
    std::cout << "    IP:  " << ip_range_.size() << " hands -> " 
              << ip_iso_.num_canonical << " canonical groups ("
              << std::fixed << std::setprecision(0)
              << (100.0 * (1.0 - static_cast<double>(ip_iso_.num_canonical) / 
                  std::max(1, static_cast<int>(ip_range_.size()))))
              << "% reduction)" << std::endl;
}

void CFRSolver::build() {
    std::cout << "Building game tree..." << std::endl;
    root_ = builder_.build();
    
    std::cout << "  Player nodes: " << builder_.num_player_nodes() << std::endl;
    std::cout << "  Total nodes:  " << builder_.num_total_nodes() << std::endl;

    build_hand_maps();

    // Build suit isomorphism if enabled
    if (config_.use_isomorphism) {
        build_isomorphism();
    }

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

    // With isomorphism, max_hands = max of canonical group counts
    // Without, max_hands = max of range sizes
    int max_hands;
    if (isomorphism_enabled_) {
        max_hands = std::max(oop_iso_.num_canonical, ip_iso_.num_canonical);
    } else {
        max_hands = std::max(static_cast<int>(oop_range_.size()), 
                             static_cast<int>(ip_range_.size()));
    }
    
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
// Solve entry point
// ============================================================================

void CFRSolver::solve() {
    int num_threads = std::max(1, config_.num_threads);
    bool use_parallel = (num_threads > 1);

    std::cout << "\nStarting DCFR solver (" << config_.num_iterations << " iterations";
    if (use_parallel) {
        std::cout << ", " << num_threads << " threads";
    }
    if (isomorphism_enabled_) {
        std::cout << ", isomorphism";
    }
    if (config_.target_exploitability > 0) {
        std::cout << ", target " << config_.target_exploitability << "%";
    }
    std::cout << ")...\n";
    std::cout << std::string(60, '=') << std::endl;

    auto total_start = std::chrono::high_resolution_clock::now();
    bool converged = false;
    int final_iter = config_.num_iterations;

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
            
            double exploit = compute_exploitability();
            
            std::cout << "  Iter " << std::setw(4) << iter 
                      << " | " << std::fixed << std::setprecision(1) 
                      << iter_ms << " ms"
                      << " | avg " << avg_ms << " ms"
                      << " | ETA " << std::setprecision(0) << eta << "s"
                      << " | exploit " << std::setprecision(3) << exploit << "%"
                      << std::endl;
            
            // Early stopping: check if exploitability is below target
            if (config_.target_exploitability > 0 && 
                std::abs(exploit) < config_.target_exploitability) {
                std::cout << "  >>> Converged! Exploitability " 
                          << std::setprecision(3) << std::abs(exploit) 
                          << "% < target " << config_.target_exploitability << "%\n";
                converged = true;
                final_iter = iter;
                break;
            }
        }
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    double total_s = std::chrono::duration<double>(total_end - total_start).count();

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Solving complete in " << std::fixed << std::setprecision(2) 
              << total_s << " seconds (" << final_iter << " iterations)";
    if (use_parallel) {
        std::cout << " (" << num_threads << " threads)";
    }
    if (converged) {
        std::cout << " [converged]";
    }
    std::cout << std::endl;
}

// ============================================================================
// Single-threaded CFR iteration
// ============================================================================

void CFRSolver::cfr_iteration(int iteration) {
    for (int traverser = 0; traverser < 2; ++traverser) {
        const auto& trav_range = get_range(traverser);
        int num_trav_hands = static_cast<int>(trav_range.size());
        int num_oop_hands = static_cast<int>(oop_range_.size());
        int num_ip_hands = static_cast<int>(ip_range_.size());

        std::vector<float> oop_reach(num_oop_hands);
        for (int i = 0; i < num_oop_hands; ++i) oop_reach[i] = oop_range_[i].weight;
        
        std::vector<float> ip_reach(num_ip_hands);
        for (int i = 0; i < num_ip_hands; ++i) ip_reach[i] = ip_range_[i].weight;
        
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
    // Use canonical index for storage lookup
    std::vector<std::vector<float>> strategies(num_acting_hands, std::vector<float>(num_actions));
    for (int h = 0; h < num_acting_hands; ++h) {
        int ci = get_canonical_index(acting_player, h);
        storage_.get_current_strategy(node->node_index, ci, num_actions, strategies[h].data());
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

            cfr_traverse(node->children[a].get(), traversing_player,
                         new_oop_reach, new_ip_reach, action_values[a],
                         dead_cards, iteration);
        }

        // Compute node values
        hand_values.assign(num_trav_hands, 0.0f);
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                hand_values[h] += strategies[h][a] * action_values[a][h];
            }
        }

        // Update regrets using canonical index
        for (int h = 0; h < num_trav_hands; ++h) {
            int ci = get_canonical_index(traversing_player, h);
            for (int a = 0; a < num_actions; ++a) {
                float regret_delta = action_values[a][h] - hand_values[h];
                storage_.regret(node->node_index, ci, a) += regret_delta;
            }
        }

        // Update strategy sums using canonical index
        const auto& reach = (traversing_player == 0) ? oop_reach : ip_reach;
        for (int h = 0; h < num_trav_hands; ++h) {
            int ci = get_canonical_index(traversing_player, h);
            for (int a = 0; a < num_actions; ++a) {
                storage_.strategy_sum(node->node_index, ci, a) += 
                    reach[h] * strategies[h][a];
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

            cfr_traverse(node->children[a].get(), traversing_player,
                         new_oop_reach, new_ip_reach, action_values[a],
                         dead_cards, iteration);
        }

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
    const auto& trav_range = get_range(traversing_player);
    int num_trav_hands = static_cast<int>(trav_range.size());
    int num_oop = static_cast<int>(oop_range_.size());
    int num_ip = static_cast<int>(ip_range_.size());
    hand_values.assign(num_trav_hands, 0.0f);

    // Pre-allocate buffers once, reuse for each deal card
    std::vector<float> new_oop_reach(num_oop);
    std::vector<float> new_ip_reach(num_ip);
    std::vector<float> card_values(num_trav_hands);
    int num_deals = 0;
    
    for (int card = 0; card < NUM_CARDS; ++card) {
        if (mask_has_card(dead_cards, card)) continue;

        CardMask new_dead = mask_add_card(dead_cards, card);
        
        for (int h = 0; h < num_oop; ++h) {
            new_oop_reach[h] = mask_has_card(oop_range_[h].mask(), card) ? 0.0f : oop_reach[h];
        }
        for (int h = 0; h < num_ip; ++h) {
            new_ip_reach[h] = mask_has_card(ip_range_[h].mask(), card) ? 0.0f : ip_reach[h];
        }

        std::fill(card_values.begin(), card_values.end(), 0.0f);
        
        cfr_traverse(node->children[0].get(), traversing_player,
                     new_oop_reach, new_ip_reach, card_values,
                     new_dead, iteration);

        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] += card_values[h];
        }
        num_deals++;
    }

    if (num_deals > 0) {
        float inv = 1.0f / num_deals;
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] *= inv;
        }
    }
}

// ============================================================================
// Multi-threaded parallel CFR iteration
// ============================================================================

void CFRSolver::cfr_iteration_parallel(int iteration) {
    for (int traverser = 0; traverser < 2; ++traverser) {
        const auto& trav_range = get_range(traverser);
        int num_trav_hands = static_cast<int>(trav_range.size());
        int num_oop_hands = static_cast<int>(oop_range_.size());
        int num_ip_hands = static_cast<int>(ip_range_.size());

        std::vector<float> oop_reach(num_oop_hands);
        for (int i = 0; i < num_oop_hands; ++i) oop_reach[i] = oop_range_[i].weight;
        
        std::vector<float> ip_reach(num_ip_hands);
        for (int i = 0; i < num_ip_hands; ++i) ip_reach[i] = ip_range_[i].weight;
        
        std::vector<float> hand_values(num_trav_hands, 0.0f);

        for (auto& accum : thread_accumulators_) {
            accum.clear();
        }

        if (root_->is_chance()) {
            cfr_traverse_chance_parallel(root_.get(), traverser, oop_reach, ip_reach,
                                         hand_values, game_params_.board.mask, iteration);
        } else {
            cfr_traverse_threaded(root_.get(), traverser, oop_reach, ip_reach,
                                  hand_values, game_params_.board.mask, iteration,
                                  thread_accumulators_[0]);
        }

        merge_accumulators();
    }
}

// ============================================================================
// Thread-local CFR traversal (with isomorphism support)
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

    // Get current strategy using canonical index
    std::vector<std::vector<float>> strategies(num_acting_hands, std::vector<float>(num_actions));
    for (int h = 0; h < num_acting_hands; ++h) {
        int ci = get_canonical_index(acting_player, h);
        storage_.get_current_strategy(node->node_index, ci, num_actions, strategies[h].data());
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

        hand_values.assign(num_trav_hands, 0.0f);
        for (int h = 0; h < num_trav_hands; ++h) {
            for (int a = 0; a < num_actions; ++a) {
                hand_values[h] += strategies[h][a] * action_values[a][h];
            }
        }

        // Write regret deltas using canonical index
        for (int h = 0; h < num_trav_hands; ++h) {
            int ci = get_canonical_index(traversing_player, h);
            for (int a = 0; a < num_actions; ++a) {
                float regret_delta = action_values[a][h] - hand_values[h];
                size_t idx = storage_.index(node->node_index, ci, a);
                accum.regret_delta(idx) += regret_delta;
            }
        }

        const auto& reach = (traversing_player == 0) ? oop_reach : ip_reach;
        for (int h = 0; h < num_trav_hands; ++h) {
            int ci = get_canonical_index(traversing_player, h);
            for (int a = 0; a < num_actions; ++a) {
                size_t idx = storage_.index(node->node_index, ci, a);
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
    int num_oop = static_cast<int>(oop_range_.size());
    int num_ip = static_cast<int>(ip_range_.size());
    hand_values.assign(num_trav_hands, 0.0f);

    int num_threads = std::max(1, config_.num_threads);

    std::vector<int> deal_cards;
    deal_cards.reserve(52);
    for (int card = 0; card < NUM_CARDS; ++card) {
        if (!mask_has_card(dead_cards, card)) {
            deal_cards.push_back(card);
        }
    }

    int num_deals = static_cast<int>(deal_cards.size());
    if (num_deals == 0) return;

    struct ThreadResult {
        std::vector<float> partial_values;
        int num_deals_processed = 0;
    };

    std::vector<ThreadResult> thread_results(num_threads);
    for (auto& tr : thread_results) {
        tr.partial_values.assign(num_trav_hands, 0.0f);
    }

    auto worker = [&](int thread_id, int start_deal, int end_deal) {
        ThreadLocalAccumulator& accum = thread_accumulators_[thread_id];
        auto& result = thread_results[thread_id];

        // Pre-allocate buffers per thread, reuse for each deal
        std::vector<float> new_oop_reach(num_oop);
        std::vector<float> new_ip_reach(num_ip);
        std::vector<float> card_values(num_trav_hands);

        for (int d = start_deal; d < end_deal; ++d) {
            int card = deal_cards[d];
            CardMask new_dead = mask_add_card(dead_cards, card);

            for (int h = 0; h < num_oop; ++h) {
                new_oop_reach[h] = mask_has_card(oop_range_[h].mask(), card) ? 0.0f : oop_reach[h];
            }
            for (int h = 0; h < num_ip; ++h) {
                new_ip_reach[h] = mask_has_card(ip_range_[h].mask(), card) ? 0.0f : ip_reach[h];
            }

            std::fill(card_values.begin(), card_values.end(), 0.0f);

            cfr_traverse_threaded(node->children[0].get(), traversing_player,
                                  new_oop_reach, new_ip_reach, card_values,
                                  new_dead, iteration, accum);

            for (int h = 0; h < num_trav_hands; ++h) {
                result.partial_values[h] += card_values[h];
            }
            result.num_deals_processed++;
        }
    };

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

    for (auto& th : threads) {
        th.join();
    }

    int total_deals = 0;
    for (const auto& tr : thread_results) {
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] += tr.partial_values[h];
        }
        total_deals += tr.num_deals_processed;
    }

    if (total_deals > 0) {
        float inv = 1.0f / total_deals;
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] *= inv;
        }
    }
}

// ============================================================================
// Merge accumulators
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
// Sorted range cache for O(M+N) showdown
// ============================================================================

const RiverSortedRange& CFRSolver::get_sorted_range(int player, CardMask board_mask) const {
    {
        std::lock_guard<std::mutex> lock(sorted_range_mutex_);
        auto it = sorted_range_cache_.find(board_mask);
        if (it != sorted_range_cache_.end()) {
            return it->second[player];
        }
    }

    // Build sorted ranges for both players on this board
    const auto& eval = get_evaluator();
    int board_count = mask_count(board_mask & FULL_DECK_MASK);
    std::array<RiverSortedRange, 2> ranges;

    for (int p = 0; p < 2; ++p) {
        const auto& range = get_range(p);
        int num_hands = static_cast<int>(range.size());
        ranges[p].hands.reserve(num_hands);

        for (int h = 0; h < num_hands; ++h) {
            const Hand& hand = range[h];
            if (hand.conflicts_with(board_mask)) continue;

            RankedHand rh;
            rh.rank = eval.evaluate(board_mask, board_count, hand);
            rh.hand_index = h;
            rh.card1 = hand.cards[0];
            rh.card2 = hand.cards[1];
            ranges[p].hands.push_back(rh);
        }

        // Sort descending by rank
        std::sort(ranges[p].hands.begin(), ranges[p].hands.end());
    }

    std::lock_guard<std::mutex> lock(sorted_range_mutex_);
    auto [it, _] = sorted_range_cache_.emplace(board_mask, std::move(ranges));
    return it->second[player];
}

// ============================================================================
// O(M+N) sorted sweep showdown
// ============================================================================

void CFRSolver::compute_showdown_sorted(
    int traversing_player,
    double pot,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask board_mask) const
{
    int opp_player = 1 - traversing_player;
    const auto& trav_sorted = get_sorted_range(traversing_player, board_mask);
    const auto& opp_sorted = get_sorted_range(opp_player, board_mask);
    const auto& opp_reach_vec = (traversing_player == 0) ? ip_reach : oop_reach;
    const auto& trav_range = get_range(traversing_player);
    int num_trav = static_cast<int>(trav_range.size());

    hand_values.assign(num_trav, 0.0f);
    float half_pot = static_cast<float>(pot / 2.0);

    const auto& trav_hands = trav_sorted.hands;
    const auto& opp_hands = opp_sorted.hands;
    int num_trav_sorted = static_cast<int>(trav_hands.size());
    int num_opp_sorted = static_cast<int>(opp_hands.size());

    if (num_trav_sorted == 0 || num_opp_sorted == 0) return;

    // Pass 1: compute winning payoffs (traverse from strongest to weakest)
    // For each trav hand, sum up opp reach of hands that are WEAKER
    {
        float winsum = 0.0f;
        float card_winsum[52] = {};

        int j = 0;
        for (int i = 0; i < num_trav_sorted; ++i) {
            const auto& th = trav_hands[i];
            // Advance j to include all opp hands with rank < trav rank (weaker)
            while (j < num_opp_sorted && opp_hands[j].rank > th.rank) {
                const auto& oh = opp_hands[j];
                float r = opp_reach_vec[oh.hand_index];
                winsum += r;
                card_winsum[oh.card1] += r;
                card_winsum[oh.card2] += r;
                j++;
            }
            // winsum includes all opp hands strictly weaker than trav
            // Subtract card conflicts (opp hands that share a card with trav)
            hand_values[th.hand_index] = (winsum
                                          - card_winsum[th.card1]
                                          - card_winsum[th.card2]
                                         ) * half_pot;
        }
    }

    // Pass 2: compute losing payoffs (traverse from weakest to strongest)
    {
        float losssum = 0.0f;
        float card_losssum[52] = {};

        int j = num_opp_sorted - 1;
        for (int i = num_trav_sorted - 1; i >= 0; --i) {
            const auto& th = trav_hands[i];
            // Advance j to include all opp hands with rank > trav rank (stronger)
            while (j >= 0 && opp_hands[j].rank < th.rank) {
                const auto& oh = opp_hands[j];
                float r = opp_reach_vec[oh.hand_index];
                losssum += r;
                card_losssum[oh.card1] += r;
                card_losssum[oh.card2] += r;
                j--;
            }
            // losssum includes all opp hands strictly stronger than trav
            hand_values[th.hand_index] -= (losssum
                                           - card_losssum[th.card1]
                                           - card_losssum[th.card2]
                                          ) * half_pot;
        }
    }
}

// ============================================================================
// Payoff computations — optimized O(M+N) algorithms
// ============================================================================

void CFRSolver::compute_showdown_payoffs(
    int traversing_player,
    double pot,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards) const
{
    // For any 5-card board (river), use O(M+N) sorted sweep
    int board_count = mask_count(dead_cards & FULL_DECK_MASK);
    if (board_count >= 5) {
        compute_showdown_sorted(traversing_player, pot, oop_reach, ip_reach,
                                hand_values, dead_cards);
        return;
    }

    // Fallback for non-river boards (shouldn't normally happen — showdowns are at river)
    const auto& trav_range = get_range(traversing_player);
    const auto& opp_range = get_range(1 - traversing_player);
    const auto& opp_reach = (traversing_player == 0) ? ip_reach : oop_reach;
    int num_trav = static_cast<int>(trav_range.size());
    int num_opp = static_cast<int>(opp_range.size());

    hand_values.assign(num_trav, 0.0f);
    float half_pot = static_cast<float>(pot / 2.0);
    const auto& eval = get_evaluator();

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
            if (cmp > 0) ev += opp_r * half_pot;
            else if (cmp < 0) ev -= opp_r * half_pot;
        }
        hand_values[t] = ev;
    }
}

// ============================================================================
// O(M+N) fold payoff using card-exclusion sum
// ============================================================================

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
    float half_pot = static_cast<float>(pot / 2.0);
    float sign = (folder == traversing_player) ? -1.0f : 1.0f;
    float payoff_per_reach = sign * half_pot;

    // Precompute: total opponent reach sum, and per-card reach sum
    float opp_sum = 0.0f;
    float opp_card_sum[52] = {};

    for (int o = 0; o < num_opp; ++o) {
        const Hand& opp_hand = opp_range[o];
        if (opp_hand.conflicts_with(dead_cards)) continue;
        float r = opp_reach[o];
        if (r <= 0) continue;
        opp_sum += r;
        opp_card_sum[opp_hand.cards[0]] += r;
        opp_card_sum[opp_hand.cards[1]] += r;
    }

    // For each traverser hand: payoff = payoff_per_reach * (total_opp - conflicts)
    // Conflicts = hands sharing a card = card_sum[c1] + card_sum[c2] - shared_exact
    // We need to find if any opp hand has BOTH cards matching (impossible for 2-card hands with same deck)
    // but we need to subtract the double-counted hand that has both c1 and c2
    for (int t = 0; t < num_trav; ++t) {
        const Hand& trav_hand = trav_range[t];
        if (trav_hand.conflicts_with(dead_cards)) continue;

        int c1 = trav_hand.cards[0];
        int c2 = trav_hand.cards[1];

        // effective_opp = opp_sum - card_sum[c1] - card_sum[c2]
        // (No double-counting correction needed: no opp hand can have both c1 AND c2,
        //  since that would be the same hand as the traverser.)
        float effective_opp = opp_sum - opp_card_sum[c1] - opp_card_sum[c2];
        hand_values[t] = payoff_per_reach * effective_opp;
    }
}

// ============================================================================
// Query / export
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
    
    // Use canonical index for strategy lookup
    int ci = get_canonical_index(player, hand_idx);
    storage_.get_average_strategy(node_index, ci, num_actions, strategy.data());
}

// ============================================================================
// Best Response / Exploitability computation
//
// For each player, compute the value of playing the BEST possible strategy
// against the opponent's average (converged) strategy. If the converged
// strategy is a perfect Nash equilibrium, the best response value equals
// the game value, and exploitability is zero.
//
// Exploitability = (BR_value_player0 + BR_value_player1) / pot
// ============================================================================

void CFRSolver::best_response_traverse(
    const GameTreeNode* node,
    int br_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards) const
{
    if (!node) return;

    // Terminal / fold nodes: compute payoffs as usual
    if (node->is_terminal() || node->type == NodeType::FOLD) {
        if (node->type == NodeType::FOLD) {
            compute_fold_payoffs(br_player, node->player, node->pot,
                                 oop_reach, ip_reach, hand_values, dead_cards);
        } else {
            compute_showdown_payoffs(br_player, node->pot,
                                     oop_reach, ip_reach, hand_values, dead_cards);
        }
        return;
    }

    // Chance node
    if (node->is_chance()) {
        best_response_chance(node, br_player, oop_reach, ip_reach,
                             hand_values, dead_cards);
        return;
    }

    // Player node
    int acting_player = node->player;
    const auto& acting_range = get_range(acting_player);
    const auto& trav_range = get_range(br_player);
    int num_actions = node->num_actions();
    int num_acting_hands = static_cast<int>(acting_range.size());
    int num_trav_hands = static_cast<int>(trav_range.size());

    // Get average strategy for the acting player (the converged strategy)
    std::vector<std::vector<float>> strategies(num_acting_hands, std::vector<float>(num_actions));
    for (int h = 0; h < num_acting_hands; ++h) {
        int ci = get_canonical_index(acting_player, h);
        storage_.get_average_strategy(node->node_index, ci, num_actions, strategies[h].data());
    }

    if (acting_player == br_player) {
        // BR player's node: pick the action that maximizes value FOR EACH HAND
        std::vector<std::vector<float>> action_values(num_actions,
            std::vector<float>(num_trav_hands, 0.0f));

        for (int a = 0; a < num_actions; ++a) {
            std::vector<float> new_oop_reach, new_ip_reach;

            if (br_player == 0) {
                // BR player is OOP: for BR, reach = 1 for all hands (we try each)
                // But we need to pass through the reach for the subtree correctly
                // For BR player, we don't scale reach by strategy (we try each action independently)
                new_oop_reach = oop_reach; // BR player reach stays as-is
                new_ip_reach = ip_reach;
            } else {
                new_oop_reach = oop_reach;
                new_ip_reach = ip_reach; // BR player reach stays as-is
            }

            best_response_traverse(node->children[a].get(), br_player,
                                   new_oop_reach, new_ip_reach, action_values[a],
                                   dead_cards);
        }

        // For each hand, pick the action with the highest value
        hand_values.assign(num_trav_hands, 0.0f);
        for (int h = 0; h < num_trav_hands; ++h) {
            float best_val = action_values[0][h];
            for (int a = 1; a < num_actions; ++a) {
                best_val = std::max(best_val, action_values[a][h]);
            }
            hand_values[h] = best_val;
        }

    } else {
        // Opponent's node: follow opponent's average strategy
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

            best_response_traverse(node->children[a].get(), br_player,
                                   new_oop_reach, new_ip_reach, action_values[a],
                                   dead_cards);
        }

        // Sum action values (opponent's strategy is incorporated via reach)
        hand_values.assign(num_trav_hands, 0.0f);
        for (int a = 0; a < num_actions; ++a) {
            for (int h = 0; h < num_trav_hands; ++h) {
                hand_values[h] += action_values[a][h];
            }
        }
    }
}

void CFRSolver::best_response_chance(
    const GameTreeNode* node,
    int br_player,
    const std::vector<float>& oop_reach,
    const std::vector<float>& ip_reach,
    std::vector<float>& hand_values,
    CardMask dead_cards) const
{
    const auto& trav_range = get_range(br_player);
    int num_trav_hands = static_cast<int>(trav_range.size());
    int num_oop = static_cast<int>(oop_range_.size());
    int num_ip = static_cast<int>(ip_range_.size());
    hand_values.assign(num_trav_hands, 0.0f);

    // Pre-allocate buffers once, reuse for each deal
    std::vector<float> new_oop_reach(num_oop);
    std::vector<float> new_ip_reach(num_ip);
    std::vector<float> card_values(num_trav_hands);
    int num_deals = 0;

    for (int card = 0; card < NUM_CARDS; ++card) {
        if (mask_has_card(dead_cards, card)) continue;

        CardMask new_dead = mask_add_card(dead_cards, card);

        for (int h = 0; h < num_oop; ++h) {
            new_oop_reach[h] = mask_has_card(oop_range_[h].mask(), card) ? 0.0f : oop_reach[h];
        }
        for (int h = 0; h < num_ip; ++h) {
            new_ip_reach[h] = mask_has_card(ip_range_[h].mask(), card) ? 0.0f : ip_reach[h];
        }

        std::fill(card_values.begin(), card_values.end(), 0.0f);
        best_response_traverse(node->children[0].get(), br_player,
                               new_oop_reach, new_ip_reach, card_values, new_dead);

        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] += card_values[h];
        }
        num_deals++;
    }

    if (num_deals > 0) {
        float inv = 1.0f / num_deals;
        for (int h = 0; h < num_trav_hands; ++h) {
            hand_values[h] *= inv;
        }
    }
}

double CFRSolver::compute_exploitability() const {
    int num_oop = static_cast<int>(oop_range_.size());
    int num_ip = static_cast<int>(ip_range_.size());

    double ev_sum = 0.0;

    for (int br_player = 0; br_player < 2; ++br_player) {
        const auto& trav_range = get_range(br_player);
        const auto& opp_range = get_range(1 - br_player);
        int num_trav = static_cast<int>(trav_range.size());
        int num_opp = static_cast<int>(opp_range.size());

        std::vector<float> oop_reach(num_oop);
        for (int i = 0; i < num_oop; ++i) oop_reach[i] = oop_range_[i].weight;
        
        std::vector<float> ip_reach(num_ip);
        for (int i = 0; i < num_ip; ++i) ip_reach[i] = ip_range_[i].weight;
        
        std::vector<float> hand_values(num_trav, 0.0f);

        best_response_traverse(root_.get(), br_player, oop_reach, ip_reach,
                               hand_values, game_params_.board.mask);

        double total_ev = 0.0;
        int valid_hands = 0;
        for (int h = 0; h < num_trav; ++h) {
            if (trav_range[h].conflicts_with(game_params_.board.mask)) continue;
            
            int opp_count = 0;
            for (int o = 0; o < num_opp; ++o) {
                if (opp_range[o].conflicts_with(game_params_.board.mask)) continue;
                if (trav_range[h].mask() & opp_range[o].mask()) continue;
                opp_count++;
            }
            
            if (opp_count > 0) {
                total_ev += hand_values[h] / opp_count;
            }
            valid_hands++;
        }

        if (valid_hands > 0) {
            ev_sum += total_ev / valid_hands;
        }
    }

    return ev_sum / game_params_.initial_pot * 100.0;
}

std::string CFRSolver::export_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"solver\": \"PokerSolver DCFR\",\n";
    json << "  \"iterations\": " << config_.num_iterations << ",\n";
    json << "  \"threads\": " << config_.num_threads << ",\n";
    json << "  \"isomorphism\": " << (isomorphism_enabled_ ? "true" : "false") << ",\n";
    json << "  \"pot\": " << game_params_.initial_pot << ",\n";
    json << "  \"stack\": " << game_params_.effective_stack << ",\n";
    json << "  \"board\": \"";
    for (int i = 0; i < game_params_.board.count; ++i) {
        json << card_to_string(game_params_.board.cards[i]);
    }
    json << "\",\n";

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
            
            int ci = get_canonical_index(player, h);
            storage_.get_average_strategy(root->node_index, ci, num_actions, strategy.data());
            
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
