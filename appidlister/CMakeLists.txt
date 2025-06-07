# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-appidlister.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-appidlister.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-appidlister.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-appidlister main.cpp)
target_link_libraries(selenium-webdriver-at-spi-appidlister
    Qt::Core
    Qt::Gui
    KF6::WindowSystem
)

target_link_libraries(selenium-webdriver-at-spi-appidlister Plasma::KWaylandClient)

install(TARGETS selenium-webdriver-at-spi-appidlister ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
