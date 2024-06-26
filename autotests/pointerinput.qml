// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>

import QtQuick 2.15
import QtQuick.Window 2.15

Item {
    width: Screen.width
    height: Screen.height

    TextInput {
        id: result
        Accessible.name: "result"
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onTapped: result.text = "mouse left"
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onTapped: result.text = "mouse right"
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onTapped: result.text = "mouse middle"
    }

    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen
        onTapped: result.text = "touchscreen"
        onLongPressed: result.text = "touchscreen longpressed"
    }

    TapHandler {
        acceptedDevices: PointerDevice.Stylus
        onTapped: result.text = "stylus"
    }

    DragHandler {
        target: result
        onActiveChanged: if (active) result.text = "dragged"
    }

    WheelHandler {
        orientation: Qt.Vertical | Qt.Horizontal
        onWheel: wheel => result.text = `wheel ${wheel.angleDelta.x} ${wheel.angleDelta.y}`
    }
}
