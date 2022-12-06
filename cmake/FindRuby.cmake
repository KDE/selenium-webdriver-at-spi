# This module defines the following variables:
#
#  Ruby_FOUND - true if found
#  Ruby_PATH - path to the bin (only when found)
#
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

include(ProgramFinder)
program_finder(ruby)
