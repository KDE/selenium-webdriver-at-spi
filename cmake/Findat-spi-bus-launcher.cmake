# This module defines the following variables:
#
#  at-spi-bus-launcher_FOUND - true if found
#  at-spi-bus-launcher_PATH - path to the bin (only when found)
#
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

include(ProgramFinder)
program_finder(at-spi-bus-launcher
    PATHS
        /usr/lib/at-spi2-core/ # debians
        /usr/libexec/ # newer debians
        /usr/lib/at-spi2/ # suses
    DOC "AT-SPI accessibility dbus launcher"
)
