// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022-2023 Harald Sitter <sitter@kde.org>

#include <chrono>

#include <QDebug>
#include <QGuiApplication>
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
        static constexpr auto syncTimes = 3;
        for (auto i = 0; i < syncTimes; i++) {
            QCoreApplication::processEvents();
            m_connection->roundtrip();
            QCoreApplication::processEvents();
        }
        QCoreApplication::processEvents();
        Q_ASSERT(m_windowManagement);
        const auto windows = m_windowManagement->windows();
        for (const auto &window : windows) {
            insert(window);
        }
    }

    void insert(KWayland::Client::PlasmaWindow *window)
    {
        m_pidsToAppIds.insert(QString::number(window->pid()), window->appId());
    }

    QVariantHash data() const
    {
        return m_pidsToAppIds;
    }

private:
    QVariantHash m_pidsToAppIds;
    std::unique_ptr<KWayland::Client::ConnectionThread> m_connection;
    KWayland::Client::Registry m_registry;
    std::unique_ptr<KWayland::Client::PlasmaWindowManagement> m_windowManagement;
};

QVariantHash waylandPidsToAppIds()
{
    WaylandLister lister;
    return lister.data();
}

QVariantHash x11PidsToAppIds()
{
    QVariantHash pidsToAppIds;
    const auto wids = KX11Extras::windows();
    for (const auto &wid : wids) {
        const KWindowInfo info(wid, NET::WMPid, NET::WM2DesktopFileName | NET::WM2GTKApplicationId);
        if (!info.desktopFileName().isEmpty()) {
            pidsToAppIds.insert(QString::number(info.pid()), info.desktopFileName());
        }
        if (!info.gtkApplicationId().isEmpty()) {
            pidsToAppIds.insert(QString::number(info.pid()), info.gtkApplicationId());
        }
    }
    return pidsToAppIds;
}

int main(int argc, char **argv)
{
    const QGuiApplication app(argc, argv);

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
    return 0;
}

#include "main.moc"
