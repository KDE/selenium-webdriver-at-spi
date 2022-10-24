# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

add_executable(selenium-webdriver-at-spi-appidlister main.cpp)
target_link_libraries(selenium-webdriver-at-spi-appidlister
    Qt::Core
    Qt::Gui
    KF5::WindowSystem
)
install(TARGETS selenium-webdriver-at-spi-appidlister ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
