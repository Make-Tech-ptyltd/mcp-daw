#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Live audio engine: owns the device callback, the transport and the lock-free plan hand-off.
//
// Realtime contract of audioDeviceIOCallbackWithContext():
//   * no allocation, no locks, no I/O, no logging;
//   * picks up a new plan with one atomic exchange;
//   * hands the previous plan back to the message thread through a wait-free SPSC ring; the
//     message thread deletes retired plans (and with them any runtimes/audio no longer used);
//   * if the ring is ever full, it simply keeps the current plan until the next callback.
//
// Transport position is an integer sample counter owned by the audio thread (no drift), published
// as beats for the UI/MCP. When tempo or sample rate changes, the musical position is preserved.

#include "../core/ProjectController.h"
#include "PlanRenderer.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>

namespace mcpdaw
{
class AudioEngine final : public juce::AudioIODeviceCallback,
                          public TransportControl,
                          public ProjectController::Listener,
                          private juce::AsyncUpdater,
                          private juce::Timer
{
public:
    explicit AudioEngine (ProjectController&);
    ~AudioEngine() override;

    /** Attach to a device manager (the engine adds itself as callback). Optional: headless use works too. */
    void attach (juce::AudioDeviceManager&);
    void detach();

    // --- TransportControl ------------------------------------------------------------------------
    void play() override;
    void stop() override;
    void pause() override;
    void seekBeats (double beat) override;
    bool isPlaying() const override { return playing.load (std::memory_order_relaxed); }
    double positionBeats() const override;
    juce::var describeState() const override;

    // --- metering (message thread) ----------------------------------------------------------------
    struct StereoPeak { float left = 0, right = 0; };
    /** Peak since the previous call for a track (resets the accumulator). */
    StereoPeak takeTrackPeak (const juce::String& trackId);
    StereoPeak takeMasterPeak();
    bool clipped() const noexcept { return clipLatch.load (std::memory_order_relaxed); }
    void resetClip() noexcept { clipLatch.store (false, std::memory_order_relaxed); }
    float effectTelemetry (const juce::String& effectId) const;
    double deviceSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }
    double cpuLoad() const;
    int underrunCount() const;

    // --- preview (browser audition) ---------------------------------------------------------------
    void startPreview (AudioDataPtr data, float gainDb = -6.0f);
    void stopPreview();
    bool isPreviewing() const noexcept { return previewActive.load (std::memory_order_relaxed); }

    // --- AudioIODeviceCallback ---------------------------------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

    // --- ProjectController::Listener ---------------------------------------------------------------
    void projectChanged (const Project&, bool structural) override;
    void editingPreferencesChanged() override;

private:
    /** Anything handed to the audio thread and later retired back. */
    struct Retirable { virtual ~Retirable() = default; };
    struct PlanHolder final : Retirable { std::unique_ptr<PlaybackPlan> plan; };
    struct PreviewVoice final : Retirable { AudioDataPtr data; double position = 0; double step = 1; float gain = 0.5f; };

    ProjectController& controller;
    juce::AudioDeviceManager* deviceManager = nullptr;
    std::unique_ptr<RuntimeRegistry> registry;     // message thread

    // Hand-off slots (message thread -> audio thread)
    std::atomic<PlanHolder*> pendingPlan { nullptr };
    std::atomic<PreviewVoice*> pendingPreview { nullptr };
    std::atomic<bool> previewStopRequested { false };

    // Audio-thread owned
    PlanHolder* activePlan = nullptr;
    PreviewVoice* activePreview = nullptr;
    std::int64_t positionSamples = 0;
    PlanRenderer renderer;

    // Retire ring (audio thread -> message thread), single producer/single consumer.
    static constexpr size_t kRetireSlots = 128;
    std::array<Retirable*, kRetireSlots> retireRing {};
    std::atomic<size_t> retireWrite { 0 }, retireRead { 0 };
    bool retire (Retirable*) noexcept;         // audio thread
    void collectGarbage();                     // message thread

    // Shared state
    std::atomic<bool> playing { false }, previewActive { false }, clipLatch { false };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<double> publishedBeats { 0.0 };
    std::atomic<double> seekRequestBeats { -1.0 };  // < 0 = none
    std::atomic<int> deviceErrors { 0 };
    double playStartBeats = 0;                      // message thread (for ACID-style stop)

    void rebuildPlan();
    void handleAsyncUpdate() override { rebuildPlan(); }
    void timerCallback() override { collectGarbage(); }
};

static_assert (std::atomic<double>::is_always_lock_free, "lock-free doubles required for the realtime transport");
static_assert (std::atomic<void*>::is_always_lock_free);
} // namespace mcpdaw
