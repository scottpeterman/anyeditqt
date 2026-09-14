# tools/CMakeLists.txt
#
# render_probe needs Qt but not the widget, which does not exist yet. It is
# behind its own option so the core build stays Qt-free.
#
# Core is named explicitly rather than relied on to arrive with Widgets, so a
# failure says which component is missing.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# NOT qt_standard_project_setup(). That is Qt 6.3+, and a machine with an older
# system Qt satisfies `find_package(Qt6 6.2)` and then fails with "Unknown CMake
# command", which reads as a broken CMakeLists rather than as a Qt too old. The
# three properties it would have set are set here instead, which works on
# everything from 6.2 up.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Said out loud, before anything is built. A machine with both a distribution
# Qt and an installed one (~/Qt/6.10.3/gcc_64, /opt/Qt, aqtinstall) will pick
# whichever find_package reaches first, and the packaging scripts then deploy
# the libraries THAT Qt resolves to. Linking one and deploying another is the
# failure that works perfectly on the build machine, so the version and the
# prefix belong in the log where a wrong one is visible immediately.
get_target_property(_qt_core_lib Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_lib)
    get_target_property(_qt_core_lib Qt6::Core IMPORTED_LOCATION)
endif()
message(STATUS "Qt ${Qt6_VERSION}")
message(STATUS "  prefix:  ${QT6_INSTALL_PREFIX}")
message(STATUS "  QtCore:  ${_qt_core_lib}")

add_executable(render_probe render_probe.cpp)
target_link_libraries(render_probe PRIVATE aced::core Qt6::Widgets)
