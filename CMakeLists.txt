cmake_minimum_required(VERSION 3.16)
project(reelplay VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

set(SOURCES player.cpp options.cpp videowidget.cpp mpris.cpp player.h videowidget.h mpris.h)
add_executable(reelplay main.cpp ${SOURCES})
target_link_libraries(reelplay PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::DBus PkgConfig::MPV)

find_package(Qt6 COMPONENTS Test)
if(Qt6Test_FOUND)
    enable_testing()
    add_executable(test_player tests/test_player.cpp ${SOURCES})
    target_link_libraries(test_player PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::DBus Qt6::Test PkgConfig::MPV)
    target_compile_definitions(test_player PRIVATE REELPLAY_BIN="$<TARGET_FILE:reelplay>")
    add_dependencies(test_player reelplay)
    add_test(NAME test_player COMMAND test_player)
endif()

include(GNUInstallDirs)
install(TARGETS reelplay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/reelplay.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/reelplay.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

# cmake --build build --target package  ->  build/reelplay_<version>_amd64.deb
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_CONTACT "Matthew <firefoxx.ms@gmail.com>")
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Classic-style media player (Qt 6 + libmpv)")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION video)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # libmpv/Qt dependencies from the binary
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
# Reelplay was called newmpc-be up to 0.3.1: installing it replaces the old package.
set(CPACK_DEBIAN_PACKAGE_REPLACES "newmpc-be")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "newmpc-be")
include(CPack)
