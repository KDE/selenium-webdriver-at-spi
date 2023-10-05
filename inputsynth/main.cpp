// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include "qwayland-fake-input.h"
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QWaylandClientExtensionTemplate>
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#include <qpa/qplatformnativeinterface.h>
#endif

#include <wayland-client-protocol.h>

namespace
{
constexpr auto KEY_LEFTSHIFT = 42;
constexpr auto KEY_LEFTALT = 56;
constexpr auto KEY_LEFTCTRL = 29;

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

class FakeInputInterface : public QWaylandClientExtensionTemplate<FakeInputInterface>, public QtWayland::org_kde_kwin_fake_input
{
public:
    FakeInputInterface()
        : QWaylandClientExtensionTemplate<FakeInputInterface>(ORG_KDE_KWIN_FAKE_INPUT_DESTROY_SINCE_VERSION)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        initialize();
        QMetaObject::invokeMethod(this, &FakeInputInterface::sendKey, Qt::QueuedConnection);
#else
        connect(this, &FakeInputInterface::activeChanged, this, &FakeInputInterface::sendKey);
#endif
    }

    ~FakeInputInterface()
    {
        destroy();
    }

    Q_DISABLE_COPY_MOVE(FakeInputInterface)

private Q_SLOTS:
    void sendKey();
};
} // namespace

void FakeInputInterface::sendKey()
{
    authenticate(QStringLiteral("inputsynth"), QStringLiteral("hello"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto display = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()->display();
#else
    auto display = static_cast<struct wl_display *>(qGuiApp->platformNativeInterface()->nativeResourceForIntegration("wl_display"));
#endif
    wl_display_roundtrip(display);

    for (const auto &action : actions) {
        for (const auto &modifier : action.linuxModifiers()) {
            qDebug() << "  pressing modifier" << modifier;
            keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_PRESSED);
            wl_display_roundtrip(display);
        }

        qDebug() << "    pressing key" << action.linuxKeyCode();
        keyboard_key(action.linuxKeyCode(), WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_display_roundtrip(display);
        keyboard_key(action.linuxKeyCode(), WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_roundtrip(display);

        for (const auto &modifier : action.linuxModifiers()) {
            qDebug() << "  releasing modifier" << modifier;
            keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_RELEASED);
            wl_display_roundtrip(display);
        }
    }

    qGuiApp->quit();
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

    const auto document = QJsonDocument::fromJson(actionFile.readAll());
    const auto jsonActions = document.array();
    for (const auto &jsonAction : jsonActions) {
        const auto hash = jsonAction.toObject().toVariantHash();
        if (hash.value(QStringLiteral("type")).toString() != QStringLiteral("keyboard")) {
            qWarning() << "unsupported action type" << jsonAction;
            return 1;
        }
        actions.push_back({.level = hash.value(QStringLiteral("level")).toUInt(), .keycode = hash.value(QStringLiteral("keycode")).toUInt()});
    }

    auto input = std::make_unique<FakeInputInterface>();

    return app.exec();
}
