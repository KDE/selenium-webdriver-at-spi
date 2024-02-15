/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#pragma once

#include <QDBusObjectPath>
#include <QHash>
#include <QMap>
#include <QObject>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

class FakeInputInterface;
class OrgFreedesktopPortalInputCaptureInterface;

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
}

class InputEmulationInterface : public QObject
{
    Q_OBJECT
public:
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

    explicit InputEmulationInterface();
    virtual ~InputEmulationInterface() override;

    // From selenium.webdriver.common.actions.pointer_actions
    virtual void pointer_down([[maybe_unused]] Button button)
    {
    }
    virtual void pointer_up([[maybe_unused]] Button button)
    {
    }
    virtual void move_to_location([[maybe_unused]] int x, [[maybe_unused]] int y)
    {
    }

    virtual void touch_down([[maybe_unused]] unsigned id, [[maybe_unused]] int x, [[maybe_unused]] int y)
    {
    }
    virtual void touch_up([[maybe_unused]] unsigned id)
    {
    }
    virtual void touch_motion([[maybe_unused]] unsigned id, [[maybe_unused]] int x, [[maybe_unused]] int y)
    {
    }

    // From selenium.webdriver.common.actions.wheel_actions
    virtual void scroll([[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int delta_x, [[maybe_unused]] int delta_y)
    {
    }

    // From selenium.webdriver.common.actions.key_actions
    virtual void key_down([[maybe_unused]] QChar key)
    {
    }
    virtual void key_up([[maybe_unused]] QChar key)
    {
    }

    Q_DISABLE_COPY_MOVE(InputEmulationInterface)

    const QHash<Button, uint32_t> m_buttonMap = {
        {Button::Left, BTN_LEFT},
        {Button::Middle, BTN_MIDDLE},
        {Button::Right, BTN_RIGHT},
        {Button::Forward, BTN_FORWARD},
        {Button::Back, BTN_BACK},
    };

    const QHash<QChar, xkb_keysym_t> m_charToKeyMap{
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

Q_SIGNALS:
    void initialized();
};

inline uint qHash(InputEmulationInterface::Button key, uint seed)
{
    return ::qHash(static_cast<uint>(key), seed);
}

class WaylandInterface : public InputEmulationInterface
{
    Q_OBJECT
public:
    explicit WaylandInterface();
    ~WaylandInterface() override = default;

    void pointer_down(Button button) override;
    void pointer_up(Button button) override;
    void move_to_location(int x, int y) override;

    void touch_down(unsigned id, int x, int y) override;
    void touch_up(unsigned id) override;
    void touch_motion(unsigned id, int x, int y) override;

    void scroll(int x, int y, int delta_x, int delta_y) override;

    void key_down(QChar key) override;
    void key_up(QChar key) override;

private:
    FakeInputInterface *m_interface;
};

class InputCaptureInterface : public InputEmulationInterface
{
    Q_OBJECT
public:
    explicit InputCaptureInterface();
    ~InputCaptureInterface() override;

    void pointer_down(Button button) override;
    void pointer_up(Button button) override;
    void move_to_location(int x, int y) override;

    void scroll(int x, int y, int delta_x, int delta_y) override;

    void key_down(QChar key) override;
    void key_up(QChar key) override;

private:
    OrgFreedesktopPortalInputCaptureInterface *m_interface;
    QDBusObjectPath m_sessionHandle;
};

extern InputEmulationInterface *s_interface;
