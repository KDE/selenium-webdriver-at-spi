# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-inputsynth.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-inputsynth main.cpp)
target_link_libraries(selenium-webdriver-at-spi-inputsynth
    Qt::Core
    Qt::Gui
    KF${QT_MAJOR_VERSION}::WaylandClient
)
install(TARGETS selenium-webdriver-at-spi-inputsynth ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
