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

install(PROGRAMS run.rb
    RENAME selenium-webdriver-at-spi-run
    DESTINATION ${CMAKE_INSTALL_BINDIR})
    #PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)
install(FILES selenium-webdriver-at-spi.py app_roles.py requirements.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/selenium-webdriver-at-spi)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
