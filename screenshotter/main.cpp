// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

#include <optional>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

// When the tests are run under an existing session, the well known org.kde.KWin name will be claimed by the real
// kwin, figure out where our test kwin resides on the bus by reverse looking up the PID.
std::optional<QString> kwinService()
{
    auto bus = QDBusConnection::sessionBus();

    const QString kwinPid = qEnvironmentVariable("KWIN_PID");
    if (kwinPid.isEmpty()) {
        return QStringLiteral("org.kde.KWin");
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                            QStringLiteral("/org/freedesktop/DBus"),
                                                            QStringLiteral("org.freedesktop.DBus"),
                                                            QStringLiteral("ListNames"));
    QDBusReply<QStringList> namesReply = bus.call(message);
    if (namesReply.isValid()) {
        const auto names = namesReply.value();
        for (const auto &name : names) {
            QDBusMessage getPid = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                                    QStringLiteral("/org/freedesktop/DBus"),
                                                                    QStringLiteral("org.freedesktop.DBus"),
                                                                    QStringLiteral("GetConnectionUnixProcessID"));
            getPid << name;
            QDBusReply<quint32> pid = bus.call(getPid);
            if (pid.isValid() && QString::number(pid.value()) == kwinPid) {
                return name;
            }
        }
    } else {
        qWarning() << namesReply.error();
    }

    return std::nullopt;
}

int main(int argc, char **argv)
{
    const QGuiApplication app(argc, argv);

    // Unfortunately since the geometries are not including the DPR we can only look at one screen
    // and hope that they are all the same :(
    //
    // Also since the position is entirely wrong on wayland (always 0,0) we currently ignore all of this
    // and instead make full screen shots.
    //
    // const auto dpr = app.primaryScreen()->devicePixelRatio();
    //
    // auto args = app.arguments().mid(1);
    // const auto positionX = int(args.takeFirst().toInt() * dpr);
    // const auto positionY = int(args.takeFirst().toInt() * dpr);
    // const auto width = int(args.takeFirst().toInt() * dpr);
    // const auto height = int(args.takeFirst().toInt() * dpr);
    // Q_ASSERT(args.isEmpty());

    const auto service = kwinService();
    if (!service.has_value()) {
        qWarning() << "service not resolved";
        return 1;
    }

    auto bus = QDBusConnection::sessionBus();
    QDBusMessage message = QDBusMessage::createMethodCall(service.value(),
                                                          QStringLiteral("/Screenshot"),
                                                          QStringLiteral("org.kde.kwin.Screenshot"),
                                                          QStringLiteral("screenshotFullscreen"));

    QDBusReply<QString> reply = bus.call(message);
    if (!reply.isValid()) {
        qWarning() << reply.error();
        return 1;
    }
    printf("%s", qUtf8Printable(reply)); // intentionally no newline so we don't need to strip on the py side
    return 0;
}


