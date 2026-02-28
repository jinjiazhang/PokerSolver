#include "cli.h"
#include "range_parser.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <memory>

namespace poker {

CLI::CLI() {
    oop_range_str_ = "random";
    ip_range_str_ = "random";
}

void CLI::print_usage() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════╗
║               PokerSolver - GTO Calculator                  ║
║           Discounted CFR (DCFR) Algorithm Engine            ║
╚══════════════════════════════════════════════════════════════╝

Usage: poker_solver [options]

Options:
  --board <cards>       Board cards (e.g., "AhKdQc", "AhKdQcTs", "AhKdQcTsJh")
  --pot <amount>        Initial pot size (default: 10)
  --stack <amount>      Effective stack size (default: 100)
  --oop-range <range>   OOP player range (e.g., "AA,KK,QQ,AKs")
  --ip-range <range>    IP player range (e.g., "TT+,ATs+,KQs")
  --iterations <n>      Number of CFR iterations (default: 200)
  --threads <n>         Number of threads (default: 1)
  --bet-sizes <sizes>   Bet sizes as % of pot (e.g., "33,67,100")
  --raise-sizes <sizes> Raise sizes as % of pot (e.g., "50,100")
  --allin-threshold <f>  All-in threshold (default: 0.67). If a bet leaves less
                         than f * pot remaining, promote to all-in. Set 0 to disable.
  --accuracy <pct>       Target exploitability (% of pot). Solver halts early when
                         reached. (default: 0.5). Set 0 to disable.
  --output <file>       Output file for JSON results
  --interactive         Start in interactive mode
  --help                Show this help

Range notation:
  AA        All pocket aces combos
  AKs       Ace-King suited
  AKo       Ace-King offsuit
  AK        All Ace-King combos
  TT+       Tens and above
  ATs+      Ace-Ten suited and above
  22-55     Pairs from twos to fives
  AhKh      Specific combo
  random    All possible hands

Examples:
  poker_solver --board AhKdQc --pot 100 --stack 200 --oop-range "AA,KK,QQ,AKs" --ip-range "TT+,AQs+" --iterations 500
  poker_solver --interactive
)" << std::endl;
}

bool CLI::parse_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return false;
        } else if (arg == "--interactive" || arg == "-i") {
            interactive_mode_ = true;
        } else if (arg == "--board" && i + 1 < argc) {
            board_str_ = argv[++i];
        } else if (arg == "--pot" && i + 1 < argc) {
            game_params_.initial_pot = std::stod(argv[++i]);
        } else if (arg == "--stack" && i + 1 < argc) {
            game_params_.effective_stack = std::stod(argv[++i]);
        } else if (arg == "--oop-range" && i + 1 < argc) {
            oop_range_str_ = argv[++i];
        } else if (arg == "--ip-range" && i + 1 < argc) {
            ip_range_str_ = argv[++i];
        } else if (arg == "--iterations" && i + 1 < argc) {
            solver_config_.num_iterations = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            solver_config_.num_threads = std::stoi(argv[++i]);
        } else if (arg == "--bet-sizes" && i + 1 < argc) {
            auto sizes = parse_bet_sizes(argv[++i]);
            game_params_.flop_bet_config.bet_sizes = sizes;
            game_params_.turn_bet_config.bet_sizes = sizes;
            game_params_.river_bet_config.bet_sizes = sizes;
        } else if (arg == "--raise-sizes" && i + 1 < argc) {
            auto sizes = parse_bet_sizes(argv[++i]);
            game_params_.flop_bet_config.raise_sizes = sizes;
            game_params_.turn_bet_config.raise_sizes = sizes;
            game_params_.river_bet_config.raise_sizes = sizes;
        } else if (arg == "--output" && i + 1 < argc) {
            output_file_ = argv[++i];
        } else if ((arg == "--allin-threshold" || arg == "--allin_threshold") && i + 1 < argc) {
            game_params_.allin_threshold = std::stod(argv[++i]);
        } else if (arg == "--accuracy" && i + 1 < argc) {
            solver_config_.target_exploitability = std::stod(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage();
            return false;
        }
    }

    return true;
}

bool CLI::parse_board(const std::string& board_str, Board& board) {
    board = Board();
    
    // Remove spaces
    std::string clean;
    for (char c : board_str) {
        if (!std::isspace(c)) clean += c;
    }

    if (clean.size() % 2 != 0) {
        std::cerr << "Error: invalid board string '" << board_str << "'\n";
        return false;
    }

    for (size_t i = 0; i < clean.size(); i += 2) {
        int card = string_to_card(clean.substr(i, 2));
        if (card < 0) {
            std::cerr << "Error: invalid card '" << clean.substr(i, 2) << "'\n";
            return false;
        }
        if (board.contains(card)) {
            std::cerr << "Error: duplicate card '" << clean.substr(i, 2) << "'\n";
            return false;
        }
        board.add_card(card);
    }

    if (board.count < 3 || board.count > 5) {
        std::cerr << "Error: board must have 3-5 cards, got " << board.count << "\n";
        return false;
    }

    return true;
}

std::vector<double> CLI::parse_bet_sizes(const std::string& sizes_str) {
    std::vector<double> sizes;
    std::istringstream iss(sizes_str);
    std::string token;
    while (std::getline(iss, token, ',')) {
        double val = std::stod(token) / 100.0; // Convert percent to fraction
        sizes.push_back(val);
    }
    return sizes;
}

void CLI::run() {
    if (interactive_mode_) {
        run_interactive();
        return;
    }

    // Validate inputs
    if (board_str_.empty()) {
        std::cerr << "Error: board is required. Use --board <cards>\n";
        print_usage();
        return;
    }

    if (!parse_board(board_str_, game_params_.board)) {
        return;
    }

    // Parse ranges
    auto oop_range = RangeParser::parse(oop_range_str_, game_params_.board.mask);
    auto ip_range = RangeParser::parse(ip_range_str_, game_params_.board.mask);

    if (oop_range.empty()) {
        std::cerr << "Error: OOP range is empty\n";
        return;
    }
    if (ip_range.empty()) {
        std::cerr << "Error: IP range is empty\n";
        return;
    }

    std::cout << "\n=== Game Configuration ===" << std::endl;
    std::cout << "Board: " << board_str_ << std::endl;
    std::cout << "Pot:   " << game_params_.initial_pot << std::endl;
    std::cout << "Stack: " << game_params_.effective_stack << std::endl;
    std::cout << "All-in threshold: " << game_params_.allin_threshold << std::endl;
    std::cout << "Accuracy target:  " << solver_config_.target_exploitability << "%" << std::endl;
    std::cout << "OOP range: " << oop_range.size() << " combos" << std::endl;
    std::cout << "IP range:  " << ip_range.size() << " combos" << std::endl;
    std::cout << "Iterations: " << solver_config_.num_iterations << std::endl;
    std::cout << std::endl;

    // Create solver
    CFRSolver solver(game_params_, solver_config_);
    solver.set_oop_range(oop_range);
    solver.set_ip_range(ip_range);
    solver.build();
    solver.solve();

    // Export results
    if (!output_file_.empty()) {
        std::ofstream out(output_file_);
        if (out.is_open()) {
            out << solver.export_json();
            std::cout << "\nResults exported to: " << output_file_ << std::endl;
        } else {
            std::cerr << "Error: could not open output file: " << output_file_ << std::endl;
        }
    } else {
        // Print summary to stdout
        std::cout << "\n=== Strategy Summary (Root Node) ===" << std::endl;
        
        const auto* root = solver.get_root();
        if (root && root->is_player()) {
            int player = root->player;
            const auto& range = (player == 0) ? oop_range : ip_range;
            
            std::cout << "Player: " << (player == 0 ? "OOP" : "IP") << std::endl;
            std::cout << "Actions: ";
            for (int a = 0; a < root->num_actions(); ++a) {
                if (a > 0) std::cout << " | ";
                std::cout << root->actions[a].to_string();
            }
            std::cout << std::endl << std::endl;

            // Print strategy for each hand in range
            std::cout << std::left << std::setw(8) << "Hand";
            for (int a = 0; a < root->num_actions(); ++a) {
                std::cout << std::setw(12) << root->actions[a].to_string();
            }
            std::cout << std::endl;
            std::cout << std::string(8 + 12 * root->num_actions(), '-') << std::endl;

            std::vector<float> strategy;
            int shown = 0;
            for (const auto& hand : range) {
                if (hand.conflicts_with(game_params_.board.mask)) continue;
                
                solver.get_strategy(root->node_index, hand, player, strategy);
                if (strategy.empty()) continue;

                std::cout << std::left << std::setw(8) << hand.to_string();
                for (int a = 0; a < root->num_actions(); ++a) {
                    std::ostringstream val;
                    val << std::fixed << std::setprecision(1) << (strategy[a] * 100.0f) << "%";
                    std::cout << std::left << std::setw(12) << val.str();
                }
                std::cout << std::endl;
                
                if (++shown >= 30) {
                    std::cout << "... (" << range.size() << " total combos)\n";
                    break;
                }
            }
        }
    }
}

void CLI::run_interactive() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════╗
║           PokerSolver - Interactive Mode                     ║
╚══════════════════════════════════════════════════════════════╝
Type 'help' for available commands.
)" << std::endl;

    std::string line;
    while (true) {
        std::cout << "solver> ";
        if (!std::getline(std::cin, line)) break;
        
        // Trim
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) continue;
        if (line == "quit" || line == "exit") break;
        
        handle_command(line);
    }
}

void CLI::handle_command(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    
    std::string args;
    if (iss.peek() == ' ') iss.get();
    std::getline(iss, args);

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "help") {
        cmd_help();
    } else if (cmd == "board" || cmd == "set_board") {
        cmd_set_board(args);
    } else if (cmd == "pot" || cmd == "set_pot") {
        cmd_set_pot(args);
    } else if (cmd == "stack" || cmd == "set_stack" || cmd == "effective_stack") {
        cmd_set_stack(args);
    } else if (cmd == "oop_range" || cmd == "set_oop_range") {
        cmd_set_range(0, args);
    } else if (cmd == "ip_range" || cmd == "set_ip_range") {
        cmd_set_range(1, args);
    } else if (cmd == "flop_bet_sizes") {
        cmd_set_bet_sizes(Street::FLOP, args);
    } else if (cmd == "turn_bet_sizes") {
        cmd_set_bet_sizes(Street::TURN, args);
    } else if (cmd == "river_bet_sizes") {
        cmd_set_bet_sizes(Street::RIVER, args);
    } else if (cmd == "flop_raise_sizes") {
        cmd_set_raise_sizes(Street::FLOP, args);
    } else if (cmd == "turn_raise_sizes") {
        cmd_set_raise_sizes(Street::TURN, args);
    } else if (cmd == "river_raise_sizes") {
        cmd_set_raise_sizes(Street::RIVER, args);
    } else if (cmd == "iterations" || cmd == "set_iterations") {
        cmd_set_iterations(args);
    } else if (cmd == "threads" || cmd == "set_threads") {
        cmd_set_threads(args);
    } else if (cmd == "solve" || cmd == "start") {
        cmd_solve();
    } else if (cmd == "allin_threshold" || cmd == "set_allin_threshold") {
        try {
            game_params_.allin_threshold = std::stod(args);
            std::cout << "All-in threshold set to: " << game_params_.allin_threshold << std::endl;
        } catch (...) {
            std::cerr << "Invalid threshold value: " << args << std::endl;
        }
    } else if (cmd == "accuracy" || cmd == "set_accuracy") {
        try {
            solver_config_.target_exploitability = std::stod(args);
            std::cout << "Target exploitability set to: " << solver_config_.target_exploitability << "%" << std::endl;
        } catch (...) {
            std::cerr << "Invalid accuracy value: " << args << std::endl;
        }
    } else if (cmd == "export") {
        cmd_export(args);
    } else {
        std::cout << "Unknown command: " << cmd << ". Type 'help' for commands.\n";
    }
}

void CLI::cmd_set_board(const std::string& args) {
    if (parse_board(args, game_params_.board)) {
        std::cout << "Board set: ";
        for (int i = 0; i < game_params_.board.count; ++i) {
            std::cout << card_to_string(game_params_.board.cards[i]);
        }
        std::cout << std::endl;
    }
}

void CLI::cmd_set_pot(const std::string& args) {
    try {
        game_params_.initial_pot = std::stod(args);
        std::cout << "Pot set to: " << game_params_.initial_pot << std::endl;
    } catch (...) {
        std::cerr << "Invalid pot value: " << args << std::endl;
    }
}

void CLI::cmd_set_stack(const std::string& args) {
    try {
        game_params_.effective_stack = std::stod(args);
        std::cout << "Stack set to: " << game_params_.effective_stack << std::endl;
    } catch (...) {
        std::cerr << "Invalid stack value: " << args << std::endl;
    }
}

void CLI::cmd_set_range(int player, const std::string& args) {
    if (player == 0) {
        oop_range_str_ = args;
        auto range = RangeParser::parse(args, game_params_.board.mask);
        std::cout << "OOP range: " << range.size() << " combos\n";
    } else {
        ip_range_str_ = args;
        auto range = RangeParser::parse(args, game_params_.board.mask);
        std::cout << "IP range: " << range.size() << " combos\n";
    }
}

void CLI::cmd_set_bet_sizes(Street street, const std::string& args) {
    auto sizes = parse_bet_sizes(args);
    auto& config = const_cast<BetConfig&>(game_params_.get_bet_config(street));
    config.bet_sizes = sizes;
    
    std::cout << "Bet sizes set to: ";
    for (double s : sizes) std::cout << (s * 100) << "% ";
    std::cout << std::endl;
}

void CLI::cmd_set_raise_sizes(Street street, const std::string& args) {
    auto sizes = parse_bet_sizes(args);
    auto& config = const_cast<BetConfig&>(game_params_.get_bet_config(street));
    config.raise_sizes = sizes;
    
    std::cout << "Raise sizes set to: ";
    for (double s : sizes) std::cout << (s * 100) << "% ";
    std::cout << std::endl;
}

void CLI::cmd_set_iterations(const std::string& args) {
    try {
        solver_config_.num_iterations = std::stoi(args);
        std::cout << "Iterations set to: " << solver_config_.num_iterations << std::endl;
    } catch (...) {
        std::cerr << "Invalid iterations value: " << args << std::endl;
    }
}

void CLI::cmd_set_threads(const std::string& args) {
    try {
        solver_config_.num_threads = std::stoi(args);
        std::cout << "Threads set to: " << solver_config_.num_threads << std::endl;
    } catch (...) {
        std::cerr << "Invalid threads value: " << args << std::endl;
    }
}

void CLI::cmd_solve() {
    if (game_params_.board.count < 3) {
        std::cerr << "Error: set board first (e.g., board AhKdQc)\n";
        return;
    }

    auto oop_range = RangeParser::parse(oop_range_str_, game_params_.board.mask);
    auto ip_range = RangeParser::parse(ip_range_str_, game_params_.board.mask);

    if (oop_range.empty() || ip_range.empty()) {
        std::cerr << "Error: ranges are empty\n";
        return;
    }

    std::cout << "\n=== Solving ===" << std::endl;
    std::cout << "Board: ";
    for (int i = 0; i < game_params_.board.count; ++i) {
        std::cout << card_to_string(game_params_.board.cards[i]);
    }
    std::cout << "\nPot: " << game_params_.initial_pot 
              << " | Stack: " << game_params_.effective_stack << std::endl;
    std::cout << "OOP: " << oop_range.size() << " combos | IP: " 
              << ip_range.size() << " combos\n\n";

    CFRSolver solver(game_params_, solver_config_);
    solver.set_oop_range(oop_range);
    solver.set_ip_range(ip_range);
    solver.build();
    solver.solve();

    // Show root strategy
    const auto* root = solver.get_root();
    if (root && root->is_player()) {
        int player = root->player;
        const auto& range = (player == 0) ? oop_range : ip_range;
        
        std::cout << "\n=== Root Strategy (" << (player == 0 ? "OOP" : "IP") << ") ===" << std::endl;
        std::cout << "Actions: ";
        for (int a = 0; a < root->num_actions(); ++a) {
            if (a > 0) std::cout << " | ";
            std::cout << root->actions[a].to_string();
        }
        std::cout << std::endl << std::endl;

        std::vector<float> strategy;
        int shown = 0;
        for (const auto& hand : range) {
            if (hand.conflicts_with(game_params_.board.mask)) continue;
            
            solver.get_strategy(root->node_index, hand, player, strategy);
            if (strategy.empty()) continue;

            std::cout << std::left << std::setw(8) << hand.to_string();
            for (float s : strategy) {
                std::ostringstream val;
                val << std::fixed << std::setprecision(1) << (s * 100.0f) << "%";
                std::cout << std::left << std::setw(10) << val.str();
            }
            std::cout << std::endl;
            
            if (++shown >= 50) {
                std::cout << "... (" << range.size() << " total combos)\n";
                break;
            }
        }
    }
}

void CLI::cmd_export(const std::string& args) {
    std::cout << "Export feature: re-run solve with --output flag or solve first.\n";
}

void CLI::cmd_help() {
    std::cout << R"(
Available commands:
  board <cards>          Set board (e.g., board AhKdQc)
  pot <amount>           Set pot size (e.g., pot 100)
  stack <amount>         Set effective stack (e.g., stack 200)
  oop_range <range>      Set OOP range (e.g., oop_range AA,KK,QQ,AKs)
  ip_range <range>       Set IP range (e.g., ip_range TT+,ATs+,KQs)
  flop_bet_sizes <pcts>  Set flop bet sizes in % (e.g., flop_bet_sizes 33,67,100)
  turn_bet_sizes <pcts>  Set turn bet sizes in %
  river_bet_sizes <pcts> Set river bet sizes in %
  flop_raise_sizes <pcts>  Set flop raise sizes in %
  turn_raise_sizes <pcts>  Set turn raise sizes in %
  river_raise_sizes <pcts> Set river raise sizes in %
  iterations <n>         Set number of iterations (e.g., iterations 500)
  threads <n>            Set number of threads
  allin_threshold <f>    Set all-in threshold (e.g., allin_threshold 0.67)
  accuracy <pct>         Set target exploitability % (e.g., accuracy 0.5)
  solve                  Start solving
  export <file>          Export results to JSON
  help                   Show this help
  quit                   Exit
)" << std::endl;
}

} // namespace poker
