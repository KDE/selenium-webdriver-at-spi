/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#include "interface.h"

#include <ranges>
#include <span>

#include "qwayland-fake-input.h"
#include <QWaylandClientExtensionTemplate>
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#include <qpa/qplatformnativeinterface.h>
#endif

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>
#include <QGuiApplication>
#include <QScopeGuard>

#include "inputcapture.h"
#include <wayland-client-protocol.h>

InputEmulationInterface *s_interface;

namespace
{
// Magic offset stolen from kwin.
constexpr auto EVDEV_OFFSET = 8U;

struct LayoutNames {
    QString shortName;
    QString displayName;
    QString longName;
};

QDBusArgument &operator<<(QDBusArgument &argument, const LayoutNames &layoutNames)
{
    argument.beginStructure();
    argument << layoutNames.shortName << layoutNames.displayName << layoutNames.longName;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, LayoutNames &layoutNames)
{
    argument.beginStructure();
    argument >> layoutNames.shortName >> layoutNames.displayName >> layoutNames.longName;
    argument.endStructure();
    return argument;
}
}
Q_DECLARE_METATYPE(LayoutNames)

class FakeInputInterface : public QWaylandClientExtensionTemplate<FakeInputInterface>, public QtWayland::org_kde_kwin_fake_input
{
    Q_OBJECT

public:
    explicit FakeInputInterface(QObject *parent);
    ~FakeInputInterface() override;

    void roundtrip(bool touch = false);
    void sendKey(xkb_keysym_t keysym, wl_keyboard_key_state keyState);

    Q_DISABLE_COPY_MOVE(FakeInputInterface)

Q_SIGNALS:
    void initialized();

private:
    // The default layout used by KWin. This is either environment-defined (for nested KWins) or read from its DBus API
    // when dealing with a native KWin.
    QByteArray defaultLayout() const;
    // Resolve the modifiers required to produce a certain key.
    // XKB API is again a bit awkward here because it spits out modifier string names rather than codes or syms
    // so this function implicitly relies on loadModifiers() having first resolved modifiers to their stringy
    // representation.
    // Besides that it is straight forward. We request a modifier mask, check which modifiers are active in the mask
    // and based on that we can identifier the keycodes we need to press.
    void resolveModifiersForKey(xkb_keycode_t keycode, xkb_level_index_t level);

    wl_display *m_display = nullptr;

    QStringList m_modifiers;
    std::unique_ptr<xkb_context> m_context;
    xkb_rule_names m_ruleNames;
    std::unique_ptr<xkb_keymap> m_keymap;
    std::unique_ptr<xkb_state> m_state;
    xkb_layout_index_t m_layout;
    xkb_mod_index_t m_modCount;
    QMap<uint, QList<xkb_keycode_t>> m_modifierSymToCodes;
    QMap<QString, uint> m_modifierNameToSym;
};

FakeInputInterface::FakeInputInterface(QObject *parent)
    : QWaylandClientExtensionTemplate<FakeInputInterface>(ORG_KDE_KWIN_FAKE_INPUT_DESTROY_SINCE_VERSION)
    , m_context(xkb_context_new(XKB_CONTEXT_NO_FLAGS))
    , m_ruleNames({.rules = nullptr, .model = nullptr, .layout = defaultLayout().constData(), .variant = nullptr, .options = nullptr})
    , m_keymap(xkb_keymap_new_from_names(m_context.get(), &m_ruleNames, XKB_KEYMAP_COMPILE_NO_FLAGS))
    , m_state(xkb_state_new(m_keymap.get()))
    , m_layout(xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_EFFECTIVE))
    , m_modCount(xkb_keymap_num_mods(m_keymap.get()))
{
    setParent(parent);
    auto startAuth = [this]() {
        authenticate(QStringLiteral("inputsynth"), QStringLiteral("hello"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        m_display = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()->display();
#else
        m_display = static_cast<struct wl_display *>(qGuiApp->platformNativeInterface()->nativeResourceForIntegration("wl_display"));
#endif
        wl_display_roundtrip(m_display);

        Q_EMIT initialized();
    };

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    initialize();
    QMetaObject::invokeMethod(this, startAuth, Qt::QueuedConnection);
#else
    connect(this, &FakeInputInterface::activeChanged, this, startAuth);
#endif

    // Load the modifier keycodes. This walks all modifiers and maps them to keycodes. Effectively just resolving
    // that Alt is 123 and Ctrl is 456 etc.
    static constexpr auto modifierKeys = {XKB_KEY_Shift_L,
                                          XKB_KEY_Alt_L,
                                          XKB_KEY_Meta_L,
                                          XKB_KEY_Mode_switch,
                                          XKB_KEY_Super_L,
                                          XKB_KEY_Super_R,
                                          XKB_KEY_Hyper_L,
                                          XKB_KEY_Hyper_R,
                                          XKB_KEY_ISO_Level3_Shift,
                                          XKB_KEY_ISO_Level5_Shift};
    for (const auto &keycode : std::views::iota(xkb_keymap_min_keycode(m_keymap.get()), xkb_keymap_max_keycode(m_keymap.get()))) {
        for (const auto &level : std::views::iota(0U, xkb_keymap_num_levels_for_key(m_keymap.get(), keycode, m_layout))) {
            const xkb_keysym_t *syms = nullptr;
            uint num_syms = xkb_keymap_key_get_syms_by_level(m_keymap.get(), keycode, m_layout, level, &syms);
            for (const auto &sym : std::span{syms, num_syms}) {
                if (const auto it = std::ranges::find(modifierKeys, sym); it == modifierKeys.end()) {
                    continue;
                }

                m_modifierSymToCodes[sym].push_back(keycode - EVDEV_OFFSET);

                // The sym is a modifier. Find out which by pressing the key and checking which modifiers activate.
                xkb_state_update_key(m_state.get(), keycode, XKB_KEY_DOWN);
                auto up = qScopeGuard([this, &keycode] {
                    xkb_state_update_key(m_state.get(), keycode, XKB_KEY_UP);
                });

                for (const auto &mod : std::views::iota(0U, m_modCount)) {
                    if (xkb_state_mod_index_is_active(m_state.get(), mod, XKB_STATE_MODS_EFFECTIVE) <= 0) {
                        continue;
                    }
                    m_modifierNameToSym[QString::fromUtf8(xkb_keymap_mod_get_name(m_keymap.get(), mod))] = sym;
                    break;
                }
            }
        }
    }
}

FakeInputInterface::~FakeInputInterface()
{
    destroy();
}

void FakeInputInterface::roundtrip(bool touch)
{
    if (touch) {
        touch_frame();
    }
    wl_display_roundtrip(m_display);
}

void FakeInputInterface::sendKey(xkb_keysym_t keysym, wl_keyboard_key_state keyState)
{
    // Once we know our modifiers we can resolve the actual key by iterating the keysyms.
    xkb_keycode_t keycode = XKB_KEYCODE_INVALID;
    xkb_level_index_t level = XKB_LEVEL_INVALID;
    for (const auto &_keycode : std::views::iota(xkb_keymap_min_keycode(m_keymap.get()), xkb_keymap_max_keycode(m_keymap.get()))) {
        for (const auto &_level : std::views::iota(0U, xkb_keymap_num_levels_for_key(m_keymap.get(), _keycode, m_layout))) {
            const xkb_keysym_t *syms = nullptr;
            uint num_syms = xkb_keymap_key_get_syms_by_level(m_keymap.get(), _keycode, m_layout, _level, &syms);
            for (const auto &sym : std::span{syms, num_syms}) {
                if (sym != keysym) {
                    continue;
                }
                keycode = _keycode - EVDEV_OFFSET;
                level = _level;
                // We found the key. As a last step we'll need to resolve the modifiers required to trigger this
                // key. e.g. to produce 'A' we need to press the 'Shift' modifier before the 'a' key.
                resolveModifiersForKey(_keycode, level);
            }
        }
    }
    Q_ASSERT(keycode != XKB_KEYCODE_INVALID);

    std::vector<quint32> linuxModifiers;
    if (level > 0) {
        for (const auto &modifier : m_modifiers) {
            if (m_modifierNameToSym.contains(modifier)) {
                const auto modifierSym = m_modifierNameToSym.value(modifier);
                const auto modifierCodes = m_modifierSymToCodes.value(modifierSym);
                // Returning the first possible code only is a bit meh but seems to work fine so far.
                linuxModifiers.push_back(modifierCodes.at(0));
            }
        }
    }

    if (keyState == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (const auto &modifier : linuxModifiers) {
            qDebug() << "  pressing modifier" << modifier;
            keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_PRESSED);
            wl_display_roundtrip(m_display);
        }
    }

    qDebug() << "    key (state)" << keycode << keyState;
    keyboard_key(keycode, keyState);
    wl_display_roundtrip(m_display);

    if (keyState == WL_KEYBOARD_KEY_STATE_RELEASED) {
        for (const auto &modifier : linuxModifiers) {
            qDebug() << "  releasing modifier" << modifier;
            keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_RELEASED);
            wl_display_roundtrip(m_display);
        }
    }
}

QByteArray FakeInputInterface::defaultLayout() const
{
    static const auto layout = [] {
        if (qEnvironmentVariableIsSet("KWIN_XKB_DEFAULT_KEYMAP")) {
            auto layout = qgetenv("XKB_DEFAULT_LAYOUT");
            qDebug() << "synthesizing environment-influenced layout:" << layout;
            return layout;
        }

        // When running outside a nested kwin we'll need to follow whatever kwin has defined as layout.

        qDBusRegisterMetaType<LayoutNames>();
        qDBusRegisterMetaType<QList<LayoutNames>>();

        QDBusMessage layoutMessage = QDBusMessage::createMethodCall(QStringLiteral("org.kde.keyboard"),
                                                                    QStringLiteral("/Layouts"),
                                                                    QStringLiteral("org.kde.KeyboardLayouts"),
                                                                    QStringLiteral("getLayout"));
        QDBusReply<int> layoutReply = QDBusConnection::sessionBus().call(layoutMessage);
        if (!layoutReply.isValid()) {
            qWarning() << "Failed to get layout index" << layoutReply.error().message() << "defaulting to us";
            return QByteArrayLiteral("us");
        }
        const auto layoutIndex = layoutReply.value();

        QDBusMessage listMessage = QDBusMessage::createMethodCall(QStringLiteral("org.kde.keyboard"),
                                                                  QStringLiteral("/Layouts"),
                                                                  QStringLiteral("org.kde.KeyboardLayouts"),
                                                                  QStringLiteral("getLayoutsList"));
        QDBusReply<QList<LayoutNames>> listReply = QDBusConnection::sessionBus().call(listMessage);
        if (!listReply.isValid()) {
            qWarning() << "Failed to get layout list" << listReply.error().message() << "defaulting to us";
            return QByteArrayLiteral("us");
        }

        auto layout = listReply.value().at(layoutIndex).shortName.toUtf8();
        qDebug() << "synthesizing layout:" << layout;
        return layout;
    }();
    Q_ASSERT(!layout.isEmpty());
    return layout;
}

void FakeInputInterface::resolveModifiersForKey(xkb_keycode_t keycode, xkb_level_index_t level)
{
    static constexpr auto maxMasks = 1; // we only care about a single mask because we need only one way to access the key
    std::array<xkb_mod_mask_t, maxMasks> mask{};
    const auto maskSize = xkb_keymap_key_get_mods_for_level(m_keymap.get(), keycode, m_layout, level, mask.data(), mask.size());
    for (const auto &mask : std::span{mask.data(), maskSize}) {
        for (const auto &mod : std::views::iota(0U, m_modCount)) {
            if ((mask & (1 << mod)) == 0) {
                continue;
            }
            const auto name = xkb_keymap_mod_get_name(m_keymap.get(), mod);
            const auto qName = QString::fromUtf8(name);
            if (!m_modifiers.contains(qName)) {
                m_modifiers.push_back(qName);
            }
        }
    }
}

InputEmulationInterface::InputEmulationInterface()
    : QObject()
{
}

InputEmulationInterface::~InputEmulationInterface()
{
}

WaylandInterface::WaylandInterface()
    : InputEmulationInterface()
    , m_interface(new FakeInputInterface(this))
{
    connect(m_interface, &FakeInputInterface::initialized, this, &WaylandInterface::initialized);
}

void WaylandInterface::pointer_down(Button button)
{
    m_interface->button(m_buttonMap[button], WL_POINTER_BUTTON_STATE_PRESSED);
    m_interface->roundtrip();
}

void WaylandInterface::pointer_up(Button button)
{
    m_interface->button(m_buttonMap[button], WL_POINTER_BUTTON_STATE_RELEASED);
    m_interface->roundtrip();
}

void WaylandInterface::move_to_location(int x, int y)
{
    m_interface->pointer_motion_absolute(wl_fixed_from_int(x), wl_fixed_from_int(y));
    m_interface->roundtrip();
}

void WaylandInterface::touch_down(unsigned id, int x, int y)
{
    m_interface->touch_down(id, wl_fixed_from_int(x), wl_fixed_from_int(y));
    m_interface->roundtrip(true);
}

void WaylandInterface::touch_up(unsigned id)
{
    m_interface->touch_up(id);
    m_interface->roundtrip(true);
}

void WaylandInterface::touch_motion(unsigned id, int x, int y)
{
    m_interface->touch_motion(id, wl_fixed_from_int(x), wl_fixed_from_int(y));
    m_interface->roundtrip(true);
}

void WaylandInterface::scroll(int x, int y, int delta_x, int delta_y)
{
    move_to_location(x, y);

    if (delta_x != 0) {
        m_interface->axis(WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_int(delta_x));
        m_interface->roundtrip();
    }
    if (delta_y != 0) {
        m_interface->axis(WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(delta_y));
        m_interface->roundtrip();
    }
}

void WaylandInterface::key_down(QChar key)
{
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (auto it = m_charToKeyMap.constFind(key); it != m_charToKeyMap.cend()) {
        keysym = it.value();
    } else {
        keysym = xkb_utf32_to_keysym(key.unicode());
    }
    Q_ASSERT(keysym != XKB_KEY_NoSymbol);
    m_interface->sendKey(keysym, WL_KEYBOARD_KEY_STATE_PRESSED);
}

void WaylandInterface::key_up(QChar key)
{
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (auto it = m_charToKeyMap.constFind(key); it != m_charToKeyMap.cend()) {
        keysym = it.value();
    } else {
        keysym = xkb_utf32_to_keysym(key.unicode());
    }
    Q_ASSERT(keysym != XKB_KEY_NoSymbol);
    m_interface->sendKey(keysym, WL_KEYBOARD_KEY_STATE_RELEASED);
}

InputCaptureInterface::InputCaptureInterface()
    : InputEmulationInterface()
    , m_interface(new OrgFreedesktopPortalInputCaptureInterface(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                                QStringLiteral("/org/freedesktop/portal/desktop"),
                                                                QDBusConnection::sessionBus(),
                                                                this))
{
    QDBusPendingReply<QDBusObjectPath> reply = m_interface->CreateSession(QString(), QVariantMap());
    reply.waitForFinished();
    Q_ASSERT(reply.isValid());
    m_sessionHandle = reply.value();
}

InputCaptureInterface::~InputCaptureInterface()
{
}

void InputCaptureInterface::pointer_down(Button button)
{
}

void InputCaptureInterface::pointer_up(Button button)
{
}

void InputCaptureInterface::move_to_location(int x, int y)
{
}

void InputCaptureInterface::scroll(int x, int y, int delta_x, int delta_y)
{
}

void InputCaptureInterface::key_down(QChar key)
{
}

void InputCaptureInterface::key_up(QChar key)
{
}

#include "interface.moc"
#include "moc_interface.cpp"
