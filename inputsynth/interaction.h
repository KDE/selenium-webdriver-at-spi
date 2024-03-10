/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#pragma once

#include <memory>

#include "qwayland-fake-input.h"
#include <QHash>
#include <QMap>
#include <QPoint>
#include <QWaylandClientExtensionTemplate>
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#include <qpa/qplatformnativeinterface.h>
#endif

#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

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

class FakeInputInterface : public QWaylandClientExtensionTemplate<FakeInputInterface>, public QtWayland::org_kde_kwin_fake_input
{
    Q_OBJECT

public:
    explicit FakeInputInterface();
    ~FakeInputInterface() override;

    void roundtrip(bool touch = false);
    void sendKey(const std::vector<quint32> &linuxModifiers, quint32 linuxKeyCode, wl_keyboard_key_state keyState);

    Q_DISABLE_COPY_MOVE(FakeInputInterface)

Q_SIGNALS:
    void readyChanged();

private:
    bool m_ready = false;
    wl_display *m_display = nullptr;
};

extern FakeInputInterface *s_interface;

class BaseAction
{
public:
    explicit BaseAction();
    virtual ~BaseAction();

    virtual void perform() = 0;

    Q_DISABLE_COPY_MOVE(BaseAction)
};

class KeyboardAction : public BaseAction
{
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
    explicit KeyboardAction(const QChar &key, wl_keyboard_key_state keyState);
    ~KeyboardAction() override;

    void perform() override;

    std::vector<quint32> linuxModifiers() const;

private:
    // The default layout used by KWin. This is either environment-defined (for nested KWins) or read from its DBus API
    // when dealing with a native KWin.
    QByteArray defaultLayout() const;

    void loadModifiers();

    // Resolve the modifiers required to produce a certain key.
    // XKB API is again a bit awkward here because it spits out modifier string names rather than codes or syms
    // so this function implicitly relies on loadModifiers() having first resolved modifiers to their stringy
    // representation.
    // Besides that it is straight forward. We request a modifier mask, check which modifiers are active in the mask
    // and based on that we can identifier the keycodes we need to press.
    void resolveModifiersForKey(xkb_keycode_t keycode);

    xkb_keysym_t charToKeysym(const QChar &key);

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
};

class PauseAction : public BaseAction
{
public:
    explicit PauseAction(unsigned long duration);
    ~PauseAction() override;

    void perform() override;

private:
    unsigned long m_duration = 0;
};

class WheelAction : public BaseAction
{
public:
    explicit WheelAction(const QString &id, const QPoint &pos, const QPoint &deltaPos, unsigned long duration);
    ~WheelAction() override;

    void perform() override;

private:
    unsigned m_uniqueId;
    QPoint m_pos;
    QPoint m_deltaPos;
    unsigned long m_duration = 0;
};

class PointerAction : public BaseAction
{
public:
    // https://github.com/SeleniumHQ/selenium/blob/6620bce4e8e9da1fee3ec5a5547afa7dece3f80e/py/selenium/webdriver/common/actions/interaction.py#L30
    enum class PointerKind {
        Mouse,
        Touch,
        Pen,
    };

    enum class ActionType {
        Move,
        Down,
        Up,
        Cancel,
    };

    enum class Button {
        Left = 0,
        Touch = 0,
        PenContact = 0,
        Middle = 1,
        Right = 2,
        PenBarrel = 2,
        X1 = 3,
        Back = 3,
        X2 = 4,
        Forward = 4,
    };

    enum class Origin {
        Viewport,
        Pointer,
    };

    explicit PointerAction(PointerKind pointerType, const QString &id, ActionType actionType, Button button, unsigned long duration);
    ~PointerAction() override;

    void setPosition(const QPoint &pos, Origin origin);
    void perform() override;

private:
    static QHash<unsigned /* unique id */, QPoint> s_positions;
    static QSet<unsigned /*unique id*/> s_touchPoints;
    static QSet<int /* pressed button */> s_mouseButtons;

    unsigned m_uniqueId;
    PointerKind m_pointerType = PointerKind::Touch;
    ActionType m_actionType = ActionType::Move;
    Button m_button = Button::Left;
    QPoint m_pos;
    Origin m_origin = Origin::Viewport;
    unsigned long m_duration = 0;

    friend class WheelAction;
};
