#pragma once

#include "ProgressionData.h"
#include <algorithm>
#include <cctype>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Theory brain (v1.12): "what comes next" and progression generation in scale degrees (0 = I/i .. 6 = VII), learned
// from real progressions: the built-in free-midi-chords set (MIT), plus the user's own chord library when present
// (read at runtime, never shipped). A small functional-harmony prior (V->I, IV->V, ii->V, ...) keeps rare moves sensible
// and fills in transitions the data never shows.
namespace neuro
{

struct Progression
{
    bool minor = false;
    std::vector<int> degrees;
    std::string numerals, moods, source;
};

// "i", "VI7", "iv", "ii5", "Vsus4", "viidim" -> degree 0..6; borrowed ("bVI", "#iv") or unknown -> -1.
inline int degreeOfNumeral (std::string_view n)
{
    if (n.empty() || n[0] == 'b' || n[0] == '#')
        return -1;

    static constexpr std::string_view romans[] { "vii", "vi", "iv", "v", "iii", "ii", "i" };
    static constexpr int degrees[] { 6, 5, 3, 4, 2, 1, 0 };
    std::string lower;
    for (auto c : n.substr (0, std::min<size_t> (3, n.size()))) lower += (char) std::tolower ((unsigned char) c);

    for (size_t i = 0; i < std::size (romans); ++i)
        if (lower.compare (0, romans[i].size(), romans[i]) == 0)
        {
            // "iv" must not read as "i" + "v": romans are ordered longest first; reject "vi" matched inside "vii" etc. by order.
            return degrees[i];
        }

    return -1;
}

class ProgressionBrain
{
public:
    ProgressionBrain()
    {
        for (const auto& p : builtInProgressions)
        {
            Progression q;
            q.minor = p.minor != 0;
            q.degrees.assign (p.degrees, p.degrees + p.length);
            q.numerals = p.numerals;
            q.moods = p.moods;
            q.source = "free-midi-chords";
            add (std::move (q));
        }
    }

    void add (Progression p)
    {
        if (p.degrees.size() < 2)
            return;

        auto& t = counts[p.minor ? 1 : 0];
        for (size_t i = 0; i + 1 < p.degrees.size(); ++i)
            t[(size_t) p.degrees[i]][(size_t) p.degrees[i + 1]] += 1.0f;
        t[(size_t) p.degrees.back()][(size_t) p.degrees.front()] += 0.5f; // loops wrap around
        all.push_back (std::move (p));
    }

    size_t size() const noexcept { return all.size(); }
    const std::vector<Progression>& progressions() const noexcept { return all; }

    // P(next | from) for each degree, data + prior, the same chord excluded (a "next" chord is a change).
    std::array<float, 7> nextProbabilities (bool minor, int from) const
    {
        static constexpr float priorMajor[7][7] {
            //  I    ii   iii  IV   V    vi   vii
            { 0.0f, 0.8f, 0.4f, 1.6f, 1.6f, 1.2f, 0.2f }, // I
            { 0.3f, 0.0f, 0.2f, 0.4f, 2.0f, 0.3f, 0.3f }, // ii -> V
            { 0.3f, 0.2f, 0.0f, 0.8f, 0.3f, 1.4f, 0.1f }, // iii -> vi
            { 1.2f, 0.6f, 0.2f, 0.0f, 1.6f, 0.4f, 0.3f }, // IV -> V / I
            { 2.2f, 0.2f, 0.2f, 0.6f, 0.0f, 1.0f, 0.1f }, // V -> I / vi
            { 0.5f, 0.8f, 0.3f, 1.6f, 0.8f, 0.0f, 0.1f }, // vi -> IV / ii
            { 2.0f, 0.1f, 0.4f, 0.1f, 0.3f, 0.3f, 0.0f }, // vii -> I
        };
        static constexpr float priorMinor[7][7] {
            //  i    ii   III  iv   v    VI   VII
            { 0.0f, 0.3f, 0.9f, 1.4f, 0.9f, 1.4f, 1.2f }, // i
            { 0.3f, 0.0f, 0.2f, 0.3f, 1.8f, 0.3f, 0.3f }, // ii° -> v
            { 0.6f, 0.1f, 0.0f, 0.8f, 0.3f, 1.0f, 1.2f }, // III -> VII / VI
            { 1.2f, 0.3f, 0.3f, 0.0f, 1.4f, 0.8f, 0.8f }, // iv -> v / i
            { 2.0f, 0.1f, 0.3f, 0.6f, 0.0f, 0.9f, 0.4f }, // v -> i
            { 0.6f, 0.4f, 1.0f, 1.0f, 0.6f, 0.0f, 1.4f }, // VI -> VII / III
            { 1.6f, 0.1f, 1.2f, 0.4f, 0.4f, 0.6f, 0.0f }, // VII -> i / III
        };
        const auto& prior = minor ? priorMinor : priorMajor;
        const auto& data = counts[minor ? 1 : 0];
        const auto f = std::clamp (from, 0, 6);
        std::array<float, 7> p {};
        float total = 0.0f;

        for (int d = 0; d < 7; ++d)
        {
            p[(size_t) d] = d == f ? 0.0f : data[(size_t) f][(size_t) d] + 2.0f * prior[f][d];
            total += p[(size_t) d];
        }

        for (auto& v : p)
            v = total > 0.0f ? v / total : 1.0f / 6.0f;

        return p;
    }

    // The n most likely next degrees, best first.
    std::vector<std::pair<int, float>> suggest (bool minor, int from, int n = 4) const
    {
        const auto p = nextProbabilities (minor, from);
        std::vector<std::pair<int, float>> out;
        for (int d = 0; d < 7; ++d)
            if (p[(size_t) d] > 0.0f)
                out.push_back ({ d, p[(size_t) d] });
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
        out.resize (std::min<size_t> (out.size(), (size_t) n));
        return out;
    }

    static std::vector<std::string> moods()
    {
        return { "Dark", "Dramatic", "Empowered", "Excited", "Hopeful", "Joyful", "Lonely", "Mysterious", "Nostalgic", "Peaceful",
                 "Playful", "Rebellious", "Relaxed", "Romantic", "Sad", "Spiritual", "Tender", "Triumphant" };
    }

    // A progression of `length` chords (4 or 8). mood "" = smart: a walk on the learned table from the tonic that ends on
    // a chord leading home. With a mood: a real progression tagged with it (then extended or trimmed to the length), or
    // the walk when none matches. useLibraryOnly: only progressions from the user's library (when loaded).
    Progression generate (bool minor, const std::string& mood, int length, uint32_t seed, bool libraryOnly = false) const
    {
        std::mt19937 rng (seed);
        std::vector<const Progression*> pool;

        if (! mood.empty())
            for (const auto& p : all)
                if (p.minor == minor && p.moods.find (mood) != std::string::npos && (! libraryOnly || p.source != "free-midi-chords"))
                    pool.push_back (&p);

        Progression out;
        out.minor = minor;

        if (! pool.empty())
        {
            const auto& pick = *pool[std::uniform_int_distribution<size_t> (0, pool.size() - 1) (rng)];
            out = pick;

            if ((int) out.degrees.size() > length)
                out.degrees.resize ((size_t) length);

            if ((int) out.degrees.size() < length) // 4 -> 8: repeat, then answer the phrase with a learned turnaround
            {
                const auto first = out.degrees;
                while ((int) out.degrees.size() < length)
                    out.degrees.push_back (first[out.degrees.size() % first.size()]);
                out.degrees.back() = cadence (minor, out.degrees[out.degrees.size() - 2], rng);
                out.numerals += " (extended)";
            }

            return out;
        }

        out.degrees.push_back (0);
        while ((int) out.degrees.size() < length)
        {
            const auto last = (int) out.degrees.size() == length - 1;
            out.degrees.push_back (last ? cadence (minor, out.degrees.back(), rng) : sample (minor, out.degrees.back(), rng));
        }

        out.moods = mood.empty() ? "Smart" : mood + " (smart)";
        out.source = "theory brain";
        return out;
    }

    // Roman numerals of the key's own chords (natural minor / major triads).
    static std::string numeral (bool minor, int degree)
    {
        static constexpr const char* maj[] { "I", "ii", "iii", "IV", "V", "vi", "vii°" };
        static constexpr const char* min[] { "i", "ii°", "III", "iv", "v", "VI", "VII" };
        return (minor ? min : maj)[std::clamp (degree, 0, 6)];
    }

private:
    std::array<std::array<std::array<float, 7>, 7>, 2> counts {};
    std::vector<Progression> all;

    int sample (bool minor, int from, std::mt19937& rng) const
    {
        const auto p = nextProbabilities (minor, from);
        std::discrete_distribution<int> d (p.begin(), p.end());
        return d (rng);
    }

    // The last chord before the loop returns to the tonic: the most likely way home that isn't the tonic itself.
    int cadence (bool minor, int before, std::mt19937& rng) const
    {
        std::array<float, 7> w {};
        for (int d = 1; d < 7; ++d)
            w[(size_t) d] = d == before ? 0.0f : nextProbabilities (minor, d)[0] * nextProbabilities (minor, before)[(size_t) d];
        if (std::all_of (w.begin(), w.end(), [] (float v) { return v <= 0.0f; }))
            return 4;
        std::discrete_distribution<int> dist (w.begin(), w.end());
        return dist (rng);
    }
};

// The user's chord library ("MASTER CHORD LIBRARY" index written by chordlib for MP-KEY): id, key ("C major"),
// moods ("Dark,Sad"), numerals ("i VI III VII"), ... Progressions with borrowed chords are skipped.
inline int loadChordLibraryTsv (ProgressionBrain& brain, const std::string& text)
{
    int added = 0;
    size_t pos = 0;

    while (pos < text.size())
    {
        const auto end = std::min (text.find ('\n', pos), text.size());
        const auto line = std::string_view (text).substr (pos, end - pos);
        pos = end + 1;

        if (line.empty() || line[0] == '#')
            continue;

        std::vector<std::string_view> f;
        size_t a = 0;
        while (a <= line.size())
        {
            const auto b = std::min (line.find ('\t', a), line.size());
            f.push_back (line.substr (a, b - a));
            a = b + 1;
        }

        if (f.size() < 4)
            continue;

        Progression p;
        p.minor = f[1].find ("minor") != std::string_view::npos;
        p.moods = std::string (f[2]);
        std::replace (p.moods.begin(), p.moods.end(), ',', ' ');
        p.numerals = std::string (f[3]);
        p.source = "your chord library";
        bool ok = f[1].find ("major") != std::string_view::npos || p.minor;
        size_t s = 0;

        while (ok && s < f[3].size())
        {
            const auto e = std::min (f[3].find (' ', s), f[3].size());
            if (e > s)
            {
                const auto d = degreeOfNumeral (f[3].substr (s, e - s));
                ok = d >= 0;
                p.degrees.push_back (d);
            }
            s = e + 1;
        }

        if (ok && p.degrees.size() >= 2 && p.degrees.size() <= 16)
        {
            brain.add (std::move (p));
            ++added;
        }
    }

    return added;
}

} // namespace neuro
