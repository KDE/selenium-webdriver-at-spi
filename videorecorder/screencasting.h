/*
    SPDX-FileCopyrightText: 2020 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QObject>
#include <QSharedPointer>
#include <QVector>
#include <memory>
#include <optional>

class QScreen;
struct zkde_screencast_unstable_v1;

namespace KWayland::Client
{
class PlasmaWindow;
class Registry;
class Output;
} // namespace KWayland::Client

class ScreencastingPrivate;
class ScreencastingSourcePrivate;
class ScreencastingStreamPrivate;
class ScreencastingStream : public QObject
{
    Q_OBJECT
public:
    explicit ScreencastingStream(QObject *parent);
    ~ScreencastingStream() override;
    Q_DISABLE_COPY_MOVE(ScreencastingStream)

    quint32 nodeId() const;

Q_SIGNALS:
    void created(quint32 nodeid);
    void failed(const QString &error);
    void closed();

private:
    friend class Screencasting;
    std::unique_ptr<ScreencastingStreamPrivate> d;
};

class Screencasting : public QObject
{
    Q_OBJECT
public:
    explicit Screencasting(QObject *parent = nullptr);
    ~Screencasting() override;
    Q_DISABLE_COPY_MOVE(Screencasting)

    enum CursorMode {
        Hidden = 1,
        Embedded = 2,
        Metadata = 4,
    };
    Q_ENUM(CursorMode);

    ScreencastingStream *createRegionStream(const QRect &region, qreal scaling, CursorMode mode);
    ScreencastingStream *createOutputStream(QScreen *screen, CursorMode mode);

    void destroy();

private:
    std::unique_ptr<ScreencastingPrivate> d;
};
