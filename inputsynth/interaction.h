/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#pragma once

#include <memory>

#include <QHash>
#include <QPoint>

#include <wayland-client-protocol.h>

#include "interface.h"

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
    explicit KeyboardAction(QChar key, wl_keyboard_key_state keyState);
    ~KeyboardAction() override;

    void perform() override;

    std::vector<quint32> linuxModifiers() const;

private:
    QChar m_key;
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
    };

    enum class Origin {
        Viewport,
        Pointer,
    };

    explicit PointerAction(PointerKind pointerType, const QString &id, ActionType actionType, InputEmulationInterface::Button button, unsigned long duration);
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
    InputEmulationInterface::Button m_button = InputEmulationInterface::Button::Left;
    QPoint m_pos;
    Origin m_origin = Origin::Viewport;
    unsigned long m_duration = 0;

    friend class WheelAction;
};
