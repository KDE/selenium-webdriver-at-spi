/*
    SPDX-FileCopyrightText: 2022 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "screencasting.h"

#include <KPipeWire/PipeWireRecord>
#include <KSignalHandler>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>

#include <csignal>

using namespace KWayland::Client;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    auto m_record = new PipeWireRecord(&app);

    QCommandLineParser parser;
    QCommandLineOption outputOption(QStringLiteral("output"),
                                    QStringLiteral("path for the generated video"),
                                    QStringLiteral("path"),
                                    QStringLiteral("recording.%1").arg(m_record->extension()));
    parser.addHelpOption();
    parser.addOption(outputOption);
    parser.process(app);

    Screencasting screencasting;

    QRect region;
    for (auto screen : qGuiApp->screens()) {
        region |= screen->geometry();
    }

    auto stream = screencasting.createRegionStream(region, 1, Screencasting::Metadata);
    m_record->setOutput(parser.value(outputOption));
    QObject::connect(stream, &ScreencastingStream::created, &app, [stream, m_record] {
        m_record->setNodeId(stream->nodeId());
        m_record->setActive(true);
    });

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, m_record, [m_record] {
        m_record->setActive(false);
    });

    QObject::connect(m_record, &PipeWireRecord::errorFound, qGuiApp, [](const QString &error) {
        qWarning() << "recording error!" << error;
        qGuiApp->exit(3);
    });
    qDebug() << "initial state" << m_record->state();
    bool hasStarted = false;
    QObject::connect(m_record, &PipeWireRecord::stateChanged, qGuiApp, [m_record, &hasStarted] {
        auto state = m_record->state();
        qDebug() << "state changed" << state;
        switch (state) {
        case PipeWireRecord::Idle:
            qWarning() << "idle!" << hasStarted;
            if (hasStarted) {
                qGuiApp->quit();
            }
            break;
        case PipeWireRecord::Recording:
            qWarning() << "recording...";
            hasStarted = true;
            break;
        case PipeWireRecord::Rendering:
            qWarning() << "rendering...";
            break;
        }
    });

    return app.exec();
}
