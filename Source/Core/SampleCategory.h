#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

// Sample library: what a sample is, from its file name and folders (no audio needed, so 50k files index in seconds).
// Producer packs name things consistently enough: "Kicks/KWKIK066.wav", "808s/808 (Zay).wav", "Hihat Loops/... 96 BPM.wav".
namespace neuro
{

enum class SampleType
{
    kick, snare, clap, rim, closedHat, openHat, cymbal, bass808, tom, perc, fx, vox, instrument,
    drumLoop, breakLoop, hatLoop, percLoop, melodicLoop, other
};

inline constexpr int numSampleTypes = (int) SampleType::other + 1;
inline constexpr const char* sampleTypeNames[] { "Kick", "Snare", "Clap", "Rim", "Closed Hat", "Open Hat", "Cymbal", "808", "Tom",
                                                 "Perc", "FX", "Vox", "Instrument", "Drum Loop", "Break", "Hat Loop", "Perc Loop", "Melodic Loop",
                                                 "Other" };

inline bool isLoopType (SampleType t) noexcept { return t >= SampleType::drumLoop && t <= SampleType::melodicLoop; }

struct SampleInfo
{
    SampleType type = SampleType::other;
    int bpm = 0;    // 0 = unknown
    int keyPc = -1; // pitch class of the key / note named in the file, or -1
    bool keyNamed = false; // keyPc is a key with its mode ("Bmin", "F# Maj", "G Minor"), not just a note in brackets
    bool minor = false;
};

namespace samplecat
{
    inline std::vector<std::string> tokens (std::string_view s)
    {
        std::vector<std::string> out;
        std::string cur;

        auto flush = [&] { if (! cur.empty()) out.push_back (cur); cur.clear(); };

        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto c = (unsigned char) s[i];

            if (std::isalnum (c) || c == '#')
            {
                // Split letters from digits ("bpm095" -> bpm 095, "Snare02" -> snare 02), but keep "808" whole.
                if (! cur.empty() && (bool) std::isdigit ((unsigned char) cur.back()) != (bool) std::isdigit (c) && c != '#')
                    flush();

                cur += (char) std::tolower (c);
            }
            else
            {
                flush();
            }
        }

        flush();
        return out;
    }

    inline bool hasPrefix (const std::string& t, std::string_view p) { return t.size() >= p.size() && t.compare (0, p.size(), p) == 0; }
    inline bool contains (const std::string& t, std::string_view p) { return t.find (p) != std::string::npos; }

    // The drum named by one token, or other.
    inline SampleType typeOfToken (const std::string& t)
    {
        if (t == "808" || t == "808s" || hasPrefix (t, "sub808") || t == "subbass") return SampleType::bass808;
        if (hasPrefix (t, "kick") || t == "kik" || t == "kck" || t == "bd" || t == "kd" || t == "kyx" || t == "kix" || hasPrefix (t, "kickz") || t == "bassdrum" || hasPrefix (t, "kwkik")) return SampleType::kick;
        if (hasPrefix (t, "snare") || t == "snr" || t == "sd" || t == "snarez" || t == "snares" || t == "snrs") return SampleType::snare;
        if (hasPrefix (t, "clap") || t == "clp" || t == "claps") return SampleType::clap;
        if (hasPrefix (t, "rim") || t == "rimshot") return SampleType::rim;
        if (hasPrefix (t, "openhat") || hasPrefix (t, "openhh") || t == "oh" || t == "ohh" || t == "ohat" || t == "opens") return SampleType::openHat;
        if (contains (t, "hihat") || contains (t, "hyhat") || contains (t, "hihh") || hasPrefix (t, "hat") || t == "hh" || t == "chh" || t == "hats" || t == "closedhat")
            return SampleType::closedHat;
        if (hasPrefix (t, "crash") || hasPrefix (t, "ride") || hasPrefix (t, "cymbal") || t == "cym" || t == "cyms" || hasPrefix (t, "splash")
            || hasPrefix (t, "china"))
            return SampleType::cymbal;
        if (hasPrefix (t, "tom")) return SampleType::tom;
        if (hasPrefix (t, "perc") || hasPrefix (t, "conga") || hasPrefix (t, "bongo") || hasPrefix (t, "shaker") || hasPrefix (t, "tamb")
            || hasPrefix (t, "cowbell") || t == "clave" || t == "claves" || hasPrefix (t, "block") || hasPrefix (t, "timbal")
            || hasPrefix (t, "triangle") || t == "snap" || t == "snaps" || hasPrefix (t, "shkr") || hasPrefix (t, "guiro"))
            return SampleType::perc;
        if (t == "fx" || t == "sfx" || hasPrefix (t, "riser") || hasPrefix (t, "impact") || hasPrefix (t, "sweep") || hasPrefix (t, "uplifter")
            || hasPrefix (t, "downlifter") || t == "noise" || hasPrefix (t, "reverse") || t == "rev" || hasPrefix (t, "transition"))
            return SampleType::fx;
        if (t == "vox" || hasPrefix (t, "vocal") || hasPrefix (t, "chant") || t == "adlib" || t == "adlibs" || hasPrefix (t, "voice"))
            return SampleType::vox;
        return SampleType::other;
    }

    // The first drum named in a list of tokens, with two-token phrases ("open hat", "hi hat", "closed hat").
    inline SampleType firstType (const std::vector<std::string>& t)
    {
        for (size_t i = 0; i < t.size(); ++i)
        {
            const auto next = i + 1 < t.size() ? t[i + 1] : std::string();

            const auto hatNext = contains (next, "hat") || next == "hh" || next == "hi";
            if (t[i] == "open" && hatNext) return SampleType::openHat;
            if (t[i] == "closed" && hatNext) return SampleType::closedHat;
            if (t[i] == "hi" && hasPrefix (next, "hat"))
                return i + 2 < t.size() && hasPrefix (t[i + 2], "open") ? SampleType::openHat : SampleType::closedHat;
            if (t[i] == "bass" && next == "drum") return SampleType::kick;
            if (t[i] == "sub" && next == "bass") return SampleType::bass808;

            if (const auto ty = typeOfToken (t[i]); ty != SampleType::other)
                return ty;
        }

        return SampleType::other;
    }

    inline bool anyToken (const std::vector<std::string>& t, std::initializer_list<std::string_view> words)
    {
        for (const auto& x : t)
            for (auto w : words)
                if (x == w)
                    return true;
        return false;
    }

    // "96 BPM", "118_BPM", "bpm095", "DrumLoop_KCUF_108BPM".
    inline int bpmOf (const std::vector<std::string>& t)
    {
        for (size_t i = 0; i < t.size(); ++i)
        {
            if (t[i] != "bpm")
                continue;

            for (const auto j : { i + 1, i - 1 }) // after "bpm" first ("bpm095"), then before ("96 bpm")
                if (j < t.size() && ! t[j].empty() && std::isdigit ((unsigned char) t[j][0]) && t[j].size() <= 3)
                    if (const auto v = std::stoi (t[j]); v >= 50 && v <= 220)
                        return v;
        }

        return 0;
    }

    inline int pitchClassOfName (std::string_view n)
    {
        static constexpr int natural[] { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        if (n.empty() || n[0] < 'a' || n[0] > 'g') return -1;
        auto pc = natural[n[0] - 'a'];
        if (n.size() > 1 && n[1] == '#') pc += 1;
        if (n.size() > 1 && n[1] == 'b') pc += 11;
        return pc % 12;
    }

    // "Cmin", "F#m", "Amaj", "C#minor"; a bare note only inside brackets, e.g. "808 (C)" (raw name needed for that).
    inline void keyOf (const std::vector<std::string>& t, std::string_view raw, SampleInfo& info)
    {
        for (size_t i = 0; i < t.size(); ++i)
        {
            const auto& x = t[i];
            if (x.empty() || x[0] < 'a' || x[0] > 'g')
                continue;

            // "f sharp major", "e flat minor"
            if (x.size() == 1 && i + 2 < t.size() && (t[i + 1] == "sharp" || t[i + 1] == "flat")
                && (t[i + 2] == "major" || t[i + 2] == "minor" || t[i + 2] == "maj" || t[i + 2] == "min"))
            {
                info.keyPc = (pitchClassOfName (x) + (t[i + 1] == "sharp" ? 1 : 11)) % 12;
                info.keyNamed = true;
                info.minor = t[i + 2] == "minor" || t[i + 2] == "min";
                return;
            }

            const auto acc = x.size() > 1 && (x[1] == '#' || (x[1] == 'b' && x.size() > 2)) ? 2u : 1u;
            const auto rest = std::string_view (x).substr (acc);
            const auto nextMode = i + 1 < t.size() ? t[i + 1] : std::string();

            if (rest == "min" || rest == "minor" || rest == "m" || rest == "maj" || rest == "major"
                || (rest.empty() && (nextMode == "min" || nextMode == "minor" || nextMode == "maj" || nextMode == "major")))
            {
                info.keyPc = pitchClassOfName (std::string_view (x).substr (0, acc));
                info.keyNamed = true;
                info.minor = rest == "min" || rest == "minor" || rest == "m" || nextMode == "min" || nextMode == "minor";
                return;
            }
        }

        // "(C)", "(F#)", "[Eb]"
        for (size_t i = 0; i + 2 < raw.size(); ++i)
            if (raw[i] == '(' || raw[i] == '[')
            {
                size_t j = i + 1, n = 0;
                std::string note;
                while (j < raw.size() && raw[j] != ')' && raw[j] != ']' && n < 4) { note += (char) std::tolower ((unsigned char) raw[j]); ++j; ++n; }
                if (j < raw.size() && (note.size() == 1 || (note.size() == 2 && (note[1] == '#' || note[1] == 'b'))))
                    if (const auto pc = pitchClassOfName (note); pc >= 0) { info.keyPc = pc; return; }
            }
    }
}

// fileName: the file's name without extension; folders: the folders from the library root down (innermost last).
inline SampleInfo classifySample (std::string_view fileName, const std::vector<std::string>& folders)
{
    using namespace samplecat;
    SampleInfo info;
    const auto nameTokens = tokens (fileName);
    std::vector<std::string> folderTokens; // innermost folder first: it's the most specific
    for (auto it = folders.rbegin(); it != folders.rend(); ++it)
        for (auto& t : tokens (*it))
            folderTokens.push_back (std::move (t));

    info.bpm = bpmOf (nameTokens);
    keyOf (nameTokens, fileName, info);

    const auto loopWords = { std::string_view ("loop"), std::string_view ("loops"), std::string_view ("groove"), std::string_view ("grooves"),
                             std::string_view ("bpm"), std::string_view ("drumloop"), std::string_view ("drumloops"), std::string_view ("drlp") };
    const auto breakWords = { std::string_view ("break"), std::string_view ("breaks"), std::string_view ("breakbeat"), std::string_view ("breakbeats") };
    const auto nameDrum = firstType (nameTokens);
    const auto nameBreak = anyToken (nameTokens, breakWords);
    const auto nameLoop = nameBreak || info.bpm > 0 || anyToken (nameTokens, loopWords)
                          || std::any_of (nameTokens.begin(), nameTokens.end(), [] (const auto& t) { return contains (t, "loop"); }); // "Percloop"
    const auto folderBreak = anyToken (folderTokens, breakWords);
    const auto folderLoop = folderBreak || anyToken (folderTokens, loopWords) || anyToken (folderTokens, { "compositions", "stems" });
    // A one-shot named as such ("ARAAB_KICK_N2K31" in "KICKS & BREAKS") stays a one-shot; a loop folder makes the rest loops.
    const auto isLoop = nameLoop || (folderLoop && nameDrum == SampleType::other);
    const auto isBreak = nameBreak || (folderBreak && ! nameLoop) || (folderBreak && info.bpm > 0);
    auto drum = nameDrum;

    if (drum == SampleType::other)
        drum = firstType (folderTokens);
    else if (drum == SampleType::closedHat && ! anyToken (nameTokens, { "closed", "chh", "closedhat" }) && firstType (folderTokens) == SampleType::openHat)
        drum = SampleType::openHat; // "Open Hihat/VDUB2 Open Hihat 021": the folder knows better than a bare "hihat"

    const auto melodic = anyToken (nameTokens, { "melody", "melodic", "keys", "piano", "guitar", "synth", "chords", "pad", "bass", "lead", "strings", "bells", "pluck" })
                         || anyToken (folderTokens, { "melody", "melodies", "melodic", "keys", "synth", "guitars", "guitar", "bass", "piano" });

    if (isLoop && ! (nameDrum == SampleType::fx || nameDrum == SampleType::vox))
    {
        if (isBreak && drum != SampleType::closedHat && drum != SampleType::openHat && drum != SampleType::perc)
            info.type = SampleType::breakLoop;
        else if (drum == SampleType::closedHat || drum == SampleType::openHat)
            info.type = SampleType::hatLoop;
        else if (drum == SampleType::perc || drum == SampleType::tom)
            info.type = SampleType::percLoop;
        else if (melodic && nameDrum == SampleType::other)
            info.type = SampleType::melodicLoop;
        else
            info.type = SampleType::drumLoop;
    }
    else
    {
        info.type = drum;

        // Melodic one-shots (keys, synth, bass, guitar hits) are worth finding too.
        if (drum == SampleType::other
            && (melodic || anyToken (nameTokens, { "wurli", "rhodes", "organ", "horn", "brass", "flute", "stab", "chord", "oneshot" })
                || anyToken (folderTokens, { "oneshots", "keyboards", "instruments", "musical" })))
            info.type = SampleType::instrument;
    }

    if (info.bpm == 0 && isLoop) // "bpm095" style folders
        info.bpm = bpmOf (folderTokens);

    return info;
}

} // namespace neuro
