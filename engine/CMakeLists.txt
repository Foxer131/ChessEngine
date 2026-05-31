# =============================================================================
# Engine: a static library (chess_core) + the UCI executable (engine).
#
# Splitting the engine into a library means tests, benchmarks, and the UCI
# front-end all link the *same* code. The GUI does NOT link this; it launches
# the engine executable and talks to it over the UCI protocol (stdin/stdout).
#
# Sources are discovered with CONFIGURE_DEPENDS so that simply dropping a new
# .cpp into src/ and rebuilding picks it up (CMake re-globs at build time).
# The targets below only materialize once sources actually exist, so the project
# configures cleanly while the engine is still empty.
# =============================================================================

file(GLOB_RECURSE CHESS_CORE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/movegen/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/search/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/eval/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/book/*.cpp")

if(CHESS_CORE_SOURCES)
    add_library(chess_core STATIC ${CHESS_CORE_SOURCES})
    target_include_directories(chess_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(chess_core PUBLIC cxx_std_20)

    # ---- UCI executable ------------------------------------------------------
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/uci/main.cpp")
        find_package(Threads REQUIRED)              # search runs on a worker thread
        add_executable(engine src/uci/main.cpp)
        target_link_libraries(engine PRIVATE chess_core Threads::Threads)
    endif()

    # ---- Benchmarks / perft --------------------------------------------------
    if(CHESS_BUILD_BENCH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_main.cpp")
        add_executable(bench bench/bench_main.cpp)
        target_link_libraries(bench PRIVATE chess_core)
    endif()
else()
    message(STATUS "chess_core: no sources yet - engine targets skipped. "
                   "Add .cpp files under engine/src/ and reconfigure.")
endif()
