# =============================================================================
# Engine: a static library (chess_core) + the UCI executable (engine).
#
# Splitting the engine into a library means tests, benchmarks, and the UCI
# front-end all link the *same* code. The GUI does NOT link this; it launches
# the engine executable and talks to it over the UCI protocol (stdin/stdout).
# =============================================================================

# ---- Core engine library ----------------------------------------------------
# NOTE: source files are added here as they are implemented. Globbing is
# intentionally avoided so CMake re-configures only when this list changes.
add_library(chess_core STATIC
    # core/      bitboards, board state, types, zobrist hashing
    # movegen/   magic bitboards, move generation, make/unmake
    # search/    alpha-beta, transposition table, ordering, quiescence
    # eval/      evaluation (hand-crafted now, NNUE later)
)

target_include_directories(chess_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(chess_core PUBLIC cxx_std_20)

# Header-only placeholder so the target is valid before any .cpp exists.
# Remove once real sources are added above.
set_target_properties(chess_core PROPERTIES LINKER_LANGUAGE CXX)

# ---- UCI executable ----------------------------------------------------------
# add_executable(engine src/uci/main.cpp)
# target_link_libraries(engine PRIVATE chess_core)

# ---- Benchmarks / perft ------------------------------------------------------
if(CHESS_BUILD_BENCH)
    # add_executable(bench bench/bench_main.cpp)
    # target_link_libraries(bench PRIVATE chess_core)
endif()
