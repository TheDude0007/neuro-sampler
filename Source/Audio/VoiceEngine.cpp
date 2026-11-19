#include "VoiceEngine.h"

namespace neuro
{

void VoiceEngine::prepare (double hostSampleRate) noexcept
{
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    killAll();
}

void VoiceEngine::noteOn (int note, float velocity, const SampleData& sample, const SliceSet& slices,
                          const StretchBank* bank, const PadMap* pads, const VoiceParams& params, float pitchOffset) noexcept
{
    velocity = juce::jlimit (0.0f, 1.0f, velocity);

    // Chords: Harmonize turns any triggered slice into the key's chord on its note (Keys mode: on the key played);
    // Chord pads lets the pad pick the chord.
    auto trigger = [&] (int index, float level, float pitch, int choke, int padIndex)
    {
        if (params.chordPads == 0 || ! playChord (index, note, level, pitch, choke, padIndex, sample, slices, bank, params))
            playSlice (index, note, level, pitch, choke, sample, slices, bank, params);
    };

    if (params.keyboardMode)
    {
        const auto offset = (float) (snapToScale (note, params.scaleMask, params.scaleRoot) - (double) params.keyboardRoot);
        trigger (params.keyboardSlice, velocity, offset + pitchOffset, 0, -1);
    }
    else if (pads == nullptr)
    {
        trigger (note - baseNote, velocity, pitchOffset, 0, note - baseNote);
    }
    else
    {
        for (size_t i = 0; i < pads->size(); ++i)
        {
            const auto& pad = (*pads)[i];

            if (pad.note != note)
                continue;

            if (params.chordPads == 2) // the pad is a chord, played with the keyboard slice (empty pads too)
                playChord (params.keyboardSlice, note, pad.levelFor (velocity), pad.pitch + pitchOffset, pad.chokeGroup, (int) i,
                           sample, slices, bank, params);
            else if (pad.sliceId > 0)
                trigger (slices.indexOfId (pad.sliceId), pad.levelFor (velocity), pad.pitch + pitchOffset, pad.chokeGroup, (int) i);
        }
    }
}

bool VoiceEngine::playChord (int sourceIndex, int noteTag, float level, float pitchOffset, int chokeGroup, int padIndex,
                             const SampleData& sample, const SliceSet& slices, const StretchBank* bank, const VoiceParams& params) noexcept
{
    if (sourceIndex < 0 || sourceIndex >= slices.size())
        return false;

    const auto& all = slices.getSlices();
    const auto& source = all[(size_t) sourceIndex];
    const auto padChord = params.chordPads == 2 && padIndex >= 0 && ! params.keyboardMode;
    auto rootOf = [] (const SliceDefinition& s) { return s.rootNote >= 0.0f ? s.rootNote : s.detectedRoot; };

    // The source's pitch: the keyboard slice's root (manual, detected, else C4) for Chord pads and Keys mode; otherwise
    // a slice that can be harmonised needs a known pitch and must not be a drum.
    float sourceRoot = params.keyboardRoot;

    if (! padChord && ! params.keyboardMode)
    {
        if (! chordLockEligible (source))
            return false;

        sourceRoot = rootOf (source);
    }

    const auto keyShift = ! padChord && ! params.keyboardMode && keyShiftEligible (source, params.keyShiftAll) ? params.keyShift : 0;
    const auto sounding = sourceRoot + source.pitchSemitones + pitchOffset + (float) keyShift;

    DiatonicChord chord;
    auto shape = params.chordShape;
    int rootTarget = 0;

    if (padChord)
    {
        const auto inBank = padIndex % padsPerBank;
        shape = inBank >= 8 ? richerShape (shape) : shape;
        chord = diatonicChord (inBank % 8, params.scaleMask, params.scaleRoot, shape);
        rootTarget = nearestNoteWithPitchClass (sounding, chord.rootPc()) + (inBank % 8 == 7 ? 12 : 0);
    }
    else
    {
        chord = diatonicChordOn ((int) std::lround (sounding) % 12, params.scaleMask, params.scaleRoot, shape);
        rootTarget = nearestNoteWithPitchClass (sounding, chord.rootPc());
    }

    // One voice per chord tone: its slice (timbre chords: a slice per voice slot) and target note. Close voicing puts
    // each tone nearest its slice's own pitch, so every voice moves as little as possible (best audio quality).
    struct ChordVoice { int slice; float base, offset; int target; int role; };
    std::array<ChordVoice, DiatonicChord::maxTones> voicesOut {};
    const auto n = chord.numTones;

    for (int i = 0; i < n; ++i)
    {
        const auto role = chord.roles[(size_t) i];
        auto& v = voicesOut[(size_t) i];
        v.role = role;
        v.slice = sourceIndex;
        v.base = sounding - (float) keyShift; // the pitch before any shift (pitchOffset included)
        v.offset = pitchOffset;

        if (const auto id = params.voiceSliceIds[(size_t) role]; id > 0)
            if (const auto other = slices.indexOfId (id); other >= 0 && other != sourceIndex)
            {
                const auto r = rootOf (all[(size_t) other]);
                v.slice = other;
                v.offset = 0.0f;
                v.base = r >= 0.0f ? r + all[(size_t) other].pitchSemitones : -1.0f; // unpitched: plays as itself
            }

        if (i == 0)
            v.target = rootTarget;
        else if (chord.octave[(size_t) i] != 0 && chord.pcs[(size_t) i] == chord.rootPc())
            v.target = rootTarget + 12; // power chord octave
        else
            v.target = nearestNoteWithPitchClass (v.slice == sourceIndex ? (float) rootTarget : v.base, chord.pcs[(size_t) i]);

        for (int j = 0; j < i; ++j) // no two voices on one note
            if (voicesOut[(size_t) j].target == v.target)
                v.target += 12;

        if ((role == 1 || role == 3) && v.target < 52) // thirds and sevenths below E3 turn to mud: up an octave
            v.target += 12;
    }

    // Voicing.
    std::array<int, DiatonicChord::maxTones> order {};
    for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    auto byPitch = [&] { std::sort (order.begin(), order.begin() + n, [&] (int a, int b) { return voicesOut[(size_t) a].target < voicesOut[(size_t) b].target; }); };
    byPitch();

    if (params.chordVoicing == ChordVoicing::drop2 && n >= 3)
        voicesOut[(size_t) order[(size_t) n - 2]].target -= 12;
    else if (params.chordVoicing == ChordVoicing::wide && voicesOut[0].target - 12 >= 24)
        voicesOut[0].target -= 12;

    byPitch();

    // Choke once for the whole chord, then start the voices upwards (strum), at an equal-loudness level.
    if (chokeGroup > 0)
        for (auto& other : voices)
            if (other.active && other.chokeGroup == chokeGroup)
            {
                other.releasing = true;
                other.releaseStep = std::max (other.releaseStep, 1.0f / (chokeMs * 0.001f * (float) hostRate));
            }

    const auto voiceLevel = level / std::sqrt ((float) n);

    for (int k = 0; k < n; ++k)
    {
        const auto& v = voicesOut[(size_t) order[(size_t) k]];
        const auto shift = v.base >= 0.0f ? (float) v.target - v.base : 0.0f; // the key change is already in the target
        playSlice (v.slice, noteTag, voiceLevel, v.offset, chokeGroup, sample, slices, bank, params, shift, true, k * params.strumSamples);
    }

    lastChord.store (packChord (chord, shape), std::memory_order_relaxed);
    return true;
}

void VoiceEngine::playSlice (int index, int noteTag, float level, float pitchOffset, int chokeGroup, const SampleData& sample,
                             const SliceSet& slices, const StretchBank* bank, const VoiceParams& params, float voiceShift,
                             bool chordVoice, int delaySamples) noexcept
{
    if (index < 0 || index >= slices.size())
        return;

    const auto& slice = slices.getSlices()[(size_t) index];
    // Clamp to the buffer: the slice set may briefly belong to a previous sample during a swap.
    auto start = juce::jlimit<int64_t> (0, sample.length(), slice.startSample);
    auto end   = juce::jlimit<int64_t> (0, sample.length(), slice.endSample);

    if (! slice.enabled || end - start < 2)
        return;

    const auto* src = &sample.buffer;
    const auto displayOffset = start;
    auto displayScale = 1.0;
    auto pitch = (double) slice.pitchSemitones + pitchOffset; // slice pitch is resampled unless a keep-length render is used
    const auto keyShift = ! chordVoice && params.keyShift != 0 && keyShiftEligible (slice, params.keyShiftAll) ? params.keyShift : 0;
    auto extraShift = (float) keyShift + voiceShift; // key change + Chord-Lock (or a chord voice), on top of the slice's own pitch

    if (! chordVoice && params.chordMode != 0 && params.chordMask != 0 && chordLockEligible (slice))
        extraShift += chordLockOffset ((slice.rootNote >= 0.0f ? slice.rootNote : slice.detectedRoot) + (float) pitch + (float) keyShift,
                                       params.chordMask);

    // Keep-length key change and Chord-Lock use a variant rendered with the whole-semitone part of their shift.
    const auto extraRendered = chordVoice ? (params.chordKeepLength ? (int) std::lround (voiceShift) : 0)
                                          : (params.keyShiftKeepLength ? keyShift : 0)
                                                + (params.chordMode == 2 ? (int) std::lround (extraShift - (float) keyShift) : 0);
    const auto useRender = slice.needsRender() || extraRendered != 0;

    if (bank != nullptr && useRender)
    {
        if (const auto* entry = bank->find (slice, extraRendered); entry != nullptr && entry->audio->getNumSamples() >= 2)
        {
            src = entry->audio.get();
            start = 0;
            end = src->getNumSamples();
            displayScale = 1.0 / slice.timeStretchRatio;
            pitch -= slice.renderedPitch(); // already in the render
            extraShift -= (float) extraRendered;
        }
    }

    pitch += extraShift; // the remainder (or all of it, while a render is pending) by resampling

    if (chokeGroup > 0 && ! chordVoice) // a chord chokes once, in playChord
        for (auto& other : voices)
            if (other.active && other.chokeGroup == chokeGroup)
            {
                other.releasing = true;
                other.releaseStep = std::max (other.releaseStep, 1.0f / (chokeMs * 0.001f * (float) hostRate));
            }

    auto* v = &voices[0];

    for (auto& candidate : voices)
    {
        if (! candidate.active) { v = &candidate; break; }
        if (candidate.order < v->order) v = &candidate; // steal the oldest
    }

    const auto rate = sample.sampleRate / hostRate * std::exp2 (pitch / 12.0);

    v->active = true;
    v->releasing = false;
    v->note = params.oneShot ? -1 : noteTag; // one-shot voices ignore note-off
    v->chokeGroup = chokeGroup;
    v->drumClass = slice.effectiveClass();
    v->src = src;
    v->displayOffset = displayOffset;
    v->displayScale = displayScale;
    v->start = start;
    v->end = end;
    v->baseRate = slice.reverse ? -rate : rate;
    v->pos = slice.reverse ? (double) (end - 1) : (double) start;
    v->gain = std::max (0.0f, level) * slice.gain;
    v->attackStep = 1.0f / (params.attackMs * 0.001f * (float) hostRate + 1.0f);
    v->releaseStep = 1.0f / (params.releaseMs * 0.001f * (float) hostRate + 1.0f);
    v->env = params.attackMs > 0.0f ? 0.0f : 1.0f;
    v->delay = std::max (0, delaySamples);
    v->order = ++counter;
}

void VoiceEngine::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && v.note == note)
            v.releasing = true;
}

void VoiceEngine::killAll() noexcept
{
    for (auto& v : voices)
        v.active = false;

    playhead.store (-1, std::memory_order_relaxed);
}

void VoiceEngine::dropVoicesNotIn (const StretchBank* bank, const juce::AudioBuffer<float>* sampleBuffer) noexcept
{
    for (auto& v : voices)
        if (v.active && v.src != sampleBuffer && (bank == nullptr || ! bank->contains (v.src)))
            v.active = false;
}

void VoiceEngine::render (const OutputBuses& buses, int startSample, int numSamples, const float* modSemitones) noexcept
{
    constexpr double declick = 64.0; // samples of fade before a slice's end
    constexpr int modInterval = 16;  // pitch modulation update rate (samples)
    const Voice* newest = nullptr;

    for (auto& v : voices)
    {
        const auto srcChannels = v.active ? v.src->getNumChannels() : 0;
        const auto hasClass = v.drumClass >= 0 && v.drumClass < numDrumClasses;
        auto& out = *(hasClass && buses.classes[(size_t) v.drumClass] != nullptr ? buses.classes[(size_t) v.drumClass] : buses.main);
        const auto outChannels = out.getNumChannels();
        const auto classLevel = hasClass ? buses.levels[(size_t) v.drumClass] : 1.0f;
        double rate = v.baseRate;

        for (int i = 0; i < numSamples && v.active; ++i)
        {
            if (i % modInterval == 0)
                rate = v.baseRate * std::exp2 ((double) modSemitones[i] / 12.0);

            if (v.delay > 0) // strummed: not started yet
            {
                --v.delay;
                continue;
            }

            auto idx = (int64_t) std::floor (v.pos);

            if (idx < v.start || idx >= v.end)
            {
                v.active = false;
                break;
            }

            if (v.releasing)
            {
                v.env -= v.releaseStep;

                if (v.env <= 0.0f)
                {
                    v.active = false;
                    break;
                }
            }
            else if (v.env < 1.0f)
            {
                v.env = std::min (1.0f, v.env + v.attackStep);
            }

            auto remaining = rate > 0.0 ? ((double) v.end - v.pos) / rate : (v.pos - (double) v.start) / -rate;
            auto g = v.gain * classLevel * v.env * (float) std::min (1.0, remaining / declick);
            auto frac = (float) (v.pos - (double) idx);
            auto next = idx + 1 < v.end ? idx + 1 : idx;

            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto* src = v.src->getReadPointer (std::min (ch, srcChannels - 1));
                auto a = src[idx];
                out.addSample (ch, startSample + i, (a + frac * (src[next] - a)) * g);
            }

            v.pos += rate;
        }

        if (v.active && (newest == nullptr || v.order > newest->order))
            newest = &v;
    }

    playhead.store (newest != nullptr ? newest->displayOffset + (int64_t) ((newest->pos - (double) newest->start) * newest->displayScale) : -1,
                    std::memory_order_relaxed);
}

} // namespace neuro
