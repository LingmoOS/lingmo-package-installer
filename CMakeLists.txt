cmake_minimum_required(VERSION 3.22)
project(lingmo-package-installer VERSION 1.0.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

option(PACKAGE_INSTALLER_BUILD_UI "Build the Lingmo SDK application" ON)

add_library(package-installer-core STATIC src/package.cpp)
target_include_directories(package-installer-core PUBLIC src)
target_compile_options(package-installer-core PRIVATE -Wall -Wextra -Wpedantic)

if(PACKAGE_INSTALLER_BUILD_UI)
    find_package(LingmoSDK REQUIRED)
    add_executable(lingmo-package-installer
        src/main.cpp
        src/mainwindow.cpp
        src/globalmenu.cpp
        src/installcontroller.cpp
    )
    target_include_directories(lingmo-package-installer PRIVATE src)
    target_link_libraries(lingmo-package-installer PRIVATE package-installer-core Lingmo::SDK)
    target_compile_options(lingmo-package-installer PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS lingmo-package-installer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES data/lingmo-package-installer.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    lingmo_add_translations(lingmo-package-installer SOURCES src DESKTOP_FILES lingmo-package-installer.desktop)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lingmo-package-installer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

if(BUILD_TESTING)
    add_executable(package-installer-core-tests tests/package_test.cpp)
    target_link_libraries(package-installer-core-tests PRIVATE package-installer-core)
    add_test(NAME package-installer-core COMMAND package-installer-core-tests)
endif()
