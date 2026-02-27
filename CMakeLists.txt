cmake_minimum_required(VERSION 3.16)
project(PokerSolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance flags
if(MSVC)
    add_compile_options(/O2 /arch:AVX2 /fp:fast /GL /Oi)
    add_link_options(/LTCG)
else()
    add_compile_options(-O3 -march=native -flto -ffast-math)
    add_link_options(-flto)
endif()

# Find threads
find_package(Threads REQUIRED)

# Source files
set(SOLVER_SOURCES
    src/main.cpp
    src/cards.cpp
    src/hand_eval.cpp
    src/game_tree.cpp
    src/cfr_engine.cpp
    src/range_parser.cpp
    src/cli.cpp
)

# Header files
set(SOLVER_HEADERS
    include/cards.h
    include/hand_eval.h
    include/game_tree.h
    include/cfr_engine.h
    include/range_parser.h
    include/cli.h
)

# Main executable
add_executable(poker_solver ${SOLVER_SOURCES} ${SOLVER_HEADERS})
target_include_directories(poker_solver PRIVATE include)
target_link_libraries(poker_solver PRIVATE Threads::Threads)

# Definitions
target_compile_definitions(poker_solver PRIVATE
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<CONFIG:Release>:NDEBUG>
)
