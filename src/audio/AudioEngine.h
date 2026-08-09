// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Transport: pushes the synth's output at a QAudioSink and reports where the
// playhead is, so the score can follow along.

#pragma once

#include "Synth.h"

#include <QIODevice>
#include <QObject>
#include <QTimer>

#include <memory>

QT_BEGIN_NAMESPACE
class QAudioSink;
QT_END_NAMESPACE

namespace ope::audio {

/// The QIODevice the sink pulls from; hands the synth's samples over.
class SynthDevice : public QIODevice {
    Q_OBJECT
public:
    explicit SynthDevice(Synth *synth, QObject *parent = nullptr);

    [[nodiscard]] qint64 readData(char *data, qint64 maxSize) override;
    [[nodiscard]] qint64 writeData(const char *data, qint64 maxSize) override;
    [[nodiscard]] qint64 bytesAvailable() const override;
    [[nodiscard]] bool isSequential() const override { return true; }

private:
    Synth *m_synth = nullptr;
};

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    void setPlan(const PlaybackPlan &plan);
    [[nodiscard]] const PlaybackPlan &plan() const noexcept { return m_plan; }

    void play(double fromSeconds = 0.0);
    void pause();
    void stop();
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double position() const;
    [[nodiscard]] double duration() const noexcept { return m_plan.totalSeconds; }
    /// True when no audio device could be opened; the UI says so instead of
    /// pretending to play.
    [[nodiscard]] bool isAvailable() const noexcept { return m_available; }
    [[nodiscard]] QString unavailableReason() const { return m_unavailableReason; }

Q_SIGNALS:
    void positionChanged(double seconds);
    void playingChanged(bool playing);
    void finished();

private:
    void tick();

    Synth m_synth;
    PlaybackPlan m_plan;
    std::unique_ptr<QAudioSink> m_sink;
    SynthDevice *m_device = nullptr;
    QTimer m_timer;
    double m_startOffset = 0.0;
    bool m_available = false;
    QString m_unavailableReason;
};

} // namespace ope::audio
