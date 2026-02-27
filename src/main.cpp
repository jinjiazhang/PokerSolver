#include "cli.h"
#include <iostream>

int main(int argc, char* argv[]) {
    poker::CLI cli;

    if (argc <= 1) {
        // No arguments: default to interactive mode
        cli.run_interactive();
        return 0;
    }

    if (!cli.parse_args(argc, argv)) {
        return 1;
    }

    cli.run();
    return 0;
}
