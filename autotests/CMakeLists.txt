# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

# Make sure return values get forwarded properly
add_test(
    NAME truetest
    COMMAND selenium-webdriver-at-spi-run /usr/bin/true
)
set_tests_properties(truetest PROPERTIES ENVIRONMENT "TEST_WITH_KWIN_WAYLAND=0")

add_test(
    NAME falsetest
    COMMAND selenium-webdriver-at-spi-run /usr/bin/false
)
set_tests_properties(falsetest PROPERTIES
    WILL_FAIL TRUE
    ENVIRONMENT "TEST_WITH_KWIN_WAYLAND=0")

add_test(
    NAME screenshottest
    COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/screenshottest.py
)
set_tests_properties(screenshottest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QML_EXEC=$<TARGET_FILE_DIR:Qt${QT_MAJOR_VERSION}::qmake>/qml")

add_test(
    NAME textinputtest
    COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/textinputtest.py
)
set_tests_properties(textinputtest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QML_EXEC=$<TARGET_FILE_DIR:Qt${QT_MAJOR_VERSION}::qmake>/qml")

add_test(
    NAME valuetest
    COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/valuetest.py
)
set_tests_properties(valuetest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QML_EXEC=$<TARGET_FILE_DIR:Qt${QT_MAJOR_VERSION}::qmake>/qml")

add_test(
    NAME imagecomparisontest
    COMMAND selenium-webdriver-at-spi-run ${CMAKE_CURRENT_SOURCE_DIR}/imagecomparisontest.py
)
set_tests_properties(imagecomparisontest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QML_EXEC=$<TARGET_FILE_DIR:Qt${QT_MAJOR_VERSION}::qmake>/qml")

# These tests depend on actual applications and cannot be run by the CI currently. To enable them we'd need a way
# to on-demand install "packages" outside the regular Dependencies system of .kde-ci.yml.
if(NOT "$ENV{KDECI_BUILD}" STREQUAL "TRUE")
    include(ExternalProject)
    ExternalProject_Add(selenium-webdriver-at-spi-examples
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/appiumtests
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/appiumtests
        INSTALL_COMMAND ""
        TEST_EXCLUDE_FROM_MAIN true
    )
    add_test(
        NAME examplestest
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/appiumtests --target test
    )
    set_tests_properties(examplestest PROPERTIES ENVIRONMENT "CTEST_OUTPUT_ON_FAILURE=1")
endif()
