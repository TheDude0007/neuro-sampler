#pragma once

#include "KeyDetector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace neuro
{

constexpr uint16_t pitchClassMask (std::initializer_list<int> intervals) noexcept
{
    uint16_t mask = 0;

    for (auto i : intervals)
        mask = (uint16_t) (mask | (1u << (i % 12)));

    return mask;
}

struct ChordType { const char* suffix; uint16_t mask; };

// Relative to the chord root. Order is the tie-break: simpler chords first.
inline constexpr std::array<ChordType, 16> chordTypes {{
    { "",     pitchClassMask ({ 0, 4, 7 }) },     { "m",    pitchClassMask ({ 0, 3, 7 }) },
    { "dim",  pitchClassMask ({ 0, 3, 6 }) },     { "aug",  pitchClassMask ({ 0, 4, 8 }) },
    { "sus2", pitchClassMask ({ 0, 2, 7 }) },     { "sus4", pitchClassMask ({ 0, 5, 7 }) },
    { "7",    pitchClassMask ({ 0, 4, 7, 10 }) }, { "maj7", pitchClassMask ({ 0, 4, 7, 11 }) },
    { "m7",   pitchClassMask ({ 0, 3, 7, 10 }) }, { "m7b5", pitchClassMask ({ 0, 3, 6, 10 }) },
    { "dim7", pitchClassMask ({ 0, 3, 6, 9 }) },  { "6",    pitchClassMask ({ 0, 4, 7, 9 }) },
    { "m6",   pitchClassMask ({ 0, 3, 7, 9 }) },  { "add9", pitchClassMask ({ 0, 2, 4, 7 }) },
    { "madd9", pitchClassMask ({ 0, 2, 3, 7 }) }, { "mmaj7", pitchClassMask ({ 0, 3, 7, 11 }) },
}};

struct ChordEstimate
{
    enum class Status { chord, uncertain, none };
    Status status = Status::none;
    int root = 0;        // pitch class
    int type = 0;        // index into chordTypes
    int bass = -1;       // pitch class of the bass note when it is a chord tone other than the root (an inversion)
    float score = 0.0f;  // cosine similarity of the chroma with the chord, plus the bass bonus

    uint16_t mask() const noexcept // absolute pitch classes
    {
        const auto m = (uint32_t) chordTypes[(size_t) type].mask;
        return (uint16_t) (((m << root) | (m >> (12 - root))) & 0xFFF);
    }

    std::string name() const
    {
        auto n = std::string (pitchClassName (root)) + chordTypes[(size_t) type].suffix;
        return bass >= 0 ? n + "/" + pitchClassName (bass) : n;
    }
};

// Names the chord in a region from its chroma (template matching, cosine similarity). The bass chroma breaks ties
// between chords with the same notes (Am7 vs C6) and marks inversions. Needs at least 3 clear pitch classes; for a region
// the pitch detector calls a single note, also check chordTonesAllStrong.
inline ChordEstimate detectChord (const std::array<float, 12>& chroma, const std::array<float, 12>& bassChroma)
{
    ChordEstimate best;
    double norm = 0.0;
    int present = 0;

    for (auto c : chroma)
    {
        norm += (double) c * c;
        present += c > 0.08f ? 1 : 0;
    }

    if (norm <= 0.0 || present < 3)
        return best;

    // The bass note: the strongest low pitch class, when the region has a clear one.
    int bassPc = -1;
    float bassShare = 0.0f;

    for (int pc = 0; pc < 12; ++pc)
        if (bassChroma[(size_t) pc] > bassShare)
        {
            bassShare = bassChroma[(size_t) pc];
            bassPc = pc;
        }

    if (bassShare < 0.25f)
        bassPc = -1;

    float runnerUp = 0.0f;

    for (int root = 0; root < 12; ++root)
    {
        for (size_t t = 0; t < chordTypes.size(); ++t)
        {
            ChordEstimate c;
            c.root = root;
            c.type = (int) t;
            const auto mask = c.mask();
            double dot = 0.0;
            int notes = 0;

            for (int pc = 0; pc < 12; ++pc)
                if ((mask >> pc) & 1u)
                {
                    dot += chroma[(size_t) pc];
                    ++notes;
                }

            c.score = (float) (dot / std::sqrt (norm * notes)) - 0.01f * (float) (notes - 3);

            if (bassPc == root)
                c.score += 0.05f;

            if (c.score > best.score + 1.0e-6f)
            {
                runnerUp = best.score;
                best = c;
            }
            else
            {
                runnerUp = std::max (runnerUp, c.score);
            }
        }
    }

    if (bassPc >= 0 && bassPc != best.root && ((best.mask() >> bassPc) & 1u))
        best.bass = bassPc;

    best.status = best.score >= 0.8f && best.score - runnerUp >= 0.01f ? ChordEstimate::Status::chord
                  : best.score >= 0.65f                                ? ChordEstimate::Status::uncertain
                                                                       : ChordEstimate::Status::none;
    return best;
}

// A single note's overtones can look like a chord: a major triad is harmonics 4:5:6. When the pitch detector hears one
// clear note, a chord counts only if its weakest tone holds at least 30% of its strongest (an overtone holds ~10%).
inline bool chordTonesAllStrong (const ChordEstimate& chord, const std::array<float, 12>& chroma) noexcept
{
    float weakest = 1.0e9f, strongest = 0.0f;
    const auto mask = chord.mask();

    for (int pc = 0; pc < 12; ++pc)
        if ((mask >> pc) & 1u)
        {
            weakest = std::min (weakest, chroma[(size_t) pc]);
            strongest = std::max (strongest, chroma[(size_t) pc]);
        }

    return strongest > 0.0f && weakest >= 0.3f * strongest;
}

} // namespace neuro
