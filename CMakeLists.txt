# CMakeLists.txt
#
# anyeditqt -- a code editor built on ace's grammar corpus, in the shape of
# anytermqt: a Qt-free C++ core, a Qt widget over it, PySide6 bindings over
# that. No Go.
#
#   core/      aced::core     model + tokenizer. Builds and tests today.
#   widget/    acedqt::core   Qt view. Headers only so far, no target.
#   bindings/  anyeditqt      PySide6 via shiboken. Needs the widget first.
#   grammars/  the corpus and the exporter that produces it from an ace checkout.

cmake_minimum_required(VERSION 3.21)
project(anyeditqt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(BUILD_WIDGET   "Build the Qt editor widget and the anyedit app" ON)
option(BUILD_PROBES   "Build the offscreen Qt render probe" OFF)
option(BUILD_BINDINGS "Build the Python bindings"  OFF)

# enable_testing() has to be called at the top level, not only in core/, or
# ctest finds nothing when run against the top build directory.
enable_testing()

add_subdirectory(core)

if(BUILD_PROBES)
    add_subdirectory(tools)
endif()

if(BUILD_WIDGET)
    add_subdirectory(widget)
    add_subdirectory(app)
endif()

if(BUILD_BINDINGS)
    message(FATAL_ERROR "bindings/ depends on the widget; see bindings/README.md.")
endif()
