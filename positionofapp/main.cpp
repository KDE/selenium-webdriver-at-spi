// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022-2023 Harald Sitter <sitter@kde.org>

#include <chrono>

#include <QDebug>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/plasmawindowmanagement.h>
#include <KWayland/Client/registry.h>

#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>

using namespace std::chrono_literals;

class WaylandLister : public QObject
{
    Q_OBJECT
public:
    explicit WaylandLister(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_connection.reset(KWayland::Client::ConnectionThread::fromApplication());
        if (!m_connection) {
            qWarning() << "no connection";
            return;
        }

        m_registry.create(m_connection.get());

        QObject::connect(&m_registry,
                         &KWayland::Client::Registry::plasmaWindowManagementAnnounced,
                         this,
                         [this](quint32 name, quint32 version) {
                             m_windowManagement.reset(m_registry.createPlasmaWindowManagement(name, version));
                         });

        m_registry.setup();

        // We'll need 3 because getting the registry is async, getting the window management interface is another,
        // then we'll have requested information about every window. By the 3rd sync it should have sent everything.
        static constexpr auto syncTimes = 30;
        for (auto i = 0; i < syncTimes; i++) {
            QCoreApplication::processEvents();
            m_connection->roundtrip();
            QCoreApplication::processEvents();
        }
        QCoreApplication::processEvents();
        Q_ASSERT(m_windowManagement);
        m_windows = m_windowManagement->windows();
        qDebug() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << m_windowManagement->windows();
    }

    QPoint positionOfWindow(quint32 pid, QSize s) const
    {
        for (const auto &window : m_windows) {
            qDebug() << "… LOOKING FOR APP!" << pid << s;
            qDebug() << "CANDIDATE:" << window->pid() << window->geometry().size();
            if(window->pid() == pid && window->geometry().size() == s) {
                return window->geometry().topLeft();
            }
        }
        return QPoint(0, 0);
    }

private:
    QList<KWayland::Client::PlasmaWindow*> m_windows;
    std::unique_ptr<KWayland::Client::ConnectionThread> m_connection;
    KWayland::Client::Registry m_registry;
    std::unique_ptr<KWayland::Client::PlasmaWindowManagement> m_windowManagement;
};

QPoint waylandPositionOfWindow(quint32 pid, QSize s)
{
    WaylandLister lister;
    return lister.positionOfWindow(pid, s);
}

QPoint x11PositionOfWindow(quint32 pid, QSize s)
{
    return QPoint(10, 10);
}

int main(int argc, char **argv) {

    const QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addPositionalArgument(QString::fromUtf8("pid"), QString::fromUtf8("The pid of the application to find."));
    parser.addPositionalArgument(QString::fromUtf8("w"), QString::fromUtf8("The width of the application to find."));
    parser.addPositionalArgument(QString::fromUtf8("h"), QString::fromUtf8("The height of the application to find."));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    quint32 pid = args[0].toUInt();
    int w = args[1].toInt();
    int h = args[2].toInt();
    QPoint p = waylandPositionOfWindow(pid, QSize(w, h));
    printf("%d %d", p.x(), p.y());
    return 0;

    /*

    QVariantHash pidsToAppIds;

    if (KWindowSystem::isPlatformX11()) {
        pidsToAppIds.insert(x11PidsToAppIds());
    } else if (KWindowSystem::isPlatformWayland()) {
        pidsToAppIds.insert(waylandPidsToAppIds());
    } else {
        qFatal("unsupported platform");
        return 1;
    }

    // Always append .desktop for convenience. Means we can do straight forward string matching in the python side.
    for (auto it = pidsToAppIds.begin(); it != pidsToAppIds.end(); ++it) {
        static const QLatin1String suffix(".desktop");
        const QString value = it.value().toString();
        if (!value.endsWith(suffix)) {
            it->setValue(QString(value + suffix));
        }
    }

    const QJsonDocument doc(QJsonObject::fromVariantHash(pidsToAppIds));
    printf("%s\n", doc.toJson().constData());
    return 0;*/
}

#include "main.moc"
