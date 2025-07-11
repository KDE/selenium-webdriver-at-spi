# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

cmake_minimum_required(VERSION 3.22)
project(selenium-webdriver-at-spi)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(CMakePackageConfigHelpers)
include(FindPkgConfig)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable legacy stuff to get rid of some deprecation warnings. Notably duplicated QProcess::finished overloads.
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050e00)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core DBus WaylandClient)

if (Qt6Gui_VERSION VERSION_GREATER_EQUAL "6.10.0")
    find_package(Qt6GuiPrivate ${REQUIRED_QT_VERSION} REQUIRED NO_MODULE)
endif()

find_package(KF6 6.0.0 REQUIRED COMPONENTS WindowSystem CoreAddons)
find_package(KWayland REQUIRED)

find_package(KPipeWire REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)
find_package(PlasmaWaylandProtocols REQUIRED)

# Runtime Dependencies
include(cmake/FindPythonModule.cmake)
find_pythonmodule(flask)
find_pythonmodule(pyatspi)
find_pythonmodule(lxml)
find_pythonmodule(gi)
pkg_check_modules(GObjectIntrospection gobject-introspection-1.0)
add_feature_info(GObjectIntrospection GObjectIntrospection_FOUND "required at runtime!")

set_package_properties(PkgConfig PROPERTIES TYPE REQUIRED)
pkg_check_modules(xkbcommon xkbcommon REQUIRED IMPORTED_TARGET)
set_package_properties(xkbcommon PROPERTIES TYPE REQUIRED)

add_subdirectory(appidlister)
add_subdirectory(screenshotter)
add_subdirectory(autotests)
add_subdirectory(inputsynth)
add_subdirectory(videorecorder)

install(PROGRAMS run.rb
    RENAME selenium-webdriver-at-spi-run
    DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES selenium-webdriver-at-spi.py app_roles.py requirements.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/selenium-webdriver-at-spi)

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/SeleniumWebDriverATSPI")

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/SeleniumWebDriverATSPIConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SeleniumWebDriverATSPIConfig.cmake
    INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SeleniumWebDriverATSPIConfig.cmake
        cmake/Findat-spi2-registryd.cmake
        cmake/Findat-spi-bus-launcher.cmake
        cmake/FindPython3.cmake
        cmake/FindPythonModule.cmake
        cmake/FindPythonModule.cmake.in
        cmake/FindRuby.cmake
        cmake/ProgramFinder.cmake
    DESTINATION ${CMAKECONFIG_INSTALL_DIR}
    COMPONENT Devel
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
