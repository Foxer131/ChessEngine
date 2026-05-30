# =============================================================================
# Tests. A single no-dependency executable (core_tests) linked against
# chess_core, registered with CTest. PERFT suites will join this later as the
# move generator comes online.
#
# Sources are auto-discovered, and the target only builds once chess_core
# exists (i.e. the engine has sources).
# =============================================================================

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(TEST_SOURCES AND TARGET chess_core)
    add_executable(core_tests ${TEST_SOURCES})
    target_link_libraries(core_tests PRIVATE chess_core)
    add_test(NAME core_tests COMMAND core_tests)
else()
    message(STATUS "tests: skipped (need chess_core sources + a test .cpp).")
endif()
