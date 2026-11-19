#include "SampleLibrary.h"

namespace neuro
{

SampleLibrary::SampleLibrary() : juce::Thread ("Neuro sample library")
{
    loadConfig();
    loadCache();
}

SampleLibrary::~SampleLibrary()
{
    stopThread (4000);
}

juce::File SampleLibrary::folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/NEURO MPC");
}

bool SampleLibrary::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

std::vector<LibrarySection> SampleLibrary::getSections() const
{
    const std::scoped_lock l (lock);
    return sections;
}

int SampleLibrary::size() const
{
    const std::scoped_lock l (lock);
    return (int) entries.size();
}

int SampleLibrary::addSection (const juce::String& name)
{
    int index = -1;
    {
        const std::scoped_lock l (lock);

        for (size_t i = 0; i < sections.size(); ++i)
            if (sections[i].name.equalsIgnoreCase (name))
                index = (int) i;

        if (index < 0)
        {
            sections.push_back ({ name.trim().isEmpty() ? juce::String ("Section") : name.trim(), {} });
            index = (int) sections.size() - 1;
        }
    }

    saveConfig();
    ++version;
    return index;
}

void SampleLibrary::addFolder (int section, const juce::File& dir)
{
    {
        const std::scoped_lock l (lock);

        if (section < 0 || section >= (int) sections.size() || ! dir.isDirectory())
            return;

        sections[(size_t) section].roots.addIfNotAlreadyThere (dir.getFullPathName());
    }

    saveConfig();
    rescan();
}

void SampleLibrary::removeSection (int section)
{
    {
        const std::scoped_lock l (lock);

        if (section < 0 || section >= (int) sections.size())
            return;

        sections.erase (sections.begin() + section);
        std::erase_if (entries, [section] (const auto& e) { return e.section == section; });

        for (auto& e : entries)
            if (e.section > section)
                --e.section;
    }

    saveConfig();
    saveCache();
    ++version;
}

void SampleLibrary::rescan()
{
    stopThread (4000);
    scannedThisRun = true;
    startThread (juce::Thread::Priority::low);
}

void SampleLibrary::rescanIfStale()
{
    if (! scannedThisRun)
        rescan();
}

void SampleLibrary::run()
{
    const auto snapshot = getSections();
    std::vector<LibraryEntry> found;
    scanCount = 0;

    for (size_t s = 0; s < snapshot.size(); ++s)
        for (const auto& rootPath : snapshot[s].roots)
        {
            const juce::File root (rootPath);

            for (const auto& item : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
            {
                if (threadShouldExit())
                    return;

                const auto& f = item.getFile();

                if (! isAudioFile (f))
                    continue;

                LibraryEntry e;
                e.path = f.getFullPathName();
                e.name = f.getFileNameWithoutExtension();
                const auto rel = f.getParentDirectory().getRelativePathFrom (root);
                std::vector<std::string> folders;

                if (rel != ".")
                    for (const auto& part : juce::StringArray::fromTokens (rel, "/", ""))
                        folders.push_back (part.toStdString());

                e.pack = folders.empty() ? root.getFileName() : juce::String (folders.front());
                e.info = classifySample (e.name.toStdString(), folders);
                e.searchText = f.getRelativePathFrom (root).toLowerCase();
                e.section = (int) s;
                found.push_back (std::move (e));
                ++scanCount;
            }
        }

    std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });

    {
        const std::scoped_lock l (lock);

        if (sections.size() != snapshot.size()) // sections changed while scanning: scan again
            return;

        entries = std::move (found);
    }

    saveCache();
    ++version;
}

std::vector<LibraryEntry> SampleLibrary::query (int section, int type, const juce::String& text, const juce::String& pack, size_t limit) const
{
    const auto words = juce::StringArray::fromTokens (text.toLowerCase(), " ", "\"");
    std::vector<LibraryEntry> out;
    const std::scoped_lock l (lock);

    for (const auto& e : entries)
    {
        if (e.section != section)
            continue;

        if (type == libraryOneShots ? isLoopType (e.info.type)
            : type == libraryLoops  ? ! isLoopType (e.info.type)
            : type >= 0 && (int) e.info.type != type)
            continue;

        if (pack.isNotEmpty() && e.pack != pack)
            continue;

        bool all = true;

        for (const auto& w : words)
            if (w.isNotEmpty() && ! e.searchText.contains (w))
            {
                all = false;
                break;
            }

        if (! all)
            continue;

        out.push_back (e);

        if (out.size() >= limit)
            break;
    }

    return out;
}

juce::StringArray SampleLibrary::getPacks (int section) const
{
    juce::StringArray packs;
    const std::scoped_lock l (lock);

    for (const auto& e : entries)
        if (e.section == section)
            packs.addIfNotAlreadyThere (e.pack);

    packs.sortNatural();
    return packs;
}

std::optional<LibraryEntry> SampleLibrary::pickRandom (int section, SampleType type, const juce::String& pack, std::mt19937& rng) const
{
    const std::scoped_lock l (lock);
    std::vector<const LibraryEntry*> any, inPack;

    for (const auto& e : entries)
        if (e.section == section && e.info.type == type)
        {
            any.push_back (&e);

            if (pack.isNotEmpty() && e.pack == pack)
                inPack.push_back (&e);
        }

    const auto& from = inPack.empty() ? any : inPack;

    if (from.empty())
        return std::nullopt;

    return *from[std::uniform_int_distribution<size_t> (0, from.size() - 1) (rng)];
}

void SampleLibrary::saveConfig() const
{
    juce::Array<juce::var> list;
    {
        const std::scoped_lock l (lock);

        for (const auto& s : sections)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("name", s.name);
            juce::Array<juce::var> roots;
            for (const auto& r : s.roots) roots.add (r);
            obj->setProperty ("folders", roots);
            list.add (juce::var (obj));
        }
    }

    folder().createDirectory();
    folder().getChildFile ("Library.json").replaceWithText (juce::JSON::toString (juce::var (list)));
}

void SampleLibrary::loadConfig()
{
    const auto parsed = juce::JSON::parse (folder().getChildFile ("Library.json"));

    if (auto* list = parsed.getArray())
        for (const auto& item : *list)
        {
            LibrarySection s;
            s.name = item.getProperty ("name", "Section").toString();

            if (auto* roots = item.getProperty ("folders", {}).getArray())
                for (const auto& r : *roots)
                    s.roots.add (r.toString());

            sections.push_back (std::move (s));
        }

    if (sections.empty())
        sections.push_back ({ "Drums", {} });
}

// One line per entry: section, type, bpm, key, minor, pack, path (tab-separated; root-relative search text is rebuilt).
void SampleLibrary::saveCache() const
{
    juce::MemoryOutputStream out;
    {
        const std::scoped_lock l (lock);

        for (const auto& e : entries)
            out << e.section << '\t' << (int) e.info.type << '\t' << e.info.bpm << '\t' << e.info.keyPc << '\t' << (e.info.minor ? 1 : 0)
                << '\t' << e.pack << '\t' << e.path << '\t' << e.searchText << '\n';
    }

    folder().createDirectory();
    folder().getChildFile ("Library index.tsv").replaceWithData (out.getData(), out.getDataSize());
}

void SampleLibrary::loadCache()
{
    juce::StringArray lines;
    lines.addLines (folder().getChildFile ("Library index.tsv").loadFileAsString());

    for (const auto& line : lines)
    {
        const auto f = juce::StringArray::fromTokens (line, "\t", "");

        if (f.size() < 8 || f[0].getIntValue() >= (int) sections.size())
            continue;

        LibraryEntry e;
        e.section = f[0].getIntValue();
        e.info.type = (SampleType) juce::jlimit (0, numSampleTypes - 1, f[1].getIntValue());
        e.info.bpm = f[2].getIntValue();
        e.info.keyPc = f[3].getIntValue();
        e.info.minor = f[4].getIntValue() != 0;
        e.pack = f[5];
        e.path = f[6];
        e.name = juce::File (e.path).getFileNameWithoutExtension();
        e.searchText = f[7];
        entries.push_back (std::move (e));
    }
}

} // namespace neuro
