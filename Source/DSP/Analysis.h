#pragma once

#include "../Audio/SampleData.h"
#include "../Core/ChordDetector.h"
#include "../Core/KeyDetector.h"
#include "../Core/PitchDetector.h"
#include "../Core/Timbre.h"

namespace neuro
{

// Analysis of one region: a slice (sliceId > 0) or the whole sample (sliceId 0). Every region gets pitch, key, chord and timbre.
struct AnalysisJob
{
    std::shared_ptr<const SampleData> sample;
    int sampleVersion = 0;
    int sliceId = 0;
    int64_t start = 0, end = 0;
};

struct AnalysisResult
{
    int sampleVersion = 0;
    int sliceId = 0;
    int64_t start = 0, end = 0;
    PitchEstimate pitch;
    KeyEstimate key;
    ChordEstimate chord; // none for single notes
    TimbreFeatures timbre;
    DrumClass drumClass = DrumClass::perc;

    // Musical content: a clear single note or a recognised chord.
    bool harmonic() const noexcept
    {
        return pitch.status == PitchEstimate::Status::pitched || chord.status == ChordEstimate::Status::chord;
    }

    bool covers (int64_t s, int64_t e) const noexcept { return start == s && end == e; }
};

inline AnalysisResult analyseRegion (const AnalysisJob& job)
{
    AnalysisResult r { job.sampleVersion, job.sliceId, job.start, job.end, {}, {}, {}, {}, DrumClass::perc };
    const auto& buffer = job.sample->buffer;
    const auto start = juce::jlimit<int64_t> (0, buffer.getNumSamples(), job.start);
    const auto end = juce::jlimit<int64_t> (start, buffer.getNumSamples(), job.end);
    std::vector<const float*> channels;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        channels.push_back (buffer.getReadPointer (ch, (int) start));

    r.pitch = detectPitch (channels.data(), (int) channels.size(), end - start, job.sample->sampleRate);

    r.timbre = analyseTimbre (channels.data(), (int) channels.size(), end - start, job.sample->sampleRate);
    r.drumClass = classify (r.timbre, r.pitch.status == PitchEstimate::Status::pitched);

    r.key = detectKey (channels.data(), (int) channels.size(), end - start, job.sample->sampleRate);

    r.chord = detectChord (r.key.chroma, r.key.bassChroma);

    if (r.pitch.status == PitchEstimate::Status::pitched && ! r.pitch.ambiguous && ! chordTonesAllStrong (r.chord, r.key.chroma))
        r.chord.status = ChordEstimate::Status::none; // one note and its overtones

    return r;
}

} // namespace neuro
