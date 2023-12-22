// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include <optional>

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
        if (auto inputType = jsonActionSet[QLatin1String("type")]; inputType == QLatin1String("key")) {
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
        } else if (inputType == QLatin1String("pointer")) {
            /*
              https://github.com/SeleniumHQ/selenium/blob/6620bce4e8e9da1fee3ec5a5547afa7dece3f80e/py/selenium/webdriver/common/actions/pointer_input.py#L66
                def encode(self):
                    return {"type": self.type, "parameters": {"pointerType": self.kind}, "id": self.name, "actions": self.actions}
             */
            const QString id = jsonActionSet[QLatin1String("id")].toString(QStringLiteral("Default"));

            PointerAction::PointerKind pointerTypeInt = PointerAction::PointerKind::Mouse;
            if (const QString pointerType = jsonActionSet[QLatin1String("parameters")].toObject().toVariantHash()[QStringLiteral("pointerType")].toString();
                pointerType == QLatin1String("touch")) {
                pointerTypeInt = PointerAction::PointerKind::Touch;
            } else if (pointerType == QLatin1String("pen")) {
                pointerTypeInt = PointerAction::PointerKind::Pen;
            }

            const auto pointerActions = jsonActionSet[QLatin1String("actions")].toArray();
            for (const auto &pointerAction : pointerActions) {
                const auto hash = pointerAction.toObject().toVariantHash();
                const auto duration = hash.value(QStringLiteral("duration")).value<ulong>();

                PointerAction::ActionType actionTypeInt = PointerAction::ActionType::Cancel;
                if (const QString actionType = hash.value(QStringLiteral("type")).toString(); actionType == QLatin1String("pointerDown")) {
                    actionTypeInt = PointerAction::ActionType::Down;
                } else if (actionType == QLatin1String("pointerUp")) {
                    actionTypeInt = PointerAction::ActionType::Up;
                } else if (actionType == QLatin1String("pointerMove")) {
                    actionTypeInt = PointerAction::ActionType::Move;
                } else if (actionType == QLatin1String("pause")) {
                    actions.emplace_back(new PauseAction(duration));
                    continue;
                }

                PointerAction::Button button = PointerAction::Button::Left;
                if (pointerTypeInt == PointerAction::PointerKind::Mouse) {
                    button = static_cast<PointerAction::Button>(hash.value(QStringLiteral("button")).toInt());
                }

                auto action = new PointerAction(pointerTypeInt, id, actionTypeInt, button, duration);

                if (actionTypeInt == PointerAction::ActionType::Move) {
                    // Positions relative to elements are ignored since at-spi2 can't report correct element positions.
                    PointerAction::Origin originInt = PointerAction::Origin::Viewport;
                    if (hash.value(QStringLiteral("origin")).toString() == QLatin1String("pointer")) {
                        originInt = PointerAction::Origin::Pointer;
                    }

                    const int x = hash.value(QStringLiteral("x")).toInt();
                    const int y = hash.value(QStringLiteral("y")).toInt();
                    action->setPosition({x, y}, originInt);
                }

                actions.emplace_back(action);
            }
            continue;
        } else if (inputType == QLatin1String("wheel")) {
            const QString id = jsonActionSet[QLatin1String("id")].toString(QStringLiteral("Default"));
            const auto wheelActions = jsonActionSet[QLatin1String("actions")].toArray();
            for (const auto &pointerAction : wheelActions) {
                const auto hash = pointerAction.toObject().toVariantHash();
                const auto duration = hash.value(QStringLiteral("duration")).value<ulong>();

                if (const QString actionType = hash.value(QStringLiteral("type")).toString(); actionType == QLatin1String("pause")) {
                    actions.emplace_back(new PauseAction(duration));
                    continue;
                } else {
                    const auto x = hash.value(QStringLiteral("x")).toInt();
                    const auto y = hash.value(QStringLiteral("y")).toInt();
                    const auto deltaX = hash.value(QStringLiteral("deltaX")).toInt();
                    const auto deltaY = hash.value(QStringLiteral("deltaY")).toInt();
                    actions.emplace_back(new WheelAction(id, QPoint(x, y), QPoint(deltaX, deltaY), duration));
                }
            }
        } else {
            qWarning() << "unsupported action type" << jsonActionSet;
            continue;
        }
    }

    auto performActions = [actions = std::move(actions)] {
        for (auto action : actions) {
            action->perform();
        }
        qDeleteAll(actions);
        QCoreApplication::quit();
    };

    app.connect(s_interface, &FakeInputInterface::readyChanged, &app, performActions);

    return app.exec();
}
