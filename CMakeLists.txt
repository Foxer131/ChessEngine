cmake_minimum_required(VERSION 3.20)

project(ChessEngine
    VERSION 0.1.0
    DESCRIPTION "A classical bitboard chess engine (UCI) with a Qt GUI"
    LANGUAGES CXX)

# ---- Global C++ standard ----------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / tooling / future Claude sessions.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Put all binaries in one place so the GUI can find engine.exe easily.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Options ----------------------------------------------------------------
option(CHESS_BUILD_GUI    "Build the Qt GUI front-end"        ON)
option(CHESS_BUILD_TESTS  "Build unit tests"                  ON)
option(CHESS_BUILD_BENCH  "Build perft / benchmark tools"     ON)
option(CHESS_NATIVE_ARCH  "Optimize for the host CPU (-march=native)" ON)

# ---- Optimization / warning flags -------------------------------------------
# The engine lives or dies on codegen quality. Keep flags centralized here.
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_options($<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    if(CHESS_NATIVE_ARCH)
        add_compile_options($<$<CONFIG:Release>:-march=native>)
    endif()
endif()

# ---- Subdirectories ---------------------------------------------------------
add_subdirectory(engine)

if(CHESS_BUILD_GUI)
    add_subdirectory(gui)
endif()

if(CHESS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
