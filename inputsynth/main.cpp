// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include <iostream>
#include <optional>

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "interaction.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;


#include <QDBusConnectionInterface>
#include <QMetaMethod>
#include <QAccessibleInterface>
#include <QWindow>
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/plasmawindowmanagement.h>
#include <KWayland/Client/registry.h>
#include <QThread>


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

class GeometryResolver : public QObject
{
    Q_OBJECT

    static constexpr auto EXTENSION_PATH = "/org/kde/plasma/a11y/atspi/extension"_L1;
    static constexpr auto EXTENSION_INTERFACE = "org.kde.plasma.a11y.atspi.extension"_L1;
public:
    explicit GeometryResolver(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_registry.create(m_connection.get());
        QObject::connect(&m_registry, &KWayland::Client::Registry::plasmaWindowManagementAnnounced, [this](quint32 name, quint32 version) {
            m_windowManagement.reset(m_registry.createPlasmaWindowManagement(name, version));
            Q_EMIT ready();
        });
        m_registry.setup();

        // // We'll need 3 because getting the registry is async, getting the window management interface is another.
        // static constexpr auto syncTimes = 2;
        // for (auto i = 0; i < syncTimes; i++) {
        //     QCoreApplication::processEvents();
        //     m_connection->roundtrip();
        //     QCoreApplication::processEvents();
        // }
        // QCoreApplication::processEvents();
        // Q_ASSERT(m_windowManagement);
    }

    QRect globalGeometryFor(const QString &accessibleId)
    {
        QCoreApplication::processEvents();
        auto request = QDBusMessage::createMethodCall(serviceName(), EXTENSION_PATH, EXTENSION_INTERFACE, QStringLiteral("accessibleProperties"));
        request << accessibleId;
        const QDBusReply<QVariantHash> reply = connection().call(request);
        const auto hash = reply.value();

        // This is the geometry of the accessible relative to its window.
        QRect accessibleRect(hash.value(QStringLiteral("rect.x")).toInt(),
                             hash.value(QStringLiteral("rect.y")).toInt(),
                             hash.value(QStringLiteral("rect.width")).toInt(),
                             hash.value(QStringLiteral("rect.height")).toInt());

        findWindowFor(accessibleId);
        auto windowRect = s_accessibleIdToWindowGeometry.value(accessibleId);
        if (windowRect.isEmpty()) {
            qFatal() << "Failed to get window rect for" << accessibleId;
            return {};
        }

        // We translate the relative coordinates of the accessible to the global coordinates of the window
        auto rect = accessibleRect.translated(windowRect.topLeft());

        qDebug() << "windowRect" << windowRect;
        qDebug() << "accessibleRect" << accessibleRect;
        qDebug() << "accessibleRect translated" << accessibleRect.translated(windowRect.topLeft());
        return rect;
    }

Q_SIGNALS:
    void ready();

private:
    static QDBusConnection connection()
    {
        static const auto connection = QDBusConnection::connectToBus(QStringLiteral("unix:path=/run/user/60106/at-spi/bus_1"), QStringLiteral("a11y"));
        return connection;
    }

    static QString serviceName()
    {
        static const auto remoteService = []() -> auto {
            const auto serviceNames = connection().interface()->registeredServiceNames().value();
            const auto it = std::ranges::find_if(serviceNames, [](const auto &service) -> bool {
                const auto pid = connection().interface()->servicePid(service).value();
                return pid == qEnvironmentVariable("ATSPI_PID").toUInt();
            });
            if (it == serviceNames.cend()) {
                qFatal() << "Failed to find the correct pid to talk to" << qEnvironmentVariable("ATSPI_PID");
                return QString();
            }
            const auto &serviceName = *it;
            qDebug() << "Talking to service" << serviceName;
            return serviceName;
        }();
        return remoteService;
    }

    void mapAccessibleToWindowGeometry(const QString &accessibleId, const QString &uuid)
    {
        Q_ASSERT(m_windowManagement);
        // We don't know when our window will appear on the interface, so let's try a number of times.
        // A bit ugly but better than having to async chain things all the way through to the action construction.
        constexpr auto waitTime = 50ms;
        constexpr auto arbitraryTries = 5s / waitTime;
        for (auto i = arbitraryTries; i != 0; i--) {
            const auto windows = m_windowManagement->windows();
            for (const auto &window : windows) {
                if (window->title().startsWith(uuid)) {
                    s_accessibleIdToWindowGeometry.insert(accessibleId, window->geometryWithoutBorder());
                    return;
                }
            }
            QThread::sleep(waitTime);
            QCoreApplication::processEvents();
        }
        qFatal() << "Failed to resolve window for" << accessibleId;
    }

    void findWindowFor(const QString &accessibleId)
    {
        auto identifyWindowByTitle = QDBusMessage::createMethodCall(serviceName(), EXTENSION_PATH, EXTENSION_INTERFACE, QStringLiteral("identifyWindowByTitle"));
        identifyWindowByTitle << accessibleId;
        const QDBusReply<QString> identifyReply = connection().call(identifyWindowByTitle);
        if (!identifyReply.isValid()) {
            qFatal() << "Failed to get uuid caption";
            return;
        }
        const auto uuid = identifyReply.value();

        mapAccessibleToWindowGeometry(accessibleId, uuid);

        auto resetWindowTitle = QDBusMessage::createMethodCall(serviceName(), EXTENSION_PATH, EXTENSION_INTERFACE, QStringLiteral("resetWindowTitle"));
        resetWindowTitle << uuid;
        std::ignore = connection().call(resetWindowTitle);
    }

    std::unique_ptr<KWayland::Client::ConnectionThread> m_connection{KWayland::Client::ConnectionThread::fromApplication()};
    KWayland::Client::Registry m_registry;
    std::unique_ptr<KWayland::Client::PlasmaWindowManagement> m_windowManagement;
    static inline QMap<QString, QRect> s_accessibleIdToWindowGeometry;
};

void runActions(QFile &actionFile, GeometryResolver &geometryResolver)
{
    std::vector<BaseAction *> actions;

    const auto document = QJsonDocument::fromJson(actionFile.readAll());
    std::cout << document.toJson().toStdString();
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
                    const int x = hash.value(QStringLiteral("x")).toInt();
                    const int y = hash.value(QStringLiteral("y")).toInt();

                    // Positions relative to elements are ignored since at-spi2 can't report correct element positions.
                    const auto variantValue = hash.value(QStringLiteral("origin"));
                    if (variantValue.toString() == QLatin1String("pointer")) {
                        action->setPosition({x, y}, PointerAction::Origin::Pointer);
                    } else if (variantValue.toString() == QLatin1String("viewport")) {
                        action->setPosition({x, y}, PointerAction::Origin::Viewport);
                    } else {
                        const auto hash = variantValue.toHash();
                        const auto element = hash.value(QStringLiteral("element-6066-11e4-a52e-4f735466cecf")).toString();
                        if (element.isEmpty()) {
                            qFatal() << "Unsupported origin type" << hash.value(QStringLiteral("origin"));
                            continue;
                        }

                        // e.g. -org-a11y-atspi-accessible-2147483803
                        const auto elementId = element.mid(element.lastIndexOf(QLatin1Char('-')) + 1);

                        // WARNING
                        // Depends on
                        // plasma-wayland-protocols
                        // kwayland
                        // kwin
                        // plasma-integration

                        const auto rect = geometryResolver.globalGeometryFor(elementId);
                        if (rect.isEmpty()) {
                            qFatal() << "Failed to resolve global coordinates for accessible";
                            continue;
                        }

                        const auto origin = QRect(x, y, 0, 0).translated(rect.topLeft()).topLeft();
                        action->setPosition(origin, PointerAction::Origin::Viewport);
                    }
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

    qDebug() << "Performing actions";
    for (auto action : actions) {
        action->perform();
    }
    qDebug() << "Done performing actions";
    qDeleteAll(actions);
    QCoreApplication::quit();
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    const auto actionFilePath = qGuiApp->arguments().at(1);
    QFile actionFile(actionFilePath);
    if (!actionFile.open(QFile::ReadOnly)) {
        qWarning() << "failed to open action file" << actionFilePath;
        return 1;
    }

    GeometryResolver geometryResolver;
    s_interface = new FakeInputInterface;

    // Wait until all interfaces are up and running so we can start processing.
    unsigned int waitingForReady = 0;
    const auto onReady = [&waitingForReady, &actionFile, &geometryResolver] {
        Q_ASSERT(waitingForReady > 0);
        waitingForReady--;
        if (waitingForReady == 0) {
            runActions(actionFile, geometryResolver);
        }
    };

    app.connect(&geometryResolver, &GeometryResolver::ready, &app, onReady, Qt::QueuedConnection);
    waitingForReady++;

    app.connect(s_interface, &FakeInputInterface::readyChanged, &app, onReady, Qt::QueuedConnection);
    waitingForReady++;

    return app.exec();
}

#include "main.moc"
