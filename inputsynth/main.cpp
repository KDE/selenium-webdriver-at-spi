// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include <QDebug>
#include <QGuiApplication>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/fakeinput.h>
#include <KWayland/Client/registry.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    std::unique_ptr<KWayland::Client::ConnectionThread> connection(
        KWayland::Client::ConnectionThread::fromApplication());
    if (!connection) {
        qWarning() << "no connection";
        return 1;
    }

    KWayland::Client::Registry registry;
    registry.create(connection.get());

    std::unique_ptr<KWayland::Client::FakeInput> input;
    QObject::connect(&registry,
                     &KWayland::Client::Registry::fakeInputAnnounced,
                     &registry,
                     [&registry, &input](quint32 name, quint32 version) {
                         input.reset(registry.createFakeInput(name, version));
                         input->authenticate(QStringLiteral("inputsynth"), QStringLiteral("hello"));

                         // The offset between KEY_* numbering, and keycodes in the XKB evdev dataset.
                         //    Stolen from xdg-desktop-portal-kde.
                         static const uint EVDEV_OFFSET = 8;

                         const auto keycode = qGuiApp->arguments().at(1).toULongLong();
                         const auto linuxKeycode = keycode - EVDEV_OFFSET;
                         qDebug() << "synthesizing" << linuxKeycode << "from" << keycode;

                         input->requestKeyboardKeyPress(linuxKeycode);
                         input->requestKeyboardKeyRelease(linuxKeycode);

                         qGuiApp->quit();
                     });

    registry.setup();

    return app.exec();
}
