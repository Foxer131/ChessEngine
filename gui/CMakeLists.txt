# =============================================================================
# GUI: Qt desktop front-end.
#
# The GUI is a SEPARATE process from the engine. It launches the `engine`
# executable and communicates via the UCI protocol over stdin/stdout
# (Qt's QProcess is the natural fit). This keeps engine and GUI decoupled and
# lets the same engine plug into external GUIs (Cutechess, Arena) for testing.
# =============================================================================

# Qt6 is preferred; fall back to Qt5 if that's what's installed.
# Svg is needed to render the SVG piece set via QSvgRenderer.
find_package(Qt6 COMPONENTS Widgets Svg QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets Svg QUIET)
endif()

if(NOT Qt6_FOUND AND NOT Qt5_FOUND)
    message(WARNING
        "Qt (Widgets) not found - skipping GUI build. "
        "Install Qt and re-run CMake, or configure with -DCHESS_BUILD_GUI=OFF.")
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(chess_gui
    src/main.cpp
    src/MainWindow.cpp
    src/BoardWidget.cpp
    src/NewGameDialog.cpp
    src/UciEngine.cpp
    src/GuiBoard.cpp
    resources/pieces/pieces.qrc)   # AUTORCC compiles the SVG piece set in

# Qt::Widgets for the UI; Qt::Svg for the piece sprites; chess_core for the
# (perft-validated) rules + legality. The engine's *thinking* runs separately.
target_link_libraries(chess_gui PRIVATE Qt::Widgets Qt::Svg chess_core)

# On Windows, mark as a GUI app (no console window) in Release.
if(WIN32)
    set_target_properties(chess_gui PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()
