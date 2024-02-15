# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

configure_file(org.kde.selenium-webdriver-at-spi-inputsynth.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.selenium-webdriver-at-spi-inputsynth.desktop DESTINATION ${KDE_INSTALL_APPDIR})

set(inputsynth_SRCS main.cpp interaction.cpp interface.cpp)

set(inputcapture_xml ${CMAKE_CURRENT_SOURCE_DIR}/org.freedesktop.portal.InputCapture.xml)
set_source_files_properties(${inputcapture_xml} PROPERTIES INCLUDE "inputcapture_dbustypes.h")
qt_add_dbus_interface(inputsynth_SRCS ${inputcapture_xml} inputcapture)

add_executable(selenium-webdriver-at-spi-inputsynth ${inputsynth_SRCS})
if(QT_MAJOR_VERSION EQUAL "5")
    ecm_add_qtwayland_client_protocol(selenium-webdriver-at-spi-inputsynth
        PROTOCOL ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml
        BASENAME fake-input
    )
    target_link_libraries(selenium-webdriver-at-spi-inputsynth
        Qt::GuiPrivate # QPlatformNativeInterface
    )
else()
    qt6_generate_wayland_protocol_client_sources(selenium-webdriver-at-spi-inputsynth FILES ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml)
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
