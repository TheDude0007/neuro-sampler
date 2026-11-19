#pragma once

#include "ChordDetector.h"
#include "Scales.h"
#include <array>
#include <cmath>
#include <string>

// Diatonic chords from the song key: chords stacked in thirds (or sus/power shapes) on a scale degree, their names and
// Roman numerals, and the note nearest a slice's own pitch for each chord tone (so a slice moves as little as possible).
namespace neuro
{

enum class ChordShape { triad, seventh, ninth, sus2, sus4, power, sevenSus4 };
inline constexpr const char* chordShapeNames[] { "Triad", "7th", "9th", "sus2", "sus4", "Power", "7sus4" };
enum class ChordVoicing { close, drop2, wide };
inline constexpr const char* chordVoicingNames[] { "Close", "Drop 2", "Wide" };

// The shape on the upper two pad rows in Chord pads mode: one step richer.
inline ChordShape richerShape (ChordShape s) noexcept
{
    switch (s)
    {
        case ChordShape::triad:     return ChordShape::seventh;
        case ChordShape::seventh:   return ChordShape::ninth;
        case ChordShape::sus2:
        case ChordShape::sus4:      return ChordShape::sevenSus4;
        case ChordShape::power:     return ChordShape::triad;
        case ChordShape::ninth:
        case ChordShape::sevenSus4: return ChordShape::ninth;
    }

    return ChordShape::seventh;
}

struct DiatonicChord
{
    static constexpr int maxTones = 5;
    int degree = 0;                     // 0-based scale degree
    int numTones = 0;
    std::array<int, maxTones> pcs {};   // pitch classes, root first, then upwards in stacking order
    std::array<int, maxTones> roles {}; // voice slot: 0 root, 1 third (or sus tone), 2 fifth, 3 seventh/ninth
    std::array<int, maxTones> octave {}; // 0, or 1 for a tone stacked past the octave (the ninth, power's octave)
    int rootPc() const noexcept { return pcs[0]; }
    uint16_t mask() const noexcept
    {
        uint16_t m = 0;
        for (int i = 0; i < numTones; ++i)
            m = (uint16_t) (m | (1u << pcs[(size_t) i]));
        return m;
    }

    // "Dm7", "G9", "Bdim"... (chord types from ChordDetector, ninths named from their seventh chord).
    std::string name() const
    {
        auto withoutNinth = *this;
        const auto ninth = numTones == 5;

        if (ninth)
            withoutNinth.numTones = 4;

        const auto m = withoutNinth.mask();
        const auto r = rootPc();
        const auto rel = (uint16_t) (((m >> r) | (m << (12 - r))) & 0xFFF);
        std::string suffix = "?";
        bool found = false;

        for (const auto& t : chordTypes)
            if (t.mask == rel)
            {
                suffix = t.suffix;
                found = true;
                break;
            }

        if (! found && rel == pitchClassMask ({ 0, 7 }))
            suffix = "5";
        else if (! found && rel == pitchClassMask ({ 0, 5, 7, 10 }))
            suffix = "7sus4";

        if (ninth)
            suffix = suffix == "maj7" ? "maj9" : suffix == "m7" ? "m9" : suffix == "7" ? "9" : suffix == "7sus4" ? "9sus4" : suffix + "(9)";

        return std::string (pitchClassName (r)) + suffix;
    }

    // "ii7", "V9", "vii°", "IVsus4"...
    std::string numeral() const
    {
        static constexpr const char* upper[] { "I", "II", "III", "IV", "V", "VI", "VII" };
        static constexpr const char* lower[] { "i", "ii", "iii", "iv", "v", "vi", "vii" };
        const auto r = rootPc();
        bool minor3 = false, major3 = false, dim5 = false, aug5 = false, sus = false;

        for (int i = 1; i < numTones; ++i)
        {
            const auto iv = (pcs[(size_t) i] - r + 12) % 12;
            if (roles[(size_t) i] == 1) { minor3 |= iv == 3; major3 |= iv == 4; sus |= iv == 2 || iv == 5; }
            if (roles[(size_t) i] == 2) { dim5 |= iv == 6; aug5 |= iv == 8; }
        }

        const auto d = degree % 7;
        std::string n = minor3 && ! major3 ? lower[d] : upper[d];
        if (dim5 && minor3) n += "°";
        if (aug5 && major3) n += "+";
        if (sus) n += "sus";
        if (numTones >= 4 && roles[3] == 3) n += numTones == 5 ? "9" : "7";
        return n;
    }
};

// The scale's pitch classes upwards from its root. Chromatic (or < 5 notes) falls back to major: stacking thirds needs
// a diatonic-like scale. Pentatonics stack into open, quartal-ish chords.
inline int scalePitchClasses (uint16_t mask, int scaleRoot, std::array<int, 12>& out) noexcept
{
    if ((mask & 0xFFF) == 0xFFF || __builtin_popcount (mask & 0xFFF) < 5)
        mask = scales[(size_t) scaleMajor].mask;

    int n = 0;

    for (int i = 0; i < 12; ++i)
        if ((mask >> i) & 1u)
            out[(size_t) n++] = (scaleRoot + i) % 12;

    return n;
}

// The diatonic chord on scale degree `degree` (0-based; wraps) of the key.
inline DiatonicChord diatonicChord (int degree, uint16_t mask, int scaleRoot, ChordShape shape) noexcept
{
    std::array<int, 12> scale {};
    const auto n = scalePitchClasses (mask, ((scaleRoot % 12) + 12) % 12, scale);
    struct Step { int step, role; };
    std::array<Step, DiatonicChord::maxTones> steps {};
    int count = 0;
    auto add = [&] (int step, int role) { steps[(size_t) count++] = { step, role }; };

    switch (shape)
    {
        case ChordShape::triad:     add (0, 0); add (2, 1); add (4, 2); break;
        case ChordShape::seventh:   add (0, 0); add (2, 1); add (4, 2); add (6, 3); break;
        case ChordShape::ninth:     add (0, 0); add (2, 1); add (4, 2); add (6, 3); add (8, 3); break;
        case ChordShape::sus2:      add (0, 0); add (1, 1); add (4, 2); break;
        case ChordShape::sus4:      add (0, 0); add (3, 1); add (4, 2); break;
        case ChordShape::power:     add (0, 0); add (4, 2); add (7, 0); break;
        case ChordShape::sevenSus4: add (0, 0); add (3, 1); add (4, 2); add (6, 3); break;
    }

    DiatonicChord c;
    c.degree = ((degree % n) + n) % n;
    c.numTones = count;

    for (int i = 0; i < count; ++i)
    {
        const auto idx = c.degree + steps[(size_t) i].step;
        c.pcs[(size_t) i] = scale[(size_t) (idx % n)];
        c.roles[(size_t) i] = steps[(size_t) i].role;
        c.octave[(size_t) i] = idx - c.degree >= n ? 1 : 0;
    }

    // Power: the octave tone is the root again, an octave up.
    if (shape == ChordShape::power)
        c.octave[2] = 1;

    return c;
}

// The diatonic chord whose root is pitch class `rootPc` (snapped into the scale first). Returns the degree used.
inline DiatonicChord diatonicChordOn (int rootPc, uint16_t mask, int scaleRoot, ChordShape shape) noexcept
{
    std::array<int, 12> scale {};
    const auto n = scalePitchClasses (mask, ((scaleRoot % 12) + 12) % 12, scale);
    int best = 0, bestDistance = 99;

    for (int i = 0; i < n; ++i)
    {
        const auto d = std::abs (((scale[(size_t) i] - rootPc) % 12 + 18) % 12 - 6); // circular distance, 0 .. 6
        if (d < bestDistance) { bestDistance = d; best = i; }
    }

    return diatonicChord (best, mask, scaleRoot, shape);
}

// The MIDI note with pitch class `pc` nearest `reference` (a fractional MIDI pitch); ties go down.
inline int nearestNoteWithPitchClass (float reference, int pc) noexcept
{
    const auto base = (int) std::floor (reference);
    int best = base, bestDistance = 1 << 30; // in cents

    for (int n = base - 6; n <= base + 7; ++n)
        if (((n % 12) + 12) % 12 == ((pc % 12) + 12) % 12)
            if (const auto d = (int) std::lround (std::abs ((float) n - reference) * 100.0f); d < bestDistance)
            {
                bestDistance = d;
                best = n;
            }

    return best;
}

} // namespace neuro
