// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "audio/Synth.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace ope;
using namespace ope::audio;

class AudioTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void resettingInsideANoteRestoresItsSound()
    {
        PlaybackPlan plan;
        plan.totalSeconds = 1.0;
        plan.notes.append(PlaybackNote { 0.0, 1.0, 60, 100 });

        Synth synth;
        synth.setPlan(plan);
        synth.reset(0.5);

        std::vector<float> samples(256);
        QVERIFY(synth.render(samples.data(), static_cast<int>(samples.size())));

        double energy = 0.0;
        for (const float sample : samples)
            energy += std::abs(sample);
        QVERIFY2(energy > 1.0, "a note spanning the resume point must remain audible");
    }
};

QTEST_APPLESS_MAIN(AudioTests)
#include "AudioTests.moc"
