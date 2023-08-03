// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15 as QQC2

QQC2.Slider {
    Accessible.name: "slider"
    from: 0
    to: 100
    width: 100
    height: 100
    value: 25
    onValueChanged: console.log(value)
    stepSize: 1
}
