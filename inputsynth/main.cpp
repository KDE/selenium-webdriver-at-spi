// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "interaction.h"

namespace
{
std::optional<wl_keyboard_key_state> typeToKeyState(QStringView type)
{
    if (type == QLatin1String("keyDown")) {
        return WL_KEYBOARD_KEY_STATE_PRESSED;
    }
    if (type == QLatin1String("keyUp")) {
        return WL_KEYBOARD_KEY_STATE_RELEASED;
    }
    qWarning() << "unsupported keyboard action type" << type;
    return {};
}
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

    std::vector<BaseAction *> actions;

    s_interface = new FakeInputInterface;

    const auto document = QJsonDocument::fromJson(actionFile.readAll());
    const auto jsonObject = document.object();
    const auto jsonActions = jsonObject.value(QStringLiteral("actions")).toArray();
    for (const auto &jsonActionSet : jsonActions) {
        if (jsonActionSet[QStringLiteral("type")] != QStringLiteral("key")) {
            qWarning() << "unsupported action type" << jsonActionSet;
            continue;
        }
        for (const auto &jsonAction : jsonActionSet[QStringLiteral("actions")].toArray()) {
            const auto hash = jsonAction.toObject().toVariantHash();
            const auto type = hash.value(QStringLiteral("type")).toString();
            BaseAction *action = nullptr;

            if (type == QLatin1String("pause")) {
                const ulong duration = hash.value(QStringLiteral("duration")).value<ulong>();
                action = new PauseAction(duration);
            } else {
                const auto string = hash.value(QStringLiteral("value")).toString();
                const QChar *character = string.unicode();
                action = new KeyboardAction(*character, typeToKeyState(type).value_or(WL_KEYBOARD_KEY_STATE_RELEASED));
            }

            actions.emplace_back(action);
        }
    }

    auto performActions = [actions = std::move(actions)] {
        for (auto action : actions) {
            action->perform();
        }
        QCoreApplication::quit();
    };

    app.connect(s_interface, &FakeInputInterface::readyChanged, &app, performActions);

    return app.exec();
}
