# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-inputsynth.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-inputsynth main.cpp)
if(QT_MAJOR_VERSION EQUAL "5")
    ecm_add_qtwayland_client_protocol(selenium-webdriver-at-spi-inputsynth
        PROTOCOL ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml
        BASENAME fake-input
    )
else()
    qt6_generate_wayland_protocol_client_sources(selenium-webdriver-at-spi-inputsynth FILES ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml)
endif()

target_link_libraries(selenium-webdriver-at-spi-inputsynth
    Qt::Core
    Qt::Gui
    Qt::GuiPrivate
    Qt::WaylandClient # Fake input protocol
    Wayland::Client
)
install(TARGETS selenium-webdriver-at-spi-inputsynth ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
