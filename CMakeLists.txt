# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

project(selenium-webdriver-at-spi)

cmake_minimum_required(VERSION 3.22)
find_package(ECM 5.99 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable legacy stuff to get rid of some deprecation warnings. Notably duplicated QProcess::finished overloads.
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050e00)

find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core DBus)
find_package(KF5 5.99 REQUIRED COMPONENTS WindowSystem Wayland)

add_subdirectory(appidlister)
add_subdirectory(screenshotter)
add_subdirectory(autotests)
add_subdirectory(inputsynth)

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
