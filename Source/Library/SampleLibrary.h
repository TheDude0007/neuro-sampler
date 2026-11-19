#pragma once

#include "../Core/SampleCategory.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <random>

// The sample library: sections ("Drums", ...) of the user's own folders, indexed in the background by type, BPM and key
// from file and folder names. Nothing is copied: entries are paths into the user's folders (commercial packs stay local).
// One shared instance per process (juce::SharedResourcePointer), used by every editor.
namespace neuro
{

struct LibraryEntry
{
    juce::String path, name, pack, searchText; // searchText: lower-case relative path, for word search
    SampleInfo info;
    int section = 0;
};

struct LibrarySection
{
    juce::String name;
    juce::StringArray roots;
};

// Type filter values beyond the SampleType range.
inline constexpr int libraryAllTypes = -1, libraryOneShots = -2, libraryLoops = -3;

class SampleLibrary : private juce::Thread
{
public:
    SampleLibrary();
    ~SampleLibrary() override;

    // ~/Library/Application Support/NEURO MPC/Library.json (sections) and Library index.tsv (the cached index).
    static juce::File folder();

    std::vector<LibrarySection> getSections() const;
    int addSection (const juce::String& name);              // returns its index (an existing section of that name is reused)
    void addFolder (int section, const juce::File& folder); // and rescans
    void removeSection (int section);

    void rescan();                                          // background; the old index stays usable meanwhile
    void rescanIfStale();                                   // once per process
    bool isScanning() const noexcept { return isThreadRunning(); }
    int getScanCount() const noexcept { return scanCount.load(); }
    int getVersion() const noexcept { return version.load(); } // changes whenever the index or sections change
    int size() const;

    // Matches in name order. type: a SampleType, or libraryAllTypes / libraryOneShots / libraryLoops. Every word of text
    // must appear in the path. pack: "" = any.
    std::vector<LibraryEntry> query (int section, int type, const juce::String& text, const juce::String& pack, size_t limit = 1000000) const;
    juce::StringArray getPacks (int section) const;
    // A random entry of that type (from `pack` when it has one), or nullopt.
    std::optional<LibraryEntry> pickRandom (int section, SampleType type, const juce::String& pack, std::mt19937& rng) const;

    static bool isAudioFile (const juce::File&);

private:
    void run() override;
    void saveConfig() const;
    void loadConfig();
    void saveCache() const;
    void loadCache();

    mutable std::mutex lock;
    std::vector<LibrarySection> sections;
    std::vector<LibraryEntry> entries;
    std::atomic<int> scanCount { 0 }, version { 0 };
    bool scannedThisRun = false;
};

} // namespace neuro
