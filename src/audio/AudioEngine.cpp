// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "AudioEngine.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <vector>

namespace ope::audio {
namespace {

constexpr int TickIntervalMs = 33;  // ~30 Hz cursor updates

QAudioFormat preferredFormat()
{
    QAudioFormat format;
    format.setSampleRate(SampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    return format;
}

} // namespace

SynthDevice::SynthDevice(Synth *synth, QObject *parent) : QIODevice(parent), m_synth(synth) {}

qint64 SynthDevice::readData(char *data, qint64 maxSize)
{
    const qint64 frames = maxSize / static_cast<qint64>(sizeof(float));
    if (frames <= 0)
        return 0;
    m_synth->render(reinterpret_cast<float *>(data), static_cast<int>(frames));
    return frames * static_cast<qint64>(sizeof(float));
}

qint64 SynthDevice::writeData(const char *, qint64) { return 0; }

qint64 SynthDevice::bytesAvailable() const
{
    // Always claim a healthy buffer: the synth can generate on demand for as
    // long as the sink cares to ask.
    return 32768 + QIODevice::bytesAvailable();
}

AudioEngine::AudioEngine(QObject *parent) : QObject(parent)
{
    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    if (output.isNull()) {
        m_unavailableReason = tr("no audio output device");
        return;
    }
    QAudioFormat format = preferredFormat();
    if (!output.isFormatSupported(format)) {
        format = output.preferredFormat();
        if (format.sampleFormat() != QAudioFormat::Float
            || format.sampleRate() != SampleRate) {
            m_unavailableReason
                = tr("the audio device does not accept 48 kHz mono float samples");
            return;
        }
    }
    m_sink = std::make_unique<QAudioSink>(output, format, this);
    m_device = new SynthDevice(&m_synth, this);
    m_available = true;

    m_timer.setInterval(TickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &AudioEngine::tick);
}

AudioEngine::~AudioEngine()
{
    if (m_sink)
        m_sink->stop();
}

void AudioEngine::setPlan(const PlaybackPlan &plan)
{
    const bool wasPlaying = isPlaying();
    if (wasPlaying)
        stop();
    m_plan = plan;
    m_synth.setPlan(plan);
}

void AudioEngine::play(double fromSeconds)
{
    if (!m_available)
        return;
    stop();
    m_startOffset = fromSeconds;
    m_synth.reset(fromSeconds);
    m_device->open(QIODevice::ReadOnly);
    m_sink->start(m_device);
    m_timer.start();
    Q_EMIT playingChanged(true);
}

void AudioEngine::pause()
{
    if (!m_available || !isPlaying())
        return;
    m_startOffset = position();
    m_sink->stop();
    m_timer.stop();
    Q_EMIT playingChanged(false);
}

void AudioEngine::stop()
{
    if (!m_available)
        return;
    const bool was = isPlaying();
    m_sink->stop();
    if (m_device->isOpen())
        m_device->close();
    m_timer.stop();
    if (was)
        Q_EMIT playingChanged(false);
}

bool AudioEngine::isPlaying() const
{
    return m_available && m_sink && m_sink->state() == QAudio::ActiveState;
}

double AudioEngine::position() const
{
    if (!m_available || !m_sink)
        return m_startOffset;
    // processedUSecs() counts audio actually handed to the device, so the cursor
    // tracks what the user is hearing rather than what has been generated.
    return m_startOffset + static_cast<double>(m_sink->processedUSecs()) / 1'000'000.0;
}

void AudioEngine::tick()
{
    const double seconds = position();
    Q_EMIT positionChanged(seconds);
    if (seconds >= m_plan.totalSeconds + 0.25 || m_synth.finished()) {
        stop();
        Q_EMIT finished();
    }
}

} // namespace ope::audio
