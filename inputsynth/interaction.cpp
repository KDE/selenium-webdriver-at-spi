/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#include "interaction.h"

#include <ranges>
#include <span>

#include <linux/input-event-codes.h>

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

QHash<unsigned /* unique id */, QPoint> PointerAction::s_positions = {};
QSet<unsigned /*unique id*/> PointerAction::s_touchPoints = {};
QSet<int /* pressed button */> PointerAction::s_mouseButtons = {};

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

[[nodiscard]] unsigned getUniqueId(const QString &idStr)
{
    static unsigned lastId = 0;
    static QHash<QString, unsigned> table;
    if (auto it = table.find(idStr); it != table.end()) {
        return *it;
    }
    return *table.insert(idStr, lastId++);
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

void FakeInputInterface::roundtrip(bool touch)
{
    if (touch) {
        touch_frame();
    }
    wl_display_roundtrip(s_interface->m_display);
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

WheelAction::WheelAction(const QString &id, const QPoint &pos, const QPoint &deltaPos, unsigned long duration)
    : m_uniqueId(getUniqueId(id))
    , m_pos(pos)
    , m_deltaPos(deltaPos)
    , m_duration(duration)
{
}

WheelAction::~WheelAction()
{
}

void WheelAction::perform()
{
    PointerAction::s_positions[m_uniqueId] = m_pos;
    s_interface->pointer_motion_absolute(wl_fixed_from_int(m_pos.x()), wl_fixed_from_int(m_pos.y()));
    s_interface->roundtrip();

    if (m_deltaPos.x() != 0) {
        s_interface->axis(WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_int(m_deltaPos.x()));
        s_interface->roundtrip();
    }
    if (m_deltaPos.y() != 0) {
        s_interface->axis(WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(m_deltaPos.y()));
        s_interface->roundtrip();
    }

    QThread::msleep(m_duration);
}

PointerAction::PointerAction(PointerKind pointerType, const QString &id, ActionType actionType, Button button, unsigned long duration)
    : m_uniqueId(getUniqueId(id))
    , m_pointerType(pointerType)
    , m_actionType(actionType)
    , m_button(button)
    , m_duration(duration)
{
}

PointerAction::~PointerAction()
{
}

void PointerAction::setPosition(const QPoint &pos, Origin origin)
{
    m_pos = pos;
    m_origin = origin;
}

void PointerAction::perform()
{
    static const QHash<int, uint32_t> s_buttonMap = {
        {static_cast<int>(Button::Left), BTN_LEFT},
        {static_cast<int>(Button::Middle), BTN_MIDDLE},
        {static_cast<int>(Button::Right), BTN_RIGHT},
        {static_cast<int>(Button::Forward), BTN_FORWARD},
        {static_cast<int>(Button::Back), BTN_BACK},
    };

    switch (m_actionType) {
    case ActionType::Move: {
        auto lastPosIt = s_positions.find(m_uniqueId);
        if (m_pointerType == PointerKind::Mouse) {
            if (lastPosIt == s_positions.end()) {
                lastPosIt = s_positions.insert(m_uniqueId, QPoint(0, 0));
            }
        } else if (lastPosIt == s_positions.end() || !s_touchPoints.contains(m_uniqueId)) {
            // Save the initial position
            s_positions[m_uniqueId] = m_pos;
            return;
        }
        // Interpolate the trail based on the total duration
        constexpr double stepDurationMs = 50.0; // Can't be too short otherwise Qt will ignore some events
        int steps = 1;
        if (m_duration > stepDurationMs) {
            int xDiff = 0;
            int yDiff = 0;
            if (m_origin == Origin::Pointer) {
                xDiff = m_pos.x();
                yDiff = m_pos.y();
            } else {
                xDiff = m_pos.x() - lastPosIt->x();
                yDiff = m_pos.y() - lastPosIt->y();
            }

            // Calculate how many steps are going to be performed
            steps = std::ceil(m_duration / stepDurationMs);
            // Distance that advances in each step
            const int stepXDiff = std::lround(xDiff / double(steps));
            const int stepYDiff = std::lround(yDiff / double(steps));

            for (int i : std::views::iota(1, steps)) {
                const wl_fixed_t newX = wl_fixed_from_int(lastPosIt->x() + stepXDiff * i);
                const wl_fixed_t newY = wl_fixed_from_int(lastPosIt->y() + stepYDiff * i);
                if (m_pointerType == PointerKind::Touch) {
                    s_interface->touch_motion(m_uniqueId, newX, newY);
                } else {
                    s_interface->pointer_motion_absolute(newX, newY);
                }
                s_interface->roundtrip(m_pointerType == PointerKind::Touch);
                QThread::msleep(stepDurationMs);
            }
        }
        // Final round of move
        wl_fixed_t lastX;
        wl_fixed_t lastY;
        if (m_pointerType == PointerKind::Mouse) {
            lastX = wl_fixed_from_int(lastPosIt->x() + m_pos.x());
            lastY = wl_fixed_from_int(lastPosIt->y() + m_pos.y());
        } else {
            lastX = wl_fixed_from_int(m_pos.x());
            lastY = wl_fixed_from_int(m_pos.y());
        }
        if (m_pointerType == PointerKind::Touch) {
            s_interface->touch_motion(m_uniqueId, lastX, lastY);
        } else {
            s_interface->pointer_motion_absolute(lastX, lastY);
        }
        s_interface->roundtrip(m_pointerType == PointerKind::Touch);
        // Sleep to the total duration
        QThread::msleep(m_duration - (steps - 1) * stepDurationMs);
        // Update the last position
        *lastPosIt = m_pos;

        return;
    }

    case ActionType::Down: {
        QPoint lastPos;
        if (auto lastPosIt = s_positions.find(m_uniqueId); lastPosIt != s_positions.end()) {
            lastPos = *lastPosIt;
        } else {
            lastPos = {0, 0};
            s_positions[m_uniqueId] = lastPos;
        }

        if (m_pointerType == PointerKind::Touch) {
            if (s_touchPoints.contains(m_uniqueId)) {
                return;
            }
            s_touchPoints.insert(m_uniqueId);
            qDebug() << "sending touch_down at" << lastPos;
            s_interface->touch_down(m_uniqueId, wl_fixed_from_int(lastPos.x()), wl_fixed_from_int(lastPos.y()));
        } else {
            if (s_mouseButtons.contains(static_cast<int>(m_button))) {
                return;
            }
            s_mouseButtons.insert(static_cast<int>(m_button));
            qDebug() << "clicking at" << lastPos;
            s_interface->button(s_buttonMap[static_cast<int>(m_button)], WL_POINTER_BUTTON_STATE_PRESSED);
        }
        s_interface->roundtrip(m_pointerType == PointerKind::Touch);
        return;
    }
    case ActionType::Up: {
        if (m_pointerType == PointerKind::Touch) {
            if (s_touchPoints.remove(m_uniqueId)) {
                qDebug() << "sending touch_up";
                s_interface->touch_up(m_uniqueId);
            }
        } else {
            if (s_mouseButtons.remove(static_cast<int>(m_button))) {
                qDebug() << "releasing mouse button" << static_cast<int>(m_button);
                s_interface->button(s_buttonMap[static_cast<int>(m_button)], WL_POINTER_BUTTON_STATE_RELEASED);
            }
        }
        s_interface->roundtrip(m_pointerType == PointerKind::Touch);
        return;
    }
    case ActionType::Cancel: {
        if (m_pointerType == PointerKind::Touch) {
            if (!s_touchPoints.empty()) {
                s_interface->touch_cancel();
                s_touchPoints.clear();
            }
        } else {
            if (!s_mouseButtons.empty()) {
                for (auto button : s_mouseButtons) {
                    s_interface->button(s_buttonMap[button], WL_POINTER_BUTTON_STATE_RELEASED);
                }
                s_mouseButtons.clear();
            }
        }
        s_interface->roundtrip(m_pointerType == PointerKind::Touch);
        return;
    }
    }

    qWarning() << "Ignored an unknown action type" << static_cast<int>(m_actionType);
}

#include "moc_interaction.cpp"
