#include "game_tree.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace poker {

std::string Action::to_string() const {
    switch (type) {
        case ActionType::FOLD:  return "FOLD";
        case ActionType::CHECK: return "CHECK";
        case ActionType::CALL:  return "CALL";
        case ActionType::ALLIN: return "ALLIN";
        case ActionType::BET: {
            std::ostringstream oss;
            oss << "BET " << amount;
            return oss.str();
        }
        case ActionType::RAISE: {
            std::ostringstream oss;
            oss << "RAISE " << amount;
            return oss.str();
        }
    }
    return "???";
}

GameTreeBuilder::GameTreeBuilder(const GameParams& params)
    : params_(params) {}

std::unique_ptr<GameTreeNode> GameTreeBuilder::build() {
    player_node_count_ = 0;
    total_node_count_ = 0;

    double bets[2] = {0, 0};
    Street start_street;

    if (params_.board.count >= 5) {
        start_street = Street::RIVER;
    } else if (params_.board.count >= 4) {
        start_street = Street::TURN;
    } else {
        start_street = Street::FLOP;
    }

    // OOP (player 0) acts first, not a donk on first street
    return build_action_node(0, start_street, params_.initial_pot, 
                              params_.effective_stack, bets, 0, true, false);
}

std::unique_ptr<GameTreeNode> GameTreeBuilder::make_terminal(
    double pot, bool is_fold, int folder) 
{
    auto node = std::make_unique<GameTreeNode>();
    total_node_count_++;
    
    if (is_fold) {
        node->type = NodeType::FOLD;
        node->pot = pot;
        node->payoff_multiplier = 1.0;
        node->player = static_cast<uint8_t>(folder);
    } else {
        node->type = NodeType::TERMINAL;
        node->pot = pot;
        node->payoff_multiplier = 1.0;
    }
    return node;
}

std::unique_ptr<GameTreeNode> GameTreeBuilder::build_chance_node(
    Street next_street, double pot, double stack, bool is_donk)
{
    auto node = std::make_unique<GameTreeNode>();
    node->type = NodeType::CHANCE;
    node->street = next_street;
    node->pot = pot;
    node->stack = stack;
    total_node_count_++;

    double bets[2] = {0, 0};
    // OOP (player 0) always acts first on new street
    auto child = build_action_node(0, next_street, pot, stack, bets, 0, true, is_donk);
    node->children.push_back(std::move(child));

    return node;
}

std::vector<Action> GameTreeBuilder::generate_actions(
    int acting_player, Street street, double pot, double stack,
    const double bets[2], int num_raises, bool can_check, bool is_donk)
{
    std::vector<Action> actions;
    
    double to_call = bets[1 - acting_player] - bets[acting_player];
    // remaining chips for the acting player
    double remaining = stack - bets[acting_player];

    // Safety: if remaining is effectively zero, no actions possible
    if (remaining < 0.01) {
        return actions; // all-in already, should be terminal
    }

    // Get this player's config for this street
    const auto& config = params_.get_bet_config(street, acting_player);

    if (to_call > 0.001) {
        // Facing a bet/raise
        actions.push_back({ActionType::FOLD, 0});
        
        if (to_call >= remaining - 0.01) {
            // Can only call all-in
            actions.push_back({ActionType::CALL, remaining});
        } else {
            actions.push_back({ActionType::CALL, to_call});
            
            if (num_raises < params_.max_raises_per_street) {
                double pot_after_call = pot + to_call;
                
                for (double frac : config.raise_sizes) {
                    double raise_total = to_call + pot_after_call * frac;
                    raise_total = std::round(raise_total * 100) / 100.0;
                    
                    // Minimum raise is 2x the previous bet
                    double prev_bet = bets[1 - acting_player];
                    double min_raise = to_call + prev_bet;
                    if (raise_total < min_raise) raise_total = min_raise;
                    
                    if (raise_total < remaining - 0.01) {
                        // All-in threshold: if remaining after raise is small, promote to all-in
                        double left_after = remaining - raise_total;
                        double pot_after = pot + raise_total;
                        if (params_.allin_threshold > 0.0 && left_after < params_.allin_threshold * pot_after) {
                            // Promote to all-in (will be added below)
                        } else {
                            actions.push_back({ActionType::RAISE, raise_total});
                        }
                    }
                }
                
                // All-in option
                if (config.add_allin && remaining > to_call + 0.01) {
                    bool allin_covered = false;
                    for (const auto& a : actions) {
                        if (std::abs(a.amount - remaining) < 0.01) {
                            allin_covered = true;
                            break;
                        }
                    }
                    if (!allin_covered) {
                        actions.push_back({ActionType::ALLIN, remaining});
                    }
                }
            }
        }
    } else {
        // No bet to call — opening action
        if (can_check) {
            actions.push_back({ActionType::CHECK, 0});
        }
        
        // Choose bet sizes: use donk_sizes if this is a donk situation and they are configured
        const auto& open_sizes = (is_donk && !config.donk_sizes.empty()) 
                                  ? config.donk_sizes 
                                  : config.bet_sizes;

        for (double frac : open_sizes) {
            double bet_amount = pot * frac;
            bet_amount = std::round(bet_amount * 100) / 100.0;
            
            if (bet_amount > 0.01 && bet_amount < remaining - 0.01) {
                // All-in threshold: if remaining after bet is small, promote to all-in
                double left_after = remaining - bet_amount;
                double pot_after = pot + bet_amount;
                if (params_.allin_threshold > 0.0 && left_after < params_.allin_threshold * pot_after) {
                    // Promote to all-in (will be added below)
                } else {
                    actions.push_back({ActionType::BET, bet_amount});
                }
            }
        }
        
        // All-in
        if (config.add_allin && remaining > 0.01) {
            bool allin_covered = false;
            for (const auto& a : actions) {
                if (std::abs(a.amount - remaining) < 0.01) {
                    allin_covered = true;
                    break;
                }
            }
            if (!allin_covered) {
                actions.push_back({ActionType::ALLIN, remaining});
            }
        }
    }

    return actions;
}

std::unique_ptr<GameTreeNode> GameTreeBuilder::build_action_node(
    int acting_player, Street street, double pot, double stack,
    double bets[2], int num_raises, bool can_check, bool is_donk)
{
    // Check if this player is already all-in
    double remaining = stack - bets[acting_player];
    if (remaining < 0.01) {
        // Player is all-in, this should be a terminal/showdown
        return make_terminal(pot, false, -1);
    }

    auto node = std::make_unique<GameTreeNode>();
    node->type = NodeType::PLAYER;
    node->player = static_cast<uint8_t>(acting_player);
    node->street = street;
    node->pot = pot;
    node->stack = stack;
    node->player_bets[0] = bets[0];
    node->player_bets[1] = bets[1];
    node->node_index = player_node_count_++;
    total_node_count_++;

    // is_donk only applies to the first player opening on a new street
    node->actions = generate_actions(acting_player, street, pot, stack, bets, num_raises, can_check, is_donk);

    // If no actions are possible (shouldn't happen normally), make terminal
    if (node->actions.empty()) {
        node->type = NodeType::TERMINAL;
        node->pot = pot;
        return node;
    }

    int other_player = 1 - acting_player;

    for (const auto& action : node->actions) {
        double new_bets[2] = {bets[0], bets[1]};

        switch (action.type) {
            case ActionType::FOLD: {
                node->children.push_back(make_terminal(pot, true, acting_player));
                break;
            }
            case ActionType::CHECK: {
                if (acting_player == 1) {
                    // IP checks after OOP check -> both checked, advance street
                    if (street == Street::RIVER) {
                        node->children.push_back(make_terminal(pot, false, -1));
                    } else {
                        Street next = static_cast<Street>(static_cast<int>(street) + 1);
                        // Both checked: no aggressor, not a donk
                        node->children.push_back(build_chance_node(next, pot, stack, false));
                    }
                } else {
                    // OOP checks, IP acts (not a donk for subsequent actions)
                    node->children.push_back(
                        build_action_node(other_player, street, pot, stack, new_bets, num_raises, true, false));
                }
                break;
            }
            case ActionType::CALL: {
                double call_amount = action.amount;
                new_bets[acting_player] += call_amount;
                double new_pot = pot + call_amount;

                // Check if either player is all-in after call
                bool someone_allin = (new_bets[0] >= stack - 0.01) || (new_bets[1] >= stack - 0.01);

                if (someone_allin) {
                    node->children.push_back(make_terminal(new_pot, false, -1));
                } else if (street == Street::RIVER) {
                    node->children.push_back(make_terminal(new_pot, false, -1));
                } else {
                    Street next = static_cast<Street>(static_cast<int>(street) + 1);
                    // Donk detection: if OOP (player 0) called, IP was the aggressor.
                    // On the next street, OOP acts first — that's a donk bet situation.
                    bool donk_next = (acting_player == 0);
                    node->children.push_back(build_chance_node(next, new_pot, stack, donk_next));
                }
                break;
            }
            case ActionType::BET:
            case ActionType::RAISE: {
                new_bets[acting_player] += action.amount;
                double new_pot = pot + action.amount;

                // After a bet/raise, opponent responds — no donk for the response
                node->children.push_back(
                    build_action_node(other_player, street, new_pot, stack,
                                      new_bets, num_raises + 1, false, false));
                break;
            }
            case ActionType::ALLIN: {
                double allin_amount = stack - bets[acting_player];
                new_bets[acting_player] = stack;
                double new_pot = pot + allin_amount;

                // Opponent faces a bet, they can fold or call
                node->children.push_back(
                    build_action_node(other_player, street, new_pot, stack,
                                      new_bets, num_raises + 1, false, false));
                break;
            }
        }
    }

    return node;
}

} // namespace poker
