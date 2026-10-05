/*
    SPDX-FileCopyrightText: 2022 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "screencasting.h"

#include <KSignalHandler>
#include <PipeWireRecord>
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include <csignal>

using namespace std::chrono_literals;
using namespace Qt::StringLiterals;
using namespace KWayland::Client;

class Context : public QObject
{
    Q_OBJECT
public:
    inline static Context *self = nullptr;

    static void reset(const QString &output)
    {
        if (self) {
            self->deleteLater(); // careful, delete later, we get called from a slot!
        }
        self = new Context(output, qGuiApp);
    }

private:
    Context(const QString &output, QObject *parent = nullptr)
        : QObject(parent)
        , m_output(output)
        , m_record([this] {
            auto record = new PipeWireRecord(this);
            record->setOutput(m_output);

            connect(record, &PipeWireRecord::errorFound, qGuiApp, [](const QString &error) {
                qWarning() << "recording error!" << error;
                qGuiApp->exit(3);
            });
            qDebug() << "initial state" << record->state();
            connect(record, &PipeWireRecord::stateChanged, qGuiApp, [record, this] {
                auto state = record->state();
                qDebug() << "state changed" << state;
                switch (state) {
                case PipeWireRecord::Idle:
                    qDebug() << "idle!" << m_hasStarted;
                    if (m_hasStarted) {
                        qGuiApp->quit();
                    }
                    break;
                case PipeWireRecord::Recording: {
                    qDebug() << "recording...";
                    m_hasStarted = true;
                    QFile startedMarker(m_output + ".started"_L1);
                    if (startedMarker.open(QFile::WriteOnly)) {
                        startedMarker.close();
                    } else {
                        qWarning() << "Could not create started marker file!";
                        qGuiApp->exit(4);
                    }
                    break;
                }
                case PipeWireRecord::Rendering:
                    constexpr auto maxRetries = 8;
                    static auto retryCount = maxRetries;
                    retryCount--;
                    if (!m_hasStarted && retryCount > 0) {
                        qWarning() << "Got into rendering state without having started recording! Trying once again...";
                        QThread::sleep(1s); // random amount of time to wait for pipewire to be ready
                        Context::reset(m_output);
                        return;
                    }
                    qDebug() << "rendering...";
                    break;
                }
            });

            return record;
        }())
        , m_screencasting([this] {
            auto screencasting = new Screencasting(this);

            QRect region;
            for (auto screen : qGuiApp->screens()) {
                region |= screen->geometry();
            }

            auto stream = screencasting->createRegionStream(region, 1, Screencasting::Metadata);
            connect(stream, &ScreencastingStream::created, m_record, [stream, this] {
                m_record->setNodeId(stream->nodeId());
                m_record->start();
            });
            return screencasting;
        }())
        , m_startTimer([this] {
            auto timer = new QTimer(this);
            timer->setSingleShot(true);
            constexpr auto maximumStartDelay = 2000ms; // arbitrary
            timer->setInterval(maximumStartDelay);
            connect(timer, &QTimer::timeout, this, [this] {
                if (m_hasStarted) {
                    return;
                }
                qWarning() << "Timeout waiting for screencasting to start!. Trying again...";
                Context::reset(m_output);
            });
            timer->start();
            return timer;
        }())
    {
        connect(KSignalHandler::self(), &KSignalHandler::signalReceived, this, [this] {
            m_record->stop();
        });
    }

    bool m_hasStarted = false;
    QString m_output;
    PipeWireRecord *m_record;
    Screencasting *m_screencasting;
    QTimer *m_startTimer;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("path for the generated video"), QStringLiteral("path"));
    parser.addHelpOption();
    parser.addOption(outputOption);
    parser.process(app);

    Context::reset(parser.value(outputOption));

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);

    return app.exec();
}

#include "main.moc"
