/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#include "interaction.h"

#include <ranges>
#include <span>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QThread>

FakeInputInterface *s_interface;

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
} // namespace

Q_DECLARE_METATYPE(LayoutNames)

FakeInputInterface::FakeInputInterface()
    : QWaylandClientExtensionTemplate<FakeInputInterface>(ORG_KDE_KWIN_FAKE_INPUT_DESTROY_SINCE_VERSION)
{
    auto startAuth = [this]() {
        authenticate(QStringLiteral("inputsynth"), QStringLiteral("hello"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        m_display = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()->display();
#else
        m_display = static_cast<struct wl_display *>(qGuiApp->platformNativeInterface()->nativeResourceForIntegration("wl_display"));
#endif
        wl_display_roundtrip(m_display);

        Q_EMIT readyChanged();
    };

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    initialize();
    QMetaObject::invokeMethod(this, startAuth, Qt::QueuedConnection);
#else
    connect(this, &FakeInputInterface::activeChanged, this, startAuth);
#endif
}

FakeInputInterface::~FakeInputInterface()
{
}

void FakeInputInterface::sendKey(const std::vector<quint32> &linuxModifiers, quint32 linuxKeyCode, wl_keyboard_key_state keyState)
{
    for (const auto &modifier : linuxModifiers) {
        qDebug() << "  pressing modifier" << modifier;
        keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_display_roundtrip(m_display);
    }

    qDebug() << "    key (state)" << linuxKeyCode << keyState;
    keyboard_key(linuxKeyCode, keyState);
    wl_display_roundtrip(m_display);

    for (const auto &modifier : linuxModifiers) {
        qDebug() << "  releasing modifier" << modifier;
        keyboard_key(modifier, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_roundtrip(m_display);
    }
}

BaseAction::BaseAction()
{
}

BaseAction::~BaseAction()
{
}

KeyboardAction::KeyboardAction(const QChar &key, wl_keyboard_key_state keyState)
    : BaseAction()
    , m_keysym(charToKeysym(key))
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

KeyboardAction::~KeyboardAction()
{
}

void KeyboardAction::perform()
{
    s_interface->sendKey(linuxModifiers(), m_keycode, m_keyState);
}

[[nodiscard]] std::vector<quint32> KeyboardAction::linuxModifiers() const
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

QByteArray KeyboardAction::defaultLayout() const
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

void KeyboardAction::loadModifiers()
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

void KeyboardAction::resolveModifiersForKey(xkb_keycode_t keycode)
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

xkb_keysym_t KeyboardAction::charToKeysym(const QChar &key)
{
    // A bit awkward but not all keys manage to map via xkb_utf32_to_keysym so we augment the lookup.
    // https://www.selenium.dev/selenium/docs/api/py/webdriver/selenium.webdriver.common.keys.html#selenium.webdriver.common.keys.Keys.ARROW_LEFT
    static const QHash<QChar, xkb_keysym_t> charToKeyMap{
        {QChar(u'\ue025'), XKB_KEY_plus},      {QChar(u'\ue00a'), XKB_KEY_Alt_L},
        {QChar(u'\ue015'), XKB_KEY_Down},      {QChar(u'\ue012'), XKB_KEY_Left},
        {QChar(u'\ue014'), XKB_KEY_Right},     {QChar(u'\ue013'), XKB_KEY_Up},
        {QChar(u'\ue003'), XKB_KEY_BackSpace}, {QChar(u'\ue001'), XKB_KEY_Cancel},
        {QChar(u'\ue005'), XKB_KEY_Clear},     {QChar(u'\ue009'), XKB_KEY_Control_L},
        {QChar(u'\ue028'), XKB_KEY_period},    {QChar(u'\ue017'), XKB_KEY_Delete},
        {QChar(u'\ue029'), XKB_KEY_slash},     {QChar(u'\ue010'), XKB_KEY_End},
        {QChar(u'\ue007'), XKB_KEY_KP_Enter},  {QChar(u'\ue019'), XKB_KEY_equal},
        {QChar(u'\ue00c'), XKB_KEY_Escape},    {QChar(u'\ue031'), XKB_KEY_F1},
        {QChar(u'\ue03a'), XKB_KEY_F10},       {QChar(u'\ue03b'), XKB_KEY_F11},
        {QChar(u'\ue03c'), XKB_KEY_F12},       {QChar(u'\ue032'), XKB_KEY_F2},
        {QChar(u'\ue033'), XKB_KEY_F3},        {QChar(u'\ue034'), XKB_KEY_F4},
        {QChar(u'\ue035'), XKB_KEY_F5},        {QChar(u'\ue036'), XKB_KEY_F6},
        {QChar(u'\ue037'), XKB_KEY_F7},        {QChar(u'\ue038'), XKB_KEY_F8},
        {QChar(u'\ue039'), XKB_KEY_F9},        {QChar(u'\ue002'), XKB_KEY_Help},
        {QChar(u'\ue011'), XKB_KEY_Home},      {QChar(u'\ue016'), XKB_KEY_Insert},
        {QChar(u'\ue008'), XKB_KEY_Shift_L},   {QChar(u'\ue03d'), XKB_KEY_Meta_L},
        {QChar(u'\ue024'), XKB_KEY_multiply},  {QChar(u'\ue000'), XKB_KEY_NoSymbol},
        {QChar(u'\ue01a'), XKB_KEY_KP_0},      {QChar(u'\ue01b'), XKB_KEY_KP_1},
        {QChar(u'\ue01c'), XKB_KEY_KP_2},      {QChar(u'\ue01d'), XKB_KEY_KP_3},
        {QChar(u'\ue01e'), XKB_KEY_KP_4},      {QChar(u'\ue01f'), XKB_KEY_KP_5},
        {QChar(u'\ue020'), XKB_KEY_KP_6},      {QChar(u'\ue021'), XKB_KEY_KP_7},
        {QChar(u'\ue022'), XKB_KEY_KP_8},      {QChar(u'\ue023'), XKB_KEY_KP_9},
        {QChar(u'\ue00f'), XKB_KEY_Page_Down}, {QChar(u'\ue00e'), XKB_KEY_Page_Up},
        {QChar(u'\ue00b'), XKB_KEY_Pause},     {QChar(u'\ue006'), XKB_KEY_Return},
        {QChar(u'\ue018'), XKB_KEY_semicolon}, {QChar(u'\ue026'), XKB_KEY_comma},
        {QChar(u'\ue00d'), XKB_KEY_space},     {QChar(u'\ue027'), XKB_KEY_minus},
        {QChar(u'\ue004'), XKB_KEY_Tab},       {QChar(u'\ue040'), XKB_KEY_Zenkaku_Hankaku},
    };

    if (auto it = charToKeyMap.constFind(key); it != charToKeyMap.cend()) {
        return it.value();
    }

    return xkb_utf32_to_keysym(key.unicode());
}

PauseAction::PauseAction(unsigned long duration)
    : BaseAction()
    , m_duration(duration)
{
}

PauseAction::~PauseAction()
{
}

void PauseAction::perform()
{
    QThread::msleep(m_duration);
}

#include "moc_interaction.cpp"
