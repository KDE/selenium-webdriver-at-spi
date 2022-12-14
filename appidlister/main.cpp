// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

#include <chrono>

#include <QDebug>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/windowtasksmodel.h>

using namespace std::chrono_literals;

int main(int argc, char **argv)
{
    const QGuiApplication app(argc, argv);

    QTimer timer;
    timer.setInterval(500ms);
    TaskManager::WindowTasksModel model;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &timer, QOverload<>::of(&QTimer::start));
    QObject::connect(&timer, &QTimer::timeout, &app, &QCoreApplication::quit);
    timer.start();

    app.exec();

    QVariantHash pidsToAppIds;
    const auto count = model.rowCount();
    for (auto i = 0; i < count; ++i) {
        const auto index = model.index(i, 0);
        auto appId = model.data(index, TaskManager::AbstractTasksModel::AppId).toString();
        // On x11 the id is without suffix on wayland it is with, reconcile the two by always appending .desktop
        static const QString suffix = QStringLiteral(".desktop");
        if (!appId.endsWith(suffix)) {
            appId.append(suffix);
        }
        pidsToAppIds.insert(model.data(index, TaskManager::AbstractTasksModel::AppPid).toString(), appId);
    }
    const QJsonDocument doc(QJsonObject::fromVariantHash(pidsToAppIds));
    printf("%s\n", doc.toJson().constData());
    return 0;
}
