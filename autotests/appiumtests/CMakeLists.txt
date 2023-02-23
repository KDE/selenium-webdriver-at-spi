# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

project(selenium-webdriver-at-spi-appiumtests)

cmake_minimum_required(VERSION 3.22)
find_package(ECM 5.99 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(CMakePackageConfigHelpers)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SeleniumWebDriverATSPI)
    set_package_properties(SeleniumWebDriverATSPI PROPERTIES
        DESCRIPTION "Server component for selenium tests using Linux accessibility infrastructure"
        PURPOSE "Needed for GUI tests"
        URL "https://invent.kde.org/sdk/selenium-webdriver-at-spi"
        TYPE OPTIONAL
    )

    add_test(
        NAME calculatortest
        COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/calculatortest.py
    )
    set_tests_properties(calculatortest PROPERTIES TIMEOUT 300)

    add_test(
        NAME kinfocentertest
        COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/kinfocentertest.rb
    )
    set_tests_properties(kinfocentertest PROPERTIES TIMEOUT 300 ENVIRONMENT "RECORD_VIDEO_NAME=kinfocentertest.webm")

    add_test(
        NAME kinfocentertest-xwayland
        COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/kinfocentertest.rb
    )
    set_tests_properties(kinfocentertest PROPERTIES TIMEOUT 300 ENVIRONMENT "TEST_WITH_XWAYLAND=1")
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
