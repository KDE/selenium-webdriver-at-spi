/*
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
    SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>
 */

#include "interaction.h"

#include <ranges>
#include <span>

#include <QDebug>
#include <QThread>

QHash<unsigned /* unique id */, QPoint> PointerAction::s_positions = {};
QSet<unsigned /*unique id*/> PointerAction::s_touchPoints = {};
QSet<int /* pressed button */> PointerAction::s_mouseButtons = {};

namespace
{
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

BaseAction::BaseAction()
{
}

BaseAction::~BaseAction()
{
}

KeyboardAction::KeyboardAction(QChar key, wl_keyboard_key_state keyState)
    : BaseAction()
    , m_key(key)
    , m_keyState(keyState)
{
}

KeyboardAction::~KeyboardAction()
{
}

void KeyboardAction::perform()
{
    if (m_keyState == WL_KEYBOARD_KEY_STATE_PRESSED) {
        qDebug() << "sending key_down" << m_key;
        s_interface->key_down(m_key);
    } else {
        qDebug() << "sending key_up" << m_key;
        s_interface->key_up(m_key);
    }
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
    qDebug() << "sending scroll" << m_pos << m_deltaPos;
    PointerAction::s_positions[m_uniqueId] = m_pos;
    s_interface->scroll(m_pos.x(), m_pos.y(), m_deltaPos.x(), m_deltaPos.y());
    QThread::msleep(m_duration);
}

PointerAction::PointerAction(PointerKind pointerType, const QString &id, ActionType actionType, InputEmulationInterface::Button button, unsigned long duration)
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
        constexpr double stepDurationMs = 16.0; // Can't be too short otherwise Qt will ignore some events
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
                const int newX = lastPosIt->x() + stepXDiff * i;
                const int newY = lastPosIt->y() + stepYDiff * i;
                if (m_pointerType == PointerKind::Touch) {
                    s_interface->touch_motion(m_uniqueId, newX, newY);
                } else {
                    s_interface->move_to_location(newX, newY);
                }
                QThread::msleep(stepDurationMs);
            }
        }
        // Final round of move
        const int lastX = m_pos.x();
        const int lastY = m_pos.y();
        if (m_pointerType == PointerKind::Touch) {
            s_interface->touch_motion(m_uniqueId, lastX, lastY);
        } else {
            s_interface->move_to_location(lastX, lastY);
        }
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
            s_interface->touch_down(m_uniqueId, lastPos.x(), lastPos.y());
        } else {
            if (s_mouseButtons.contains(static_cast<int>(m_button))) {
                return;
            }
            s_mouseButtons.insert(static_cast<int>(m_button));
            qDebug() << "clicking at" << lastPos;
            s_interface->pointer_down(m_button);
        }
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
                s_interface->pointer_up(m_button);
            }
        }
        return;
    }
    }

    qWarning() << "Ignored an unknown action type" << static_cast<int>(m_actionType);
}

#include "moc_interaction.cpp"
