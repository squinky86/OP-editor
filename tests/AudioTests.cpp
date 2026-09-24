// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "audio/Synth.h"
#include "Fixtures.h"
#include "core/Clefs.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace ope;
using namespace ope::audio;

class AudioTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void ttbbSolosPairsAndSilenceReachTheSynth()
    {
        const auto doc = io::loadBytes("song.toml", fixtures::ttbbSong());
        QVERIFY(doc);
        for (int mask = 0; mask < 16; ++mask) {
            PlaybackOptions options;
            for (int i = 0; i < 4; ++i) {
                if (!(mask & (1 << i)))
                    options.mutedParts.append(doc->parts[i].name);
            }
            Synth synth;
            synth.setPlan(buildPlan(*doc, options));
            synth.reset(0);
            std::vector<float> samples(4096);
            synth.render(samples.data(), static_cast<int>(samples.size()));
            double energy = 0;
            for (float sample : samples) {
                QVERIFY(std::isfinite(sample));
                energy += std::abs(sample);
            }
            if (mask == 0)
                QCOMPARE(energy, 0.0);
            else
                QVERIFY(energy > 1.0);
        }
    }

    void clefOnlyEditsProduceIdenticalAudio()
    {
        auto doc = io::loadBytes("song.toml", fixtures::ttbbSong());
        QVERIFY(doc);
        std::vector<float> reference;
        for (const auto &clef : clefs::all()) {
            for (auto &part : doc->parts)
                part.clef.set(clef.name);
            Synth synth;
            synth.setPlan(buildPlan(*doc));
            synth.reset(0);
            std::vector<float> samples(4096);
            synth.render(samples.data(), static_cast<int>(samples.size()));
            if (reference.empty())
                reference = samples;
            else
                QCOMPARE(samples, reference);
        }
    }

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
