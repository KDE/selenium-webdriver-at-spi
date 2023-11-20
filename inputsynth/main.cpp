// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include "qwayland-fake-input.h"

#include <ranges>
#include <span>

#include <xkbcommon/xkbcommon.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QThread>
#include <QWaylandClientExtensionTemplate>
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#include <qpa/qplatformnativeinterface.h>
#endif

#include <wayland-client-protocol.h>

namespace std
{
template<>
struct default_delete<xkb_context> {
    void operator()(xkb_context *ptr) const
    {
        xkb_context_unref(ptr);
    }
};

template<>
struct default_delete<xkb_keymap> {
    void operator()(xkb_keymap *ptr) const
    {
        xkb_keymap_unref(ptr);
    }
};

template<>
struct default_delete<xkb_state> {
    void operator()(xkb_state *ptr) const
    {
        xkb_state_unref(ptr);
    }
};
} // namespace std

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

class KeyboardAction
{
    // The default layout used by KWin. This is either environment-defined (for nested KWins) or read from its DBus API
    // when dealing with a native KWin.
    static QByteArray defaultLayout()
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

    void loadModifiers()
    {
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

    // Resolve the modifiers required to produce a certain key.
    // XKB API is again a bit awkward here because it spits out modifier string names rather than codes or syms
    // so this function implicitly relies on loadModifiers() having first resolved modifiers to their stringy
    // representation.
    // Besides that it is straight forward. We request a modifier mask, check which modifiers are active in the mask
    // and based on that we can identifier the keycodes we need to press.
    void resolveModifiersForKey(xkb_keycode_t keycode)
    {
        static constexpr auto maxMasks = 1; // we only care about a single mask because we need only one way to access the key
        std::array<xkb_mod_mask_t, maxMasks> mask{};
        const auto maskSize = xkb_keymap_key_get_mods_for_level(m_keymap.get(), keycode, m_layout, m_level, mask.data(), mask.size());
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

    xkb_keysym_t charToKeysym(const QChar &key)
    {
        // A bit awkward but not all keys manage to map via xkb_utf32_to_keysym so we augment the lookup.
        if (key == QChar(u'\ue010')) {
            return XKB_KEY_End;
        }
        if (key == QChar(u'\ue009')) {
            return XKB_KEY_Control_L;
        }
        if (key == QChar(u'\ue007')) {
            return XKB_KEY_Return;
        }

        return xkb_utf32_to_keysym(key.unicode());
    }

    xkb_keysym_t m_keysym = XKB_KEY_NoSymbol;
    xkb_keycode_t m_keycode = XKB_KEYCODE_INVALID;
    xkb_level_index_t m_level = XKB_LEVEL_INVALID;
    QStringList m_modifiers;

    std::unique_ptr<xkb_context> m_context;
    xkb_rule_names m_ruleNames;
    std::unique_ptr<xkb_keymap> m_keymap;
    std::unique_ptr<xkb_state> m_state;
    xkb_layout_index_t m_layout;

    xkb_mod_index_t m_modCount;

    QMap<uint, QList<xkb_keycode_t>> m_modifierSymToCodes;
    QMap<QString, uint> m_modifierNameToSym;

    wl_keyboard_key_state m_keyState;

public:
    /**
     * @brief Construct a new Keyboard Action object
     *
     * @param key the QChar to construct the action for
     *
     * The way this works is a bit complicated. Because we tell KWin which keys to press based on linux key codes,
     * we effectively have to resolve the actual keys that needs pressing to generate the character on a given layout.
     * When running a nested KWin that is always the us layout because we set KWIN_XKB_DEFAULT_KEYMAP (which forces
     * KWin to follow environment-defined XKB variables).
     * When not running nested, things get even more complicated because KWin follows the user's layout which may
     * be anything.
     *
     * So we end up resolving keycodes through XKB...
     * XKB resolution entails iterating all levels in all keycodes to look at all keysyms and eventually find the one
     * we are looking for. It's a bit verbose but it is what it is.
     */
    explicit KeyboardAction(const QChar &key, wl_keyboard_key_state keyState)
        : m_keysym(charToKeysym(key))
        , m_context(xkb_context_new(XKB_CONTEXT_NO_FLAGS))
        , m_ruleNames({.rules = nullptr, .model = nullptr, .layout = defaultLayout().constData(), .variant = nullptr, .options = nullptr})
        , m_keymap(xkb_keymap_new_from_names(m_context.get(), &m_ruleNames, XKB_KEYMAP_COMPILE_NO_FLAGS))
        , m_state(xkb_state_new(m_keymap.get()))
        , m_layout(xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_EFFECTIVE))
        , m_modCount(xkb_keymap_num_mods(m_keymap.get()))
        , m_keyState(keyState)
    {
        Q_ASSERT(!defaultLayout().isEmpty());
        Q_ASSERT(m_keysym != XKB_KEY_NoSymbol);

        qDebug() << "looking for keysym" << m_keysym << "for char" << key;

        // Load the modifier keycodes. This walks all modifiers and maps them to keycodes. Effectively just resolving
        // that Alt is 123 and Ctrl is 456 etc.
        loadModifiers();

        // Once we know our modifiers we can resolve the actual key by iterating the keysyms.
        for (const auto &keycode : std::views::iota(xkb_keymap_min_keycode(m_keymap.get()), xkb_keymap_max_keycode(m_keymap.get()))) {
            for (const auto &level : std::views::iota(0U, xkb_keymap_num_levels_for_key(m_keymap.get(), keycode, m_layout))) {
                const xkb_keysym_t *syms = nullptr;
                uint num_syms = xkb_keymap_key_get_syms_by_level(m_keymap.get(), keycode, m_layout, level, &syms);
                for (const auto &sym : std::span{syms, num_syms}) {
                    if (sym != m_keysym) {
                        continue;
                    }
                    qWarning() << "found keysym";
                    m_keycode = keycode - EVDEV_OFFSET;
                    m_level = level;
                    // We found the key. As a last step we'll need to resolve the modifiers required to trigger this
                    // key. e.g. to produce 'A' we need to press the 'Shift' modifier before the 'a' key.
                    resolveModifiersForKey(keycode);
                }
            }
        }
        Q_ASSERT(m_keycode != XKB_KEYCODE_INVALID);
    }

    [[nodiscard]] quint32 linuxKeyCode() const
    {
        return m_keycode;
    }

    [[nodiscard]] wl_keyboard_key_state keyState() const
    {
        return m_keyState;
    }

    [[nodiscard]] std::vector<quint32> linuxModifiers() const
    {
        if (m_level == 0) {
            return {};
        }

        qDebug() << m_modifiers;
        std::vector<quint32> ret;
        for (const auto &modifier : m_modifiers) {
            if (m_modifierNameToSym.contains(modifier)) {
                const auto modifierSym = m_modifierNameToSym.value(modifier);
                const auto modifierCodes = m_modifierSymToCodes.value(modifierSym);
                // Returning the first possible code only is a bit meh but seems to work fine so far.
                ret.push_back(modifierCodes.at(0));
            }
        }
        if (ret.empty()) {
            qCritical("Unknown level!");
            return {};
        }
        return ret;
    }
};

class FakeInputInterface : public QWaylandClientExtensionTemplate<FakeInputInterface>, public QtWayland::org_kde_kwin_fake_input
{
public:
    explicit FakeInputInterface(std::vector<KeyboardAction> &&actions)
        : QWaylandClientExtensionTemplate<FakeInputInterface>(ORG_KDE_KWIN_FAKE_INPUT_DESTROY_SINCE_VERSION)
        , m_actions(std::move(actions))
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        initialize();
        QMetaObject::invokeMethod(this, &FakeInputInterface::sendKey, Qt::QueuedConnection);
#else
        connect(this, &FakeInputInterface::activeChanged, this, &FakeInputInterface::sendKey);
#endif
    }

    ~FakeInputInterface() override
    {
        destroy();
    }

    Q_DISABLE_COPY_MOVE(FakeInputInterface)

private Q_SLOTS:
    void sendKey()
    {
        authenticate(QStringLiteral("inputsynth"), QStringLiteral("hello"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        auto display = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()->display();
#else
        auto display = static_cast<struct wl_display *>(qGuiApp->platformNativeInterface()->nativeResourceForIntegration("wl_display"));
#endif
        wl_display_roundtrip(display);

        for (const auto &action : m_actions) {
            for (const auto &modifier : action.linuxModifiers()) {
                qDebug() << "  pressing modifier" << modifier;
                keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_PRESSED);
                wl_display_roundtrip(display);
            }

            qDebug() << "    key (state)" << action.linuxKeyCode() << action.keyState();
            keyboard_key(action.linuxKeyCode(), action.keyState());
            wl_display_roundtrip(display);

            for (const auto &modifier : action.linuxModifiers()) {
                qDebug() << "  releasing modifier" << modifier;
                keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_RELEASED);
                wl_display_roundtrip(display);
            }
        }

        qGuiApp->quit();
    }

    std::vector<KeyboardAction> m_actions;
};
} // namespace

Q_DECLARE_METATYPE(LayoutNames)

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    const auto actionFilePath = qGuiApp->arguments().at(1);
    QFile actionFile(actionFilePath);
    if (!actionFile.open(QFile::ReadOnly)) {
        qWarning() << "failed to open action file" << actionFilePath;
        return 1;
    }

    std::vector<KeyboardAction> actions;

    auto typeToKeyState = [](const QVariantHash &hash) -> std::optional<wl_keyboard_key_state> {
        const auto type = hash.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("keyDown")) {
            return WL_KEYBOARD_KEY_STATE_PRESSED;
        }
        if (type == QStringLiteral("keyUp")) {
            return WL_KEYBOARD_KEY_STATE_RELEASED;
        }
        qWarning() << "unsupported keyboard action type" << type;
        qApp->quit();
        return {};
    };

    const auto document = QJsonDocument::fromJson(actionFile.readAll());
    const auto jsonObject = document.object();
    const auto jsonActions = jsonObject.value(QStringLiteral("actions")).toArray();
    for (const auto &jsonActionSet : jsonActions) {
        if (jsonActionSet[QStringLiteral("type")] != QStringLiteral("key")) {
            qWarning() << "unsupported action type" << jsonActionSet;
            continue;
        }
        for (const auto &jsonAction : jsonActionSet[QStringLiteral("actions")].toArray())  {
            const auto hash = jsonAction.toObject().toVariantHash();

            const auto string = hash.value(QStringLiteral("value")).toString();
            const QChar *character = string.unicode();
            actions.emplace_back(*character, typeToKeyState(hash).value_or(WL_KEYBOARD_KEY_STATE_RELEASED));
        }
    }

    FakeInputInterface input(std::move(actions));

    return app.exec();
}
