// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
#include "AudioEngine.h"
#include "../core/ProjectJson.h"

namespace mcpdaw
{
AudioEngine::AudioEngine (ProjectController& c) : controller (c)
{
    registry = std::make_unique<RuntimeRegistry> (sampleRate.load(), PlanRenderer::kMaxBlock);
    controller.addListener (this);
    controller.transport = this;
    rebuildPlan();
    startTimerHz (20);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    detach();
    if (controller.transport == this) controller.transport = nullptr;
    controller.removeListener (this);
    // The device callback is gone, so the audio-thread objects can be released here.
    delete pendingPlan.exchange (nullptr);
    delete pendingPreview.exchange (nullptr);
    delete activePlan;
    delete activePreview;
    collectGarbage();
}

void AudioEngine::attach (juce::AudioDeviceManager& dm)
{
    detach();
    deviceManager = &dm;
    dm.addAudioCallback (this);
}

void AudioEngine::detach()
{
    if (deviceManager != nullptr)
    {
        deviceManager->removeAudioCallback (this);   // blocks until the callback has finished
        deviceManager = nullptr;
    }
}

// ================================================================================================
// Plan publishing (message thread)
// ================================================================================================
void AudioEngine::projectChanged (const Project&, bool structural)
{
    if (structural) rebuildPlan();
}

void AudioEngine::editingPreferencesChanged()
{
    rebuildPlan();   // the Default declick time is compiled into the plan as sample counts
}

void AudioEngine::rebuildPlan()
{
    const double sr = sampleRate.load();
    if (registry == nullptr || registry->sampleRate() != sr)
        registry = std::make_unique<RuntimeRegistry> (sr, PlanRenderer::kMaxBlock);   // old runtimes live on inside old plans

    auto holder = std::make_unique<PlanHolder>();
    holder->plan = PlaybackPlan::build (controller.project(), *registry, controller.defaultDeclickMs());
    delete pendingPlan.exchange (holder.release(), std::memory_order_acq_rel);   // an unconsumed older plan was never seen
    collectGarbage();
}

bool AudioEngine::retire (Retirable* r) noexcept
{
    const auto w = retireWrite.load (std::memory_order_relaxed);
    if (w - retireRead.load (std::memory_order_acquire) >= kRetireSlots) return false;
    retireRing[w % kRetireSlots] = r;
    retireWrite.store (w + 1, std::memory_order_release);
    return true;
}

void AudioEngine::collectGarbage()
{
    auto r = retireRead.load (std::memory_order_relaxed);
    const auto w = retireWrite.load (std::memory_order_acquire);
    while (r != w)
    {
        delete retireRing[r % kRetireSlots];
        ++r;
    }
    retireRead.store (r, std::memory_order_release);
}

// ================================================================================================
// Transport (message thread)
// ================================================================================================
void AudioEngine::play()
{
    if (playing.load()) return;
    playStartBeats = positionBeats();
    playing.store (true);
}

void AudioEngine::stop()
{
    playing.store (false);
    seekBeats (playStartBeats);
}

void AudioEngine::pause()
{
    playing.store (false);
    playStartBeats = positionBeats();
}

void AudioEngine::seekBeats (double beat)
{
    beat = std::max (0.0, beat);
    seekRequestBeats.store (beat);
    if (! playing.load()) playStartBeats = beat;
}

double AudioEngine::positionBeats() const
{
    const double pending = seekRequestBeats.load();
    return pending >= 0.0 ? pending : publishedBeats.load();
}

juce::var AudioEngine::describeState() const
{
    const auto& p = controller.project();
    const double beat = positionBeats();
    auto state = makeObject ({ { "available", true }, { "playing", isPlaying() }, { "beat", beat },
                               { "position", formatBarBeat (beatsToTicks (beat)) },
                               { "seconds", beatsToSeconds (beat, p.bpm) }, { "bpm", p.bpm },
                               { "loopEnabled", p.loopEnabled }, { "sampleRate", sampleRate.load() },
                               { "clipped", clipped() }, { "deviceErrors", deviceErrors.load() } });
    if (deviceManager != nullptr)
        if (auto* device = deviceManager->getCurrentAudioDevice())
        {
            auto* o = state.getDynamicObject();
            o->setProperty ("device", device->getName());
            o->setProperty ("deviceType", device->getTypeName());
            o->setProperty ("bufferSize", device->getCurrentBufferSizeSamples());
            o->setProperty ("outputLatencyMs", 1000.0 * device->getOutputLatencyInSamples() / std::max (1.0, device->getCurrentSampleRate()));
            o->setProperty ("cpuLoad", deviceManager->getCpuUsage());
            o->setProperty ("underruns", device->getXRunCount());
        }
    return state;
}

AudioEngine::StereoPeak AudioEngine::takeTrackPeak (const juce::String& id)
{
    if (auto rt = registry->findTrack (id))
        return { rt->peakL.exchange (0.0f), rt->peakR.exchange (0.0f) };
    return {};
}

AudioEngine::StereoPeak AudioEngine::takeMasterPeak()
{
    auto rt = registry->master();
    return { rt->peakL.exchange (0.0f), rt->peakR.exchange (0.0f) };
}

float AudioEngine::effectTelemetry (const juce::String& id) const
{
    if (auto rt = registry->findEffect (id)) return rt->processor->telemetry (0);
    return 0.0f;
}

double AudioEngine::cpuLoad() const { return deviceManager ? deviceManager->getCpuUsage() : 0.0; }

int AudioEngine::underrunCount() const
{
    if (deviceManager != nullptr)
        if (auto* d = deviceManager->getCurrentAudioDevice()) return d->getXRunCount();
    return 0;
}

// ================================================================================================
// Preview
// ================================================================================================
void AudioEngine::startPreview (AudioDataPtr data, float gainDb)
{
    if (data == nullptr || data->numFrames() < 2) return;
    auto* voice = new PreviewVoice();
    voice->data = std::move (data);
    voice->step = voice->data->sampleRate / sampleRate.load();
    voice->gain = static_cast<float> (dbToGain (gainDb));
    previewStopRequested.store (false);
    delete pendingPreview.exchange (voice, std::memory_order_acq_rel);
    previewActive.store (true);
}

void AudioEngine::stopPreview()
{
    delete pendingPreview.exchange (nullptr, std::memory_order_acq_rel);
    previewStopRequested.store (true);
}

// ================================================================================================
// Device callbacks
// ================================================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    if (sr > 0 && sr != sampleRate.load())
    {
        sampleRate.store (sr);
        triggerAsyncUpdate();   // rebuild the plan (and effect runtimes) at the new rate on the message thread
    }
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceError (const juce::String&)
{
    deviceErrors.fetch_add (1);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    // 1. Adopt a new plan if one is waiting and there is room to retire the old one.
    if (retireWrite.load (std::memory_order_relaxed) - retireRead.load (std::memory_order_acquire) < kRetireSlots - 2)
    {
        if (auto* next = pendingPlan.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (activePlan != nullptr)
            {
                // Preserve the musical position across tempo / sample-rate changes.
                const auto& oldPlan = *activePlan->plan;
                const auto& newPlan = *next->plan;
                if (oldPlan.bpm != newPlan.bpm || oldPlan.sampleRate != newPlan.sampleRate)
                    positionSamples = newPlan.beatsToSamples (oldPlan.samplesToBeats (positionSamples));
                retire (activePlan);
            }
            activePlan = next;
        }

        if (previewStopRequested.exchange (false, std::memory_order_acq_rel) && activePreview != nullptr)
        {
            retire (activePreview);
            activePreview = nullptr;
            previewActive.store (false, std::memory_order_relaxed);
        }
        if (auto* voice = pendingPreview.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (activePreview != nullptr) retire (activePreview);
            activePreview = voice;
        }
    }

    // Clear everything first: silence is the safe default on every early-out path.
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr) juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (activePlan == nullptr || numOutputs <= 0) return;
    const auto& plan = *activePlan->plan;
    if (plan.sampleRate != sampleRate.load (std::memory_order_relaxed)) return;   // rebuild pending

    const double seek = seekRequestBeats.exchange (-1.0, std::memory_order_acq_rel);
    if (seek >= 0.0) positionSamples = plan.beatsToSamples (seek);

    const bool isPlayingNow = playing.load (std::memory_order_relaxed);
    float* outL = outputs[0];
    float* outR = numOutputs > 1 ? outputs[1] : nullptr;

    // 2. Render in chunks no larger than the renderer's preallocated block.
    float scratchR[PlanRenderer::kMaxBlock];
    for (int offset = 0; offset < numSamples; offset += PlanRenderer::kMaxBlock)
    {
        const int n = std::min (PlanRenderer::kMaxBlock, numSamples - offset);
        float* l = outL + offset;
        float* r = outR != nullptr ? outR + offset : scratchR;
        const auto result = renderer.render (plan, positionSamples, isPlayingNow, l, r, n);
        positionSamples = result.newPosition;
        if (result.masterPeakL > 1.0f || result.masterPeakR > 1.0f) clipLatch.store (true, std::memory_order_relaxed);
        if (outR == nullptr) for (int i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + r[i]);
    }

    // 3. Browser preview is mixed post-master (it is an audition, not part of the song).
    if (activePreview != nullptr)
    {
        auto& v = *activePreview;
        const auto& pcm = v.data->pcm;
        const int frames = pcm.getNumSamples();
        const float* srcL = pcm.getReadPointer (0);
        const float* srcR = pcm.getReadPointer (pcm.getNumChannels() > 1 ? 1 : 0);
        for (int i = 0; i < numSamples && v.position < frames - 1; ++i)
        {
            const int i0 = static_cast<int> (v.position);
            const float frac = static_cast<float> (v.position - i0);
            outL[i] += v.gain * (srcL[i0] + frac * (srcL[i0 + 1] - srcL[i0]));
            if (outR != nullptr) outR[i] += v.gain * (srcR[i0] + frac * (srcR[i0 + 1] - srcR[i0]));
            v.position += v.step;
        }
        if (v.position >= frames - 1 && retire (activePreview))
        {
            activePreview = nullptr;
            previewActive.store (false, std::memory_order_relaxed);
        }
    }

    publishedBeats.store (plan.samplesToBeats (positionSamples), std::memory_order_relaxed);
}
} // namespace mcpdaw
