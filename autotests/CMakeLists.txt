# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

# Make sure return values get forwarded properly
add_test(
    NAME truetest
    COMMAND selenium-webdriver-at-spi-run /usr/bin/true
)
add_test(
    NAME falsetest
    COMMAND selenium-webdriver-at-spi-run /usr/bin/false
)
set_tests_properties(falsetest PROPERTIES WILL_FAIL TRUE)
