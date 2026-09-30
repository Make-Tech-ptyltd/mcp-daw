// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Deterministic signal tests for the native effects: measured behaviour vs. analytic expectation.
#include "TestSupport.h"
#include "dsp/Compressor.h"
#include "dsp/Distortion.h"
#include "dsp/MultiTapDelay.h"
#include "dsp/ParametricEq.h"
#include "dsp/Reverb.h"

namespace mcpdaw::test
{
namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<EffectProcessor> make (const char* type, std::initializer_list<std::pair<const char*, float>> values = {})
{
    auto fx = createEffectProcessor (type);
    for (const auto& [id, v] : values) fx->setParameter (fx->descriptor().indexOf (id), v);
    fx->prepare (kSr, 1024);
    return fx;
}

/** Process a buffer through `fx` in 512-sample blocks (exercises block boundaries). */
void run (EffectProcessor& fx, juce::AudioBuffer<float>& b, double bpm = 120.0)
{
    const ProcessContext ctx { kSr, bpm, true };
    for (int s = 0; s < b.getNumSamples(); s += 512)
    {
        const int n = std::min (512, b.getNumSamples() - s);
        fx.process (b.getWritePointer (0) + s, b.getWritePointer (1) + s, n, ctx);
    }
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}
} // namespace

class EqTests final : public juce::UnitTest
{
public:
    EqTests() : juce::UnitTest ("Parametric EQ", "DSP") {}

    void runTest() override
    {
        beginTest ("flat by default");
        const auto defaults = findEffectType ("eq")->defaults();
        for (double f : { 30.0, 100.0, 1000.0, 10000.0 })
            expectWithinAbsoluteError (ParametricEq::responseDb (defaults, f, kSr), 0.0, 1e-6);

        beginTest ("bell: +6 dB at centre frequency, ~0 dB far away");
        auto v = defaults;
        auto* d = findEffectType ("eq");
        v[static_cast<size_t> (d->indexOf ("band3.freq"))] = 1000.0f;
        v[static_cast<size_t> (d->indexOf ("band3.gain"))] = 6.0f;
        v[static_cast<size_t> (d->indexOf ("band3.q"))] = 1.0f;
        expectWithinAbsoluteError (ParametricEq::responseDb (v, 1000.0, kSr), 6.0, 0.01);
        expectWithinAbsoluteError (ParametricEq::responseDb (v, 50.0, kSr), 0.0, 0.1);

        beginTest ("processed sine matches the analytic response");
        auto fx = make ("eq", { { "band3.freq", 1000.0f }, { "band3.gain", 6.0f }, { "band3.q", 1.0f } });
        juce::AudioBuffer<float> b (2, 48000);
        sine (b.getWritePointer (0), 48000, 1000.0, kSr);
        sine (b.getWritePointer (1), 48000, 1000.0, kSr);
        const double inRms = rms (b.getReadPointer (0) + 24000, 24000);
        run (*fx, b);
        const double gainDb = 20.0 * std::log10 (rms (b.getReadPointer (0) + 24000, 24000) / inRms);
        expectWithinAbsoluteError (gainDb, 6.0, 0.05);

        beginTest ("high-pass removes low frequencies");
        auto hp = make ("eq", { { "band1.enabled", 1.0f }, { "band1.freq", 500.0f } });
        juce::AudioBuffer<float> low (2, 48000);
        sine (low.getWritePointer (0), 48000, 50.0, kSr);
        sine (low.getWritePointer (1), 48000, 50.0, kSr);
        run (*hp, low);
        expectLessThan (rms (low.getReadPointer (0) + 24000, 24000), 0.5 / std::sqrt (2.0) * 0.02);   // < -34 dB
    }
};

class CompressorTests final : public juce::UnitTest
{
public:
    CompressorTests() : juce::UnitTest ("Compressor", "DSP") {}

    void runTest() override
    {
        beginTest ("static curve (hard knee)");
        expectWithinAbsoluteError (Compressor::staticGainReductionDb (-30, -20, 4, 0), 0.0, 1e-12);
        expectWithinAbsoluteError (Compressor::staticGainReductionDb (-8, -20, 4, 0), 9.0, 1e-12);   // 12 dB over -> 3 dB out
        beginTest ("static curve (soft knee is continuous)");
        expectWithinAbsoluteError (Compressor::staticGainReductionDb (-23, -20, 4, 6), 0.0, 1e-12);
        expectWithinAbsoluteError (Compressor::staticGainReductionDb (-17, -20, 4, 6), 2.25, 1e-12);
        expectGreaterThan (Compressor::staticGainReductionDb (-20, -20, 4, 6), 0.0);

        beginTest ("steady-state gain reduction on a loud tone");
        auto fx = make ("compressor", { { "threshold", -20.0f }, { "ratio", 4.0f }, { "knee", 0.0f }, { "attack", 1.0f }, { "release", 50.0f } });
        juce::AudioBuffer<float> b (2, 48000);
        b.clear();
        for (int i = 0; i < 48000; ++i) { b.setSample (0, i, 0.5f); b.setSample (1, i, -0.5f); }   // |x| = -6 dBFS
        run (*fx, b);
        const double outDb = 20.0 * std::log10 (std::abs (b.getSample (0, 47999)));
        // -6 dBFS is 14 dB over -20 -> 10.5 dB of reduction -> -16.5 dBFS
        expectWithinAbsoluteError (outDb, -16.5, 0.05);
        expectWithinAbsoluteError (fx->telemetry (0), 10.5f, 0.05f);

        beginTest ("below threshold is untouched");
        auto quiet = make ("compressor", { { "threshold", -10.0f } });
        juce::AudioBuffer<float> q (2, 4800);
        sine (q.getWritePointer (0), 4800, 440, kSr, 0.1f);
        sine (q.getWritePointer (1), 4800, 440, kSr, 0.1f);
        juce::AudioBuffer<float> ref (q);
        run (*quiet, q);
        for (int i = 0; i < 4800; ++i) expectWithinAbsoluteError (q.getSample (0, i), ref.getSample (0, i), 1e-6f);
    }
};

class DelayTests final : public juce::UnitTest
{
public:
    DelayTests() : juce::UnitTest ("Multi-tap delay", "DSP") {}

    void runTest() override
    {
        beginTest ("tap times follow tempo divisions");
        const auto defaults = findEffectType ("multitap_delay")->defaults();
        expectWithinAbsoluteError (MultiTapDelay::tapSeconds (defaults, 0, 120.0), 0.375, 1e-12);   // 1/8D at 120 = 0.75 beat
        expectWithinAbsoluteError (MultiTapDelay::tapSeconds (defaults, 1, 90.0), 60.0 / 90.0, 1e-12); // 1/4 at 90

        beginTest ("impulse produces taps at the right samples, panned");
        // Tap 1: 1/4 note at 120 BPM (24000 samples) hard left; tap 2: 100 ms free, hard right. No feedback, 100 % wet.
        auto fx = make ("multitap_delay", { { "tap1.enabled", 1 }, { "tap1.sync", 1 }, { "tap1.division", 11 }, { "tap1.pan", -1 },
                                            { "tap1.level", 0 }, { "tap1.feedback", 0 },
                                            { "tap2.enabled", 1 }, { "tap2.sync", 0 }, { "tap2.time", 100 }, { "tap2.pan", 1 },
                                            { "tap2.level", 0 }, { "tap2.feedback", 0 }, { "mix", 1 }, { "drive", 0 } });
        juce::AudioBuffer<float> b (2, 48000);
        b.clear();
        b.setSample (0, 0, 1.0f);
        b.setSample (1, 0, 1.0f);
        run (*fx, b);
        auto argmax = [&] (int ch) { int best = 0; for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (ch, i)) > std::abs (b.getSample (ch, best))) best = i; return best; };
        expectEquals (argmax (0), 24000);
        expectEquals (argmax (1), 4800);
        expectWithinAbsoluteError (b.getSample (0, 24000), 1.0f, 1e-4f);   // constant-power hard pan = unity on that side
        expectWithinAbsoluteError (b.getSample (1, 24000), 0.0f, 1e-4f);

        beginTest ("feedback repeats decay and stay bounded at max settings");
        auto loud = make ("multitap_delay", { { "mix", 1 }, { "drive", 1 } });
        for (int t = 1; t <= 8; ++t)
        {
            loud->setParameter (loud->descriptor().indexOf ("tap" + juce::String (t) + ".enabled"), 1);
            loud->setParameter (loud->descriptor().indexOf ("tap" + juce::String (t) + ".feedback"), 0.95f);
            loud->setParameter (loud->descriptor().indexOf ("tap" + juce::String (t) + ".level"), 6);
        }
        juce::AudioBuffer<float> noise (2, 48000 * 4);
        juce::Random rng (1);
        for (int i = 0; i < noise.getNumSamples(); ++i) { noise.setSample (0, i, rng.nextFloat() * 2 - 1); noise.setSample (1, i, rng.nextFloat() * 2 - 1); }
        run (*loud, noise);
        expect (allFinite (noise));
        expectLessThan (noise.getMagnitude (0, noise.getNumSamples()), 40.0f);
    }
};

class ReverbTests final : public juce::UnitTest
{
public:
    ReverbTests() : juce::UnitTest ("Reverb", "DSP") {}

    void runTest() override
    {
        beginTest ("impulse response is finite, starts after pre-delay and decays per RT60");
        auto fx = make ("reverb", { { "mix", 1 }, { "decay", 1.0f }, { "predelay", 50 }, { "lowcut", 20 } });
        juce::AudioBuffer<float> b (2, 48000 * 3);
        b.clear();
        b.setSample (0, 0, 1.0f);
        b.setSample (1, 0, 1.0f);
        run (*fx, b);
        expect (allFinite (b));
        expectLessThan (b.getMagnitude (0, 2000), 1e-6f);                  // nothing before 50 ms pre-delay (~2400 samples)

        auto energyDb = [&] (int from, int len) { return 20.0 * std::log10 (std::max (1e-12, rms (b.getReadPointer (0) + from, len))); };
        const double early = energyDb (9600, 4800);        // 200..300 ms
        const double oneSecondLater = energyDb (57600, 4800);
        // RT60 = 1 s -> ~60 dB drop per second (damping makes HF decay faster, so allow a window).
        expectGreaterThan (early - oneSecondLater, 45.0);
        expectLessThan (early - oneSecondLater, 80.0);

        beginTest ("stereo output is decorrelated");
        double corr = 0, el = 0, er = 0;
        for (int i = 4800; i < 48000; ++i) { corr += b.getSample (0, i) * b.getSample (1, i); el += b.getSample (0, i) * b.getSample (0, i); er += b.getSample (1, i) * b.getSample (1, i); }
        expectLessThan (std::abs (corr / std::sqrt (el * er)), 0.5);

        beginTest ("mix 0 is transparent");
        auto dry = make ("reverb", { { "mix", 0 } });
        juce::AudioBuffer<float> s (2, 4800);
        sine (s.getWritePointer (0), 4800, 300, kSr);
        sine (s.getWritePointer (1), 4800, 300, kSr);
        juce::AudioBuffer<float> ref (s);
        run (*dry, s);
        for (int i = 0; i < 4800; ++i) expectWithinAbsoluteError (s.getSample (0, i), ref.getSample (0, i), 1e-5f);
    }
};

class DistortionTests final : public juce::UnitTest
{
public:
    DistortionTests() : juce::UnitTest ("Distortion", "DSP") {}

    /** Amplitude of one frequency component (single-bin DFT) over samples [from, from + n). */
    static double amplitudeAt (const float* x, int from, int n, double freq)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = juce::MathConstants<double>::twoPi * freq * i / kSr;
            re += x[from + i] * std::cos (w);
            im -= x[from + i] * std::sin (w);
        }
        return 2.0 * std::sqrt (re * re + im * im) / n;
    }

    static double rms (const float* x, int from, int n)
    {
        double s = 0;
        for (int i = 0; i < n; ++i) s += static_cast<double> (x[from + i]) * x[from + i];
        return std::sqrt (s / n);
    }

    static juce::AudioBuffer<float> sineBuffer (double freq, float amp, int n = 48000)
    {
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = amp * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * freq * i / kSr));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    void runTest() override
    {
        using T = distortion::Type;
        const T all[] = { T::Soft, T::Warm, T::Hard, T::Fuzz, T::Fold };

        beginTest ("curves: bounded, pass through zero, unity slope at the origin (except Fuzz)");
        for (auto t : all)
        {
            float peak = 0;
            for (float x = -60.0f; x <= 60.0f; x += 0.01f) peak = std::max (peak, std::abs (Distortion::shape (t, x)));
            expectLessOrEqual (peak, 1.0001f);
            expectWithinAbsoluteError (Distortion::transfer (t, 24.0f, 0.4f, 0.0f), 0.0f, 1e-7f);   // bias never shifts silence
            if (t != T::Fuzz)
                expectWithinAbsoluteError ((Distortion::shape (t, 1e-3f) - Distortion::shape (t, -1e-3f)) / 2e-3f, 1.0f, 0.01f);
        }

        beginTest ("silence in, silence out (even with bias and crusher)");
        {
            auto fx = make ("distortion", { { "bias", 0.8f }, { "drive", 40.0f }, { "bits", 6.0f }, { "downsample", 4.0f } });
            juce::AudioBuffer<float> b (2, 4096);
            b.clear();
            run (*fx, b);
            expectEquals (b.getMagnitude (0, 4096), 0.0f);
        }

        beginTest ("symmetric curve makes odd harmonics only; bias adds even harmonics (warmth)");
        {
            auto sym = make ("distortion", { { "type", 0.0f }, { "drive", 18.0f }, { "bias", 0.0f } });
            auto s = sineBuffer (1000.0, 0.5f);
            run (*sym, s);
            const auto* y = s.getReadPointer (0);
            const double f1 = amplitudeAt (y, 4800, 43200, 1000), h2 = amplitudeAt (y, 4800, 43200, 2000), h3 = amplitudeAt (y, 4800, 43200, 3000);
            expectGreaterThan (h3 / f1, 0.05, "Soft at 18 dB: strong 3rd harmonic");
            expectLessThan (h2 / f1, 1e-3, "no bias: no 2nd harmonic");

            auto warm = make ("distortion", { { "type", 1.0f }, { "drive", 18.0f }, { "bias", 0.3f } });
            auto w = sineBuffer (1000.0, 0.5f);
            run (*warm, w);
            const auto* z = w.getReadPointer (0);
            expectGreaterThan (amplitudeAt (z, 4800, 43200, 2000) / amplitudeAt (z, 4800, 43200, 1000), 0.02, "Warm + bias: 2nd harmonic");
        }

        beginTest ("4x oversampling keeps aliases of a hard-clipped 5 kHz tone low");
        {
            auto hard = make ("distortion", { { "type", 2.0f }, { "drive", 24.0f }, { "bias", 0.0f } });
            auto s = sineBuffer (5000.0, 0.5f);
            run (*hard, s);
            const auto* y = s.getReadPointer (0);
            const double f1 = amplitudeAt (y, 4800, 43200, 5000);
            // 9th harmonic (45 kHz) would fold to 3 kHz, 7th (35 kHz) to 13 kHz without oversampling.
            const double alias = std::max (amplitudeAt (y, 4800, 43200, 3000), amplitudeAt (y, 4800, 43200, 13000));
            logMessage ("alias level " + juce::String (juce::Decibels::gainToDecibels (alias / f1), 1) + " dB re fundamental");
            expectLessThan (alias / f1, juce::Decibels::decibelsToGain (-40.0));
        }

        beginTest ("auto gain: heavy drive changes character, not loudness");
        for (auto t : all)
        {
            auto fx = make ("distortion", { { "type", static_cast<float> (t) }, { "drive", 36.0f }, { "bias", 0.0f } });
            auto s = sineBuffer (220.0, 0.5f);
            run (*fx, s);
            const double ratioDb = juce::Decibels::gainToDecibels (rms (s.getReadPointer (0), 4800, 43200) / (0.5 / std::sqrt (2.0)));
            expectWithinAbsoluteError (ratioDb, 0.0, 3.0, distortion::typeNames()[static_cast<int> (t)] + " level change");
        }

        beginTest ("brutal settings stay finite and bounded; mix 0 passes the dry signal (latency-aligned)");
        {
            auto fx = make ("distortion", { { "type", 4.0f }, { "drive", 48.0f }, { "bias", 1.0f }, { "bits", 3.0f },
                                            { "downsample", 16.0f }, { "autogain", 0.0f }, { "output", 12.0f }, { "tone", 800.0f } });
            auto s = sineBuffer (55.0, 1.0f);
            run (*fx, s);
            expect (allFinite (s));
            expectLessThan (s.getMagnitude (0, 48000), 8.0f);

            auto dry = make ("distortion", { { "mix", 0.0f }, { "drive", 48.0f } });
            auto d = sineBuffer (55.0, 0.5f);
            run (*dry, d);
            expectWithinAbsoluteError (rms (d.getReadPointer (0), 4800, 43200), 0.5 / std::sqrt (2.0), 1e-3);
            expectLessThan (static_cast<double> (static_cast<Distortion&> (*dry).latencySamples()), 16.0, "IIR oversampling latency is small");
        }
    }
};

static EqTests eqTests;
static DistortionTests distortionTests;
static CompressorTests compressorTests;
static DelayTests delayTests;
static ReverbTests reverbTests;
} // namespace mcpdaw::test
