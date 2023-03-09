// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/fakeinput.h>
#include <KWayland/Client/registry.h>

namespace
{
constexpr auto KEY_LEFTSHIFT = 42;
constexpr auto KEY_LEFTALT = 56;
constexpr auto KEY_LEFTCTRL = 29;
constexpr auto sleepTimeMs = 50;

struct KeyboardAction
{
    quint32 level;
    quint32 keycode;

    std::vector<quint32> linuxModifiers() const
    {
        if (level == 0) {
            return {};
        }
        if (level == 1) {
            return {KEY_LEFTSHIFT};
        }
        if (level == 2) {
            return {KEY_LEFTCTRL, KEY_LEFTALT};
        }
        if (level == 3) {
            return {KEY_LEFTCTRL, KEY_LEFTALT, KEY_LEFTSHIFT};
        }
        qCritical("Unknown level!");
        return {};
    }

    quint32 linuxKeyCode() const
    {
        // The offset between KEY_* numbering, and keycodes in the XKB evdev dataset.
        //    Stolen from xdg-desktop-portal-kde.
        static const uint EVDEV_OFFSET = 8;

        const auto linuxKeycode = keycode - EVDEV_OFFSET;
        return linuxKeycode;
    }
};

QList<KeyboardAction> actions;
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    const auto actionFilePath = qGuiApp->arguments().at(1);
    QFile actionFile(actionFilePath);
    if (!actionFile.open(QFile::ReadOnly)) {
        qWarning() << "failed to open action file" << actionFilePath;
        return 1;
    }

    const auto document = QJsonDocument::fromJson(actionFile.readAll());
    const auto jsonActions = document.array();
    for (const auto &jsonAction : jsonActions) {
        const auto hash = jsonAction.toObject().toVariantHash();
        if (hash.value(QStringLiteral("type")).toString() != QStringLiteral("keyboard")) {
            qWarning() << "unsupported action type" << jsonAction;
            return 1;
        }
        actions.push_back({.level=hash.value(QStringLiteral("level")).toUInt(), .keycode = hash.value(QStringLiteral("keycode")).toUInt()});
    }

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

                         for (const auto &action : actions) {
                             for (const auto &modifier : action.linuxModifiers()) {
                                 qDebug() << "  pressing modifier" << modifier;
                                 input->requestKeyboardKeyPress(modifier);
                                 QThread::msleep(sleepTimeMs);
                             }

                             qDebug() << "    pressing key" << action.linuxKeyCode();
                             input->requestKeyboardKeyPress(action.linuxKeyCode());
                             QThread::msleep(sleepTimeMs);
                             input->requestKeyboardKeyRelease(action.linuxKeyCode());
                             QThread::msleep(sleepTimeMs);

                             for (const auto &modifier : action.linuxModifiers()) {
                                 qDebug() << "  releasing modifier" << modifier;
                                 input->requestKeyboardKeyRelease(modifier);
                                 QThread::msleep(sleepTimeMs);
                             }
                        }

                         qGuiApp->quit();
                     });

    registry.setup();

    return app.exec();
}
