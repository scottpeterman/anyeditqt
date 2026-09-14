# widget/CMakeLists.txt
#
# acedqt::core -- the Qt view over aced::core. Everything Qt lives here and
# nothing in core/ knows it exists.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

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


# NOT qt_standard_project_setup(): that is 6.3+, and a machine with an older
# system Qt satisfies find_package(Qt6 6.2) and then fails with "Unknown CMake
# command". These are the properties it would have set.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# The headers are listed as sources on purpose. AUTOMOC looks for a Q_OBJECT
# header next to the .cpp that includes it; with include/ and src/ split it
# finds nothing, moc is never run, and the link fails on every signal with
# "undefined reference to EditorWidget::textChanged()" -- which reads as a
# missing source file rather than as a missing moc pass.
add_library(acedqt_core
    src/palette.cpp
    src/linecache.cpp
    src/editorwidget.cpp
    include/acedqt/editorwidget.h
    include/acedqt/linecache.h
    include/acedqt/palette.h
)
add_library(acedqt::core ALIAS acedqt_core)

target_include_directories(acedqt_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(acedqt_core PUBLIC aced::core Qt6::Widgets)

if(MSVC)
    target_compile_options(acedqt_core PRIVATE /W4)
else()
    target_compile_options(acedqt_core PRIVATE -Wall -Wextra)
endif()

if(ACED_BUILD_TESTS)
    add_executable(acedqt_tests tests/test_widget.cpp)
    target_link_libraries(acedqt_tests PRIVATE acedqt::core)
    target_include_directories(acedqt_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/tests)
    target_compile_definitions(acedqt_tests PRIVATE
        ACED_GRAMMARS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/grammars.json")
    # offscreen so this runs on a build machine with no display. It is a real
    # QApplication with real key events, not a mock.
    add_test(NAME acedqt_widget COMMAND acedqt_tests)
    set_tests_properties(acedqt_widget PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
