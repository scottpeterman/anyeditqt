# app/CMakeLists.txt
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# The settings layer is a library rather than two more files in the executable
# so it can be linked into a test binary. Q_OBJECT headers are listed as
# sources: AUTOMOC looks for the header beside the .cpp that includes it, and
# with app/settings.h included as "app/settings.h" it would not find one.
add_library(anyedit_app STATIC
    settings.cpp
    theme.cpp
    mainwindow.cpp
    preferencesdialog.cpp
    aboutdialog.cpp
    recentfiles.cpp
    findbar.cpp
    languages.cpp
    settings.h
    theme.h
    mainwindow.h
    preferencesdialog.h
    recentfiles.h
    findbar.h
    languages.h
)
target_include_directories(anyedit_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(anyedit_app PUBLIC acedqt::core Qt6::Widgets)

if(MSVC)
    target_compile_options(anyedit_app PRIVATE /W4)
else()
    target_compile_options(anyedit_app PRIVATE -Wall -Wextra)
endif()

set(ANYEDIT_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../assets")

# Read by Info.plist.in. CMAKE_OSX_DEPLOYMENT_TARGET is usually unset, and an
# empty LSMinimumSystemVersion is worse than an absent one.
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(ANYEDIT_MIN_MACOS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
    set(ANYEDIT_MIN_MACOS "11.0")
endif()

# Platform icon resources. Listed as SOURCES rather than copied afterwards so
# the build re-runs when the icon changes -- a post-build cp would leave the old
# icon in place until the next clean build, which is the kind of thing that
# wastes an afternoon.
set(ANYEDIT_PLATFORM_SOURCES)
if(APPLE)
    set(ANYEDIT_ICNS "${ANYEDIT_ASSETS}/anyedit.icns")
    if(EXISTS "${ANYEDIT_ICNS}")
        list(APPEND ANYEDIT_PLATFORM_SOURCES "${ANYEDIT_ICNS}")
        set_source_files_properties("${ANYEDIT_ICNS}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    else()
        message(WARNING "assets/anyedit.icns is missing; the bundle will have "
                        "the generic application icon. Run scripts/make-icons.py.")
    endif()
elseif(WIN32)
    if(EXISTS "${ANYEDIT_ASSETS}/anyedit.ico")
        list(APPEND ANYEDIT_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/anyedit.rc")
    else()
        message(WARNING "assets/anyedit.ico is missing; the executable will have "
                        "the generic icon. Run scripts/make-icons.py.")
    endif()
endif()

# anyedit.qrc is listed only when the PNGs it names exist. AUTORCC fails the
# build on a missing <file>, so a checkout without assets/ would not compile
# rather than merely lack an icon.
set(ANYEDIT_QRC)
if(EXISTS "${ANYEDIT_ASSETS}/anyedit-256.png")
    set(ANYEDIT_QRC "${CMAKE_CURRENT_SOURCE_DIR}/anyedit.qrc")
else()
    message(WARNING "assets/ has no PNGs; the window icon will be the platform "
                    "default. Run scripts/make-icons.py.")
endif()

add_executable(anyedit main.cpp ${ANYEDIT_QRC} ${ANYEDIT_PLATFORM_SOURCES})
target_link_libraries(anyedit PRIVATE anyedit_app acedqt::core)
# ON THE LIBRARY, not only on the executable. AboutDialog is in anyedit_app,
# so a define scoped to the anyedit target never reaches it -- the dialog
# compiles, falls back to its "0.0.0-dev" default, and reports the wrong
# version in every bug report with nothing flagging it. The test suite links
# the library without the executable and asserts the real version, which is the
# only place that mistake shows up.
target_compile_definitions(anyedit_app PUBLIC ANYEDIT_VERSION="${PROJECT_VERSION}")

# macOS wants a bundle; the packaging script checks for one and says so if the
# property did not take. Harmless everywhere else.
#
# MACOSX_BUNDLE_ICON_FILE is the file NAME inside Resources, not a path, and
# the .icns extension is optional but harmless. Getting it wrong is silent: the
# bundle builds, and shows the generic icon.
set_target_properties(anyedit PROPERTIES
    # /SUBSYSTEM:WINDOWS. Without it MSVC links a console application and
    # Windows opens a console window behind the editor on every launch,
    # including from Explorer -- which reads as a script somebody wrapped
    # rather than as an application.
    #
    # It also silences stdout, so main.cpp reattaches to the parent console
    # when there is one. `anyedit --check` has to keep printing, because
    # bundle-windows.bat greps it.
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "AnyEdit"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.anyeditqt.anyedit"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "anyedit.icns"
    # NSHumanReadableCopyright in Info.plist, which is what Finder's Get Info
    # panel shows -- so this is a licence claim in the shipped artifact, not a
    # build comment. It said BSD-3-Clause for as long as the licence was
    # undecided and ace's corpus was the only licence in play; that was the
    # provenance of the GRAMMARS, never the licence of the application.
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 Scott Peterman. GPLv3."
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    OUTPUT_NAME "anyedit"
)

if(ACED_BUILD_TESTS)
    add_executable(anyedit_tests tests/test_settings.cpp tests/test_find.cpp
                             tests/test_language.cpp tests/test_editmenu.cpp
                             tests/test_notices.cpp)
    target_link_libraries(anyedit_tests PRIVATE anyedit_app)
    target_include_directories(anyedit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/tests)
    target_compile_definitions(anyedit_tests PRIVATE
        ANYEDIT_CORE_CMAKE="${CMAKE_CURRENT_SOURCE_DIR}/../core/CMakeLists.txt"
        ANYEDIT_APP_CMAKE="${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        ANYEDIT_NOTICES="${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
        ACED_GRAMMARS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/grammars.json")
    add_test(NAME anyedit_settings COMMAND anyedit_tests)
    # A QApplication is constructed: applyTheme() needs a real style and a real
    # palette, and neither exists under QCoreApplication.
    set_tests_properties(anyedit_settings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()