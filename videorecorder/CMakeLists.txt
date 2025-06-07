# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-recorder.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-recorder.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-recorder.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-recorder main.cpp screencasting.cpp)
target_link_libraries(selenium-webdriver-at-spi-recorder
    Qt::Core
    Qt::Gui
    Qt::GuiPrivate
    Qt::WaylandClient
    Qt::DBus
    KF6::CoreAddons
    Wayland::Client
    K::KPipeWireRecord
)

qt6_generate_wayland_protocol_client_sources(selenium-webdriver-at-spi-recorder FILES
    ${PLASMA_WAYLAND_PROTOCOLS_DIR}/zkde-screencast-unstable-v1.xml)

install(TARGETS selenium-webdriver-at-spi-recorder ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
