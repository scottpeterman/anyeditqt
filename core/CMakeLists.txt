# core/CMakeLists.txt
#
# aced::core -- the editor model. No Qt, no GUI, no I/O beyond reading the
# grammar file. Everything here is testable headlessly, which is the entire
# reason the split exists.

cmake_minimum_required(VERSION 3.21)
project(aced LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(ACED_BUILD_TESTS "Build the core test suite" ON)

include(FetchContent)

# PCRE2. Vendored at a pinned tag rather than found on the system, for the same
# reason anytermqt vendors utf8proc: the three platforms have to agree, and
# Windows has no system copy to find.
#
# This is also the dependency that decides the language. The grammar corpus uses
# lookahead in 4391 regexes, which RE2 -- and therefore Go's stdlib regexp --
# cannot express at all. PCRE2 takes all but 42 of 34094.
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
FetchContent_Declare(pcre2
    GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
    GIT_TAG pcre2-10.44
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pcre2)

# nlohmann/json, header-only, used only to read the grammar corpus. Kept PRIVATE
# so nothing downstream inherits it -- see grammar.h.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)


# The headers live in include/ and NOWHERE ELSE. A copy of one left beside its
# .cpp -- which is what happens when files are moved about by hand and the two
# have the same name -- is found by AUTOMOC as well as the real one, so the
# class gets moc'd twice and the build fails with a page of "redefinition of
# qt_static_metacall" that says nothing about a duplicated file. Caught here
# instead, at configure time, in a sentence.
file(GLOB _stray_headers "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
                         "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
if(_stray_headers)
    message(FATAL_ERROR
        "header(s) found in src/, which must not contain any:\n"
        "      ${_stray_headers}\n"
        "    Headers belong in include/. A duplicate beside the .cpp is moc'd a "
        "second time\n"
        "    and the build fails with redefinition errors that point at the "
        "generated code.\n"
        "    Delete the copy in src/ and wipe the build directory -- the stale "
        "autogen tree\n"
        "    survives otherwise.")
endif()

add_library(aced_core
    src/document.cpp
    src/anchor.cpp
    src/undo.cpp
    src/grammar.cpp
    src/search.cpp
    src/linemap.cpp
    src/tokenchain.cpp
    src/foldmode.cpp
)
add_library(aced::core ALIAS aced_core)

# PUBLIC include: consumers inherit the header path.
# PRIVATE pcre2 and json: both appear only behind our headers, so a consumer
# never has to find them. regex.h does include <pcre2.h>, so it is PUBLIC there;
# swap that header for a QRegularExpression implementation and the dependency
# disappears from the public surface entirely.
target_include_directories(aced_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(aced_core PUBLIC pcre2-8-static)

# Keep the static PCRE2's symbols out of the executable's dynamic symbol table.
#
# Without this, `pcre2_compile_8` and friends are exported globally, and Qt6Core
# and Qt6Gui -- which pull in the SYSTEM libpcre2-8.so through glib -- resolve
# against our copy instead of theirs. Two PCRE2 versions in one process, and
# which one a given call reaches depends on link order and on whatever the
# distribution happens to ship. It works on the build machine, because our 10.44
# is a superset of most distributions' 10.4x, and it is a landmine everywhere
# else.
#
# Verify with:
#   readelf --dyn-syms -W <binary> | grep pcre2_compile_8     # must be empty
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(aced_core INTERFACE "LINKER:--exclude-libs,ALL")
endif()
target_link_libraries(aced_core PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(aced_core PRIVATE /W4)
else()
    target_compile_options(aced_core PRIVATE -Wall -Wextra)
endif()

if(ACED_BUILD_TESTS)
    enable_testing()
    add_executable(aced_tests
        tests/main.cpp
        tests/test_document.cpp
        tests/test_anchor.cpp
        tests/test_undo.cpp
        tests/test_grammar.cpp
        tests/test_search.cpp
        tests/test_linemap.cpp
        tests/test_foldmode.cpp
    )
    target_link_libraries(aced_tests PRIVATE aced_core)
    target_include_directories(aced_tests PRIVATE tests)
    # The grammar tests need the corpus. Passed as a define rather than copied
    # so there is exactly one on disk.
    target_compile_definitions(aced_tests PRIVATE
        ACED_GRAMMARS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/grammars.json")
    add_test(NAME aced_core COMMAND aced_tests)
endif()
