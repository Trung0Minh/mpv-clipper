cmake_minimum_required(VERSION 3.22)
project(mpv-clipper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_library(clipper_core
    src/app/LaunchOptions.cpp
    src/app/LaunchPayload.cpp
    src/app/MpvInstall.cpp
    src/app/SingleInstance.cpp
    src/media/MediaProbe.cpp
    src/media/Timecode.cpp
    src/media/MpvPlayer.cpp
    src/media/MpvRenderWidget.cpp
    src/export/ExportCommandBuilder.cpp
    src/export/ExportJob.cpp
    src/ui/RangeTimeline.cpp
    src/ui/MainWindow.cpp
    src/ui/Theme.cpp
    assets/theme.qrc
    src/util/Logging.cpp
)
target_include_directories(clipper_core PUBLIC src)
target_link_libraries(clipper_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::OpenGL Qt6::OpenGLWidgets PkgConfig::MPV
)
add_executable(mpv-clipper src/main.cpp)
if(APPLE)
    set_target_properties(mpv-clipper PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.mpv-clipper.app"
        MACOSX_BUNDLE_BUNDLE_NAME "mpv Clipper"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()
target_link_libraries(mpv-clipper PRIVATE clipper_core)
include(GNUInstallDirs)
install(TARGETS mpv-clipper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES mpv/clipper.lua DESTINATION ${CMAKE_INSTALL_DATADIR}/mpv/scripts)
install(FILES mpv/clipper.conf.example DESTINATION ${CMAKE_INSTALL_DATADIR}/mpv-clipper)
install(FILES assets/mpv-clipper.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY plan DESTINATION ${CMAKE_INSTALL_DOCDIR})
set(CPACK_PACKAGE_NAME "mpv-clipper")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "TGZ")
include(CPack)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(install_tests tests/install_tests.cpp)
    target_link_libraries(install_tests PRIVATE clipper_core)
    add_test(NAME install COMMAND install_tests $<TARGET_FILE:mpv-clipper>)
    add_executable(batch_tests tests/batch_tests.cpp)
    target_link_libraries(batch_tests PRIVATE clipper_core Qt6::Test)
    add_test(NAME batch COMMAND batch_tests)
    set_tests_properties(batch PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(startup_tests tests/startup_tests.cpp)
    target_include_directories(startup_tests PRIVATE src)
    target_link_libraries(startup_tests PRIVATE clipper_core)
    add_test(NAME startup COMMAND startup_tests)
    set_tests_properties(startup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(export_tests tests/export_tests.cpp)
    target_link_libraries(export_tests PRIVATE clipper_core)
    add_test(NAME exports COMMAND export_tests)
    set_tests_properties(exports PROPERTIES TIMEOUT 180)
    add_executable(seek_export_tests tests/seek_export_tests.cpp)
    target_link_libraries(seek_export_tests PRIVATE clipper_core)
    add_test(NAME seek_exports COMMAND seek_export_tests)
    set_tests_properties(seek_exports PROPERTIES TIMEOUT 180)
    add_executable(ipc_tests tests/ipc_tests.cpp)
    target_link_libraries(ipc_tests PRIVATE clipper_core)
    add_test(NAME ipc COMMAND ipc_tests)
    set_tests_properties(ipc PROPERTIES TIMEOUT 25)
    add_executable(player_tests tests/player_tests.cpp)
    target_link_libraries(player_tests PRIVATE clipper_core)
    add_test(NAME player COMMAND player_tests)
    set_tests_properties(player PROPERTIES TIMEOUT 40)
    add_executable(ui_smoke tests/ui_smoke.cpp)
    target_link_libraries(ui_smoke PRIVATE clipper_core Qt6::Test)
    add_test(NAME ui_smoke COMMAND ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
    find_program(MPV_EXECUTABLE mpv)
    if(MPV_EXECUTABLE)
        add_test(NAME bridge COMMAND ${MPV_EXECUTABLE} --no-config --idle=yes
            --vo=null --ao=null --script=${CMAKE_CURRENT_SOURCE_DIR}/tests/bridge_tests.lua)
        set_tests_properties(bridge PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 15)
    endif()
endif()
