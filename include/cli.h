#pragma once
#include "cards.h"
#include "game_tree.h"
#include "cfr_engine.h"
#include <string>
#include <vector>

namespace poker {

// ============================================================================
// Command-Line Interface
// ============================================================================
class CLI {
public:
    CLI();

    // Parse command-line arguments
    bool parse_args(int argc, char* argv[]);

    // Run interactive mode
    void run_interactive();

    // Run with parsed arguments
    void run();

    // Print usage
    static void print_usage();

private:
    GameParams game_params_;
    CFRSolver::Config solver_config_;
    
    std::string oop_range_str_;
    std::string ip_range_str_;
    std::string board_str_;
    std::string output_file_;
    
    bool interactive_mode_ = false;

    // Parse board string (e.g., "AhKdQc" or "Ah Kd Qc")
    bool parse_board(const std::string& board_str, Board& board);
    
    // Parse bet sizing string (e.g., "33,67,100" for 33%, 67%, 100% pot)
    std::vector<double> parse_bet_sizes(const std::string& sizes_str);

    // Interactive commands
    void handle_command(const std::string& line);
    void cmd_set_board(const std::string& args);
    void cmd_set_pot(const std::string& args);
    void cmd_set_stack(const std::string& args);
    void cmd_set_range(int player, const std::string& args);
    void cmd_set_bet_sizes(Street street, const std::string& args);
    void cmd_set_raise_sizes(Street street, const std::string& args);
    void cmd_set_sizing(const std::string& args); // Unified: set_bet_sizes <player>,<street>,<type>,<sizes>
    void cmd_set_iterations(const std::string& args);
    void cmd_set_threads(const std::string& args);
    void cmd_solve();
    void cmd_show_strategy(const std::string& args);
    void cmd_export(const std::string& args);
    void cmd_help();
};

} // namespace poker
