// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

#include <QDebug>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <KWindowInfo>
#include <KWindowSystem>

int main(int argc, char **argv)
{
    const QGuiApplication app(argc, argv);
    const auto wids = KWindowSystem::windows();
    QVariantHash pidsToAppIds;
    for (const auto &wid : wids) {
        const KWindowInfo info(wid, NET::WMPid, NET::WM2DesktopFileName | NET::WM2GTKApplicationId);
        if (!info.desktopFileName().isEmpty()) {
            pidsToAppIds.insert(QString::number(info.pid()), info.desktopFileName());
        }
        if (!info.gtkApplicationId().isEmpty()) {
            pidsToAppIds.insert(QString::number(info.pid()), info.gtkApplicationId());
        }
    }
    const QJsonDocument doc(QJsonObject::fromVariantHash(pidsToAppIds));
    printf("%s\n", doc.toJson().constData());
}
