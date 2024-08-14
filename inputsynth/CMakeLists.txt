# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-inputsynth.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop DESTINATION ${KDE_INSTALL_APPDIR})

add_executable(selenium-webdriver-at-spi-inputsynth main.cpp interaction.cpp)
if(QT_MAJOR_VERSION EQUAL "5")
    ecm_add_qtwayland_client_protocol(selenium-webdriver-at-spi-inputsynth
        PROTOCOL ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml
        BASENAME fake-input
    )
    target_link_libraries(selenium-webdriver-at-spi-inputsynth
        Qt::GuiPrivate # QPlatformNativeInterface
    )
else()
    if (Qt6_VERSION VERSION_GREATER_EQUAL "6.8.0")
        set(private_code_option  "PRIVATE_CODE")
    endif()
    qt6_generate_wayland_protocol_client_sources(selenium-webdriver-at-spi-inputsynth FILES ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml ${private_code_option})
endif()

target_link_libraries(selenium-webdriver-at-spi-inputsynth
    Qt::Core
    Qt::Gui
    Qt::WaylandClient # Fake input protocol
    Qt::DBus
    Wayland::Client
    PkgConfig::xkbcommon
)
install(TARGETS selenium-webdriver-at-spi-inputsynth ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
