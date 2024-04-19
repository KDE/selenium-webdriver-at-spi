# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-positionofapp.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-positionofapp.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-positionofapp.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-positionofapp main.cpp)
target_link_libraries(selenium-webdriver-at-spi-positionofapp
    Qt::Core
    Qt::Gui
    KF${QT_MAJOR_VERSION}::WindowSystem
)

if(QT_MAJOR_VERSION STREQUAL "5")
    target_link_libraries(selenium-webdriver-at-spi-positionofapp KF5::WaylandClient)
else()
    target_link_libraries(selenium-webdriver-at-spi-positionofapp Plasma::KWaylandClient)
endif()

install(TARGETS selenium-webdriver-at-spi-positionofapp ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
