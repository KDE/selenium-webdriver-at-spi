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
    KF${QT_MAJOR_VERSION}::CoreAddons
    Wayland::Client
    K::KPipeWireRecord
)

if (QT_MAJOR_VERSION EQUAL "5")
    ecm_add_qtwayland_client_protocol(selenium-webdriver-at-spi-recorder
        PROTOCOL ${PLASMA_WAYLAND_PROTOCOLS_DIR}/zkde-screencast-unstable-v1.xml
        BASENAME zkde-screencast-unstable-v1
    )
else()
    if (Qt6_VERSION VERSION_GREATER_EQUAL "6.8.0")
        set(private_code_option  "PRIVATE_CODE")
    endif()
    qt6_generate_wayland_protocol_client_sources(selenium-webdriver-at-spi-recorder FILES
        ${PLASMA_WAYLAND_PROTOCOLS_DIR}/zkde-screencast-unstable-v1.xml
        ${private_code_option})
endif()

install(TARGETS selenium-webdriver-at-spi-recorder ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
