#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuro
{

inline const char* pitchClassName (int pc) noexcept
{
    static constexpr const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}

struct KeyEstimate
{
    enum class Status { key, uncertain, unavailable };
    struct Candidate { int tonic = 0; bool minor = false; float correlation = 0.0f; };

    Status status = Status::unavailable;
    std::array<Candidate, 3> candidates {}; // best first; meaningful unless unavailable
    std::array<float, 12> chroma {};        // normalised pitch-class energy
    std::array<float, 12> bassChroma {};    // C2-B3 only, weighted toward the lowest notes (the bass note of a chord)
    float margin = 0.0f;                    // best minus second-best correlation
};

// Chroma (Goertzel bank over C2-B7, Hann-windowed frames) correlated against the 24 "bgate" key profiles (Faraldo et al.,
// as published in Essentia). For the key, each note's expected overtones (octave, 12th, 2 octaves, 17th) are partly
// subtracted first, so a note's 3rd harmonic doesn't pull the key toward its dominant. On labelled real melodies this
// scored 5/7 against 3/7 for Krumhansl-Kessler on raw chroma (Documentation/PITCH_ANALYSIS.md).
// Returns uncertain when evidence is weak: low correlation, a near-tie, or too few pitch classes (e.g. a single note).
inline KeyEstimate detectKey (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate,
                              double maxSeconds = 60.0)
{
    static constexpr double major[12] { 1.00, 0.00, 0.42, 0.00, 0.53, 0.37, 0.00, 0.77, 0.00, 0.38, 0.21, 0.30 };
    static constexpr double minor[12] { 1.00, 0.00, 0.36, 0.39, 0.00, 0.38, 0.00, 0.74, 0.27, 0.00, 0.42, 0.23 };
    static constexpr int overtones[4] { 12, 19, 24, 28 };
    static constexpr double overtoneWeight[4] { 0.3 / 1.0, 0.3 / 1.5, 0.3 / 2.0, 0.3 / 2.5 };
    constexpr double pi = 3.14159265358979323846;

    KeyEstimate result;

    if (channels == nullptr || numChannels <= 0 || sampleRate <= 0.0)
        return result;

    const auto frame = (int) std::pow (2.0, std::ceil (std::log2 (0.17 * sampleRate))); // 8192 at 44.1/48 kHz
    const auto n = (int64_t) std::min<double> ((double) numSamples, maxSeconds * sampleRate);

    std::vector<double> window ((size_t) frame), buf ((size_t) frame), coeff;

    for (int i = 0; i < frame; ++i)
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / (frame - 1));

    for (int note = 36; note < 108; ++note)
        coeff.push_back (2.0 * std::cos (2.0 * pi * 440.0 * std::pow (2.0, (note - 69) / 12.0) / sampleRate));

    std::array<double, 12> chroma {}, bass {}, keyChroma {};
    std::vector<double> notes (coeff.size());
    int frames = 0;

    // Short inputs are analysed as one zero-padded frame.
    for (int64_t start = 0; start == 0 || start + frame <= n; start += frame / 2)
    {
        double energy = 0.0;

        for (int i = 0; i < frame; ++i)
        {
            double s = 0.0;

            if (start + i < n)
                for (int ch = 0; ch < numChannels; ++ch)
                    s += channels[ch][start + i];

            buf[(size_t) i] = s / numChannels * window[(size_t) i];
            energy += buf[(size_t) i] * buf[(size_t) i];
        }

        if (energy < 1.0e-6 * frame)
            continue;

        ++frames;

        for (size_t k = 0; k < coeff.size(); ++k)
        {
            double s1 = 0.0, s2 = 0.0;

            for (int i = 0; i < frame; ++i)
            {
                const auto s0 = buf[(size_t) i] + coeff[k] * s1 - s2;
                s2 = s1;
                s1 = s0;
            }

            const auto magnitude = std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coeff[k] * s1 * s2));
            notes[k] = magnitude;
            chroma[(36 + k) % 12] += magnitude;

            if (k < 24) // C2-B3, weighted toward the lowest notes: the bass note, not a low chord tone
                bass[(36 + k) % 12] += magnitude * (double) (24 - k) / 24.0;
        }

        for (size_t k = 0; k < notes.size(); ++k)
        {
            auto fundamental = notes[k];

            for (int o = 0; o < 4; ++o)
                if (k >= (size_t) overtones[o])
                    fundamental -= overtoneWeight[o] * notes[k - (size_t) overtones[o]];

            keyChroma[(36 + k) % 12] += std::max (0.0, fundamental);
        }
    }

    double total = 0.0;

    for (auto c : chroma)
        total += c;

    if (frames == 0 || total <= 0.0)
        return result;

    int present = 0;
    double bassTotal = 0.0;

    for (auto b : bass)
        bassTotal += b;

    for (size_t i = 0; i < 12; ++i)
        result.bassChroma[i] = bassTotal > 0.0 ? (float) (bass[i] / bassTotal) : 0.0f;


    for (size_t i = 0; i < 12; ++i)
    {
        result.chroma[i] = (float) (chroma[i] / total);
        present += result.chroma[i] > 0.05f ? 1 : 0;
    }

    double keyTotal = 0.0;

    for (auto c : keyChroma)
        keyTotal += c;

    std::array<double, 12> forKey {};

    for (size_t i = 0; i < 12; ++i)
        forKey[i] = keyTotal > 0.0 ? keyChroma[i] / keyTotal : result.chroma[i];

    auto correlate = [&] (const double* profile, int tonic)
    {
        double mx = 0.0, my = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            mx += forKey[(size_t) i];
            my += profile[(i - tonic + 12) % 12];
        }

        mx /= 12.0;
        my /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            const auto dx = forKey[(size_t) i] - mx, dy = profile[(i - tonic + 12) % 12] - my;
            sxy += dx * dy;
            sxx += dx * dx;
            syy += dy * dy;
        }

        return sxx > 0.0 ? (float) (sxy / std::sqrt (sxx * syy)) : 0.0f;
    };

    std::vector<KeyEstimate::Candidate> all;

    for (int t = 0; t < 12; ++t)
    {
        all.push_back ({ t, false, correlate (major, t) });
        all.push_back ({ t, true, correlate (minor, t) });
    }

    std::sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.correlation > b.correlation; });
    std::copy_n (all.begin(), 3, result.candidates.begin());
    result.margin = all[0].correlation - all[1].correlation;
    result.status = all[0].correlation >= 0.6f && result.margin >= 0.05f && present >= 4 ? KeyEstimate::Status::key
                                                                                         : KeyEstimate::Status::uncertain;
    return result;
}

} // namespace neuro
