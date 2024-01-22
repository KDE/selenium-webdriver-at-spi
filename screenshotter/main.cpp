// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

#include <array>
#include <chrono>
#include <optional>

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QTemporaryFile>
#include <qplatformdefs.h>

using namespace std::chrono_literals;

namespace
{
QImage allocateImage(const QVariantMap &metadata)
{
    bool ok = false;

    const int width = metadata.value(QStringLiteral("width")).toInt(&ok);
    if (!ok) {
        return {};
    }

    const int height = metadata.value(QStringLiteral("height")).toInt(&ok);
    if (!ok) {
        return {};
    }

    const int format = metadata.value(QStringLiteral("format")).toInt(&ok);
    if (!ok) {
        return {};
    }

    return {width, height, QImage::Format(format)};
}

QImage readImage(int pipeFd, const QVariantMap &metadata)
{
    QFile out;
    if (!out.open(pipeFd, QFileDevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        qWarning() << "failed to open out pipe for reading";
        ::close(pipeFd);
        return {};
    }

    QImage result = allocateImage(metadata);
    if (result.isNull()) {
        qWarning() << "failed to allocate image";
        return {};
    }

    auto readData = 0;
    while (readData < result.sizeInBytes()) {
        if (const int ret = out.read(reinterpret_cast<char *>(result.bits() + readData), result.sizeInBytes() - readData); ret >= 0) {
            readData += ret;
        }
    }
    return result;
}

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

QImage takeScreenshotX11()
{
    QTemporaryFile tempFile(QStringLiteral("XXXXXX.png"));
    QProcess importProc;
    importProc.start(QStringLiteral("import"), {QStringLiteral("-window"), QStringLiteral("root"), tempFile.fileName()});
    importProc.waitForFinished();
    return QImage(tempFile.fileName());
}

QImage takeScreenshotWayland()
{
    // Unfortunately since the geometries are not including the DPR we can only look at one screen
    // and hope that they are all the same :(
    //
    // Also since the position is entirely wrong on wayland (always 0,0) we currently ignore all of this
    // and instead make full screen shots.
    //
    // const auto dpr = app.primaryScreen()->devicePixelRatio();
    //
    // auto args const = app.arguments().mid(1);
    // const auto positionX = int(args.takeFirst().toInt() * dpr);
    // const auto positionY = int(args.takeFirst().toInt() * dpr);
    // const auto width = int(args.takeFirst().toInt() * dpr);
    // const auto height = int(args.takeFirst().toInt() * dpr);
    // Q_ASSERT(args.isEmpty());

    const auto service = kwinService();
    if (!service.has_value()) {
        qWarning() << "kwin dbus service not resolved";
        return {};
    }

    auto pipeFds = std::to_array<int>({0, 0});
    if (pipe2(pipeFds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
        qWarning() << "failed to open pipe" << strerror(errno);
        return {};
    }

    QVariantList arguments;
    arguments.append(QVariantMap());
    arguments.append(QVariant::fromValue(QDBusUnixFileDescriptor(pipeFds.at(1))));

    auto bus = QDBusConnection::sessionBus();
    QDBusMessage message = QDBusMessage::createMethodCall(service.value(),
                                                          QStringLiteral("/org/kde/KWin/ScreenShot2"),
                                                          QStringLiteral("org.kde.KWin.ScreenShot2"),
                                                          QStringLiteral("CaptureActiveScreen")); // CaptureWorkspace is nicer but only available in plasma6
    message << QVariantMap() << QVariant::fromValue(QDBusUnixFileDescriptor(pipeFds.at(1)));

    QDBusPendingCall msg = bus.asyncCall(message);
    msg.waitForFinished();
    QDBusReply<QVariantMap> reply = msg.reply();
    if (!reply.isValid()) {
        qWarning() << reply.error();
        return {};
    }

    ::close(pipeFds.at(1));

    return readImage(pipeFds.at(0), reply.value());
}
}

int main(int argc, char **argv)
{
    const QGuiApplication app(argc, argv);

    QImage image;
    if (qgetenv("TEST_WITH_KWIN_WAYLAND") == "0") {
        image = takeScreenshotX11();
        if (image.isNull()) {
            return 1;
        }
    } else {
        image = takeScreenshotWayland();
        if (image.isNull()) {
            return 1;
        }
    }

    QBuffer buf;
    image.save(&buf, "PNG");
    printf("%s", buf.data().toBase64().constData()); // intentionally no newline so we don't need to strip on the py side
    return 0;
}
