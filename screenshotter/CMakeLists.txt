# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-screenshotter.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-screenshotter.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-screenshotter.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-screenshotter main.cpp)
target_link_libraries(selenium-webdriver-at-spi-screenshotter
    Qt::Core
    Qt::Gui
    Qt::DBus
)
install(TARGETS selenium-webdriver-at-spi-screenshotter ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
