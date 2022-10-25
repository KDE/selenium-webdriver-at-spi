# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

project(selenium-webdriver-at-spi)

cmake_minimum_required(VERSION 3.22)
find_package(ECM 5.99 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable legacy stuff to get rid of some deprecation warnings. Notably duplicated QProcess::finished overloads.
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050e00)

find_package(KF5 5.99 REQUIRED COMPONENTS WindowSystem)

add_subdirectory(appidlister)

install(PROGRAMS run.rb
    RENAME selenium-webdriver-at-spi-run
    DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES selenium-webdriver-at-spi.py app_roles.py requirements.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/selenium-webdriver-at-spi)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)