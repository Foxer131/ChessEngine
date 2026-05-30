# =============================================================================
# Tests. The single most important test for a chess engine is PERFT:
# enumerate all legal moves to a fixed depth from known positions and compare
# node counts against published reference values. This catches almost every
# move-generation bug. Add a perft suite first, before search/eval tests.
#
# Test framework (doctest or Catch2) will be fetched into third_party/ or via
# FetchContent once we start writing tests.
# =============================================================================

# Example shape (uncomment once a framework + sources exist):
#
# add_executable(unit_tests
#     perft_tests.cpp
#     movegen_tests.cpp)
# target_link_libraries(unit_tests PRIVATE chess_core)
# add_test(NAME unit_tests COMMAND unit_tests)
