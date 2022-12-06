# This module defines the following variables:
#
#  at-spi2-registryd_FOUND - true if found
#  at-spi2-registryd_PATH - path to the bin (only when found)
#
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

include(ProgramFinder)
program_finder(at-spi2-registryd
    PATHS
        /usr/lib/at-spi2-core/ # debians
        /usr/libexec/ # newer debians
        /usr/lib/at-spi2/ # suses
    DOC "AT-SPI accessibility registry daemon"
)
