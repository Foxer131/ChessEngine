# =============================================================================
# GUI: Qt desktop front-end.
#
# The GUI is a SEPARATE process from the engine. It launches the `engine`
# executable and communicates via the UCI protocol over stdin/stdout
# (Qt's QProcess is the natural fit). This keeps engine and GUI decoupled and
# lets the same engine plug into external GUIs (Cutechess, Arena) for testing.
# =============================================================================

# Qt6 is preferred; fall back to Qt5 if that's what's installed.
find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets QUIET)
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

# add_executable(chess_gui src/main.cpp)
# target_link_libraries(chess_gui PRIVATE Qt::Widgets)
