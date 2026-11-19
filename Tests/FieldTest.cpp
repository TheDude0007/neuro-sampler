// Field test (not part of ctest): real drum loops, KIT tab snapshot, and the installed VST3/AU hosted through JUCE.
// Usage: neuro_field_test <outputDir> <loop.caf/wav>...
#include "Core/TransientDetector.h"
#include "PluginEditor.h"
#include "Core/KeyFeatures.h"
#include "Core/KeyDetector.h"
#include <thread>
#include <mutex>
#include <CoreFoundation/CoreFoundation.h>

namespace
{
    // Hands NEURO's own state to a hosted instance the way a DAW does: wrapped in the host format's state container.
    bool loadNeuroState (juce::AudioPluginInstance& instance, const juce::MemoryBlock& ours, const juce::String& formatName)
    {
        juce::MemoryBlock hostState;
        instance.getStateInformation (hostState);

        if (formatName == "VST3")
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
            if (xml == nullptr || ! xml->hasTagName ("VST3PluginState")) return false;
            if (auto* component = xml->getChildByName ("IComponent")) xml->removeChildElement (component, true);
            xml->createNewChildElement ("IComponent")->addTextElement (ours.toBase64Encoding());
            juce::MemoryBlock wrapped;
            juce::AudioProcessor::copyXmlToBinary (*xml, wrapped);
            instance.setStateInformation (wrapped.getData(), (int) wrapped.getSize());
            return true;
        }

        // AudioUnit: the ClassInfo property list, with NEURO's bytes under JUCE's "jucePluginState" key.
        auto data = CFDataCreate (nullptr, (const UInt8*) hostState.getData(), (CFIndex) hostState.getSize());
        auto plist = CFPropertyListCreateWithData (nullptr, data, kCFPropertyListMutableContainersAndLeaves, nullptr, nullptr);
        CFRelease (data);
        if (plist == nullptr || CFGetTypeID (plist) != CFDictionaryGetTypeID()) return false;
        auto ourData = CFDataCreate (nullptr, (const UInt8*) ours.getData(), (CFIndex) ours.getSize());
        CFDictionarySetValue ((CFMutableDictionaryRef) plist, CFSTR ("jucePluginState"), ourData);
        CFRelease (ourData);
        auto out = CFPropertyListCreateData (nullptr, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
        CFRelease (plist);
        if (out == nullptr) return false;
        instance.setStateInformation (CFDataGetBytePtr (out), (int) CFDataGetLength (out));
        CFRelease (out);
        return true;
    }

    // Fraction of a class's hits on given 16th positions within the bar (16 steps).
    float fractionOn (const neuro::SeqLane& lane, std::initializer_list<int> positions)
    {
        int on = 0, hit = 0;
        for (int i = 0; i < lane.length; ++i)
            if (lane.steps[(size_t) i].on)
            {
                ++on;
                for (int p : positions) hit += (i % 16) == p ? 1 : 0;
            }
        return on > 0 ? (float) hit / (float) on : -1.0f;
    }

    void realLoops (const juce::StringArray& files)
    {
        std::printf ("\n== Real drum loops (Apple Logic Drummer recordings) ==\n");
        for (const auto& path : files)
        {
            NeuroMPCProcessor p;
            juce::String error;
            const juce::File f (path);
            std::unique_ptr<juce::AudioFormatReader> reader (p.formats.createReaderFor (f));
            double bpm = 120.0;
            if (reader != nullptr)
                for (auto key : reader->metadataValues.getAllKeys())
                    if (key.containsIgnoreCase ("tempo") || key.containsIgnoreCase ("bpm"))
                        bpm = juce::jmax (40.0, reader->metadataValues[key].getDoubleValue());
            if (! p.loadSample (f, error)) { std::printf ("%s: %s\n", f.getFileName().toRawUTF8(), error.toRawUTF8()); continue; }

            const auto seconds = (double) p.getSample()->length() / p.getSample()->sampleRate;
            for (int pass = 0; pass < 2; ++pass) // generic, then taught (examples a user would pick in bar 1)
            {
                if (pass == 1)
                {
                    const auto spb = p.getSample()->sampleRate * 60.0 / (bpm != 120.0 ? bpm : 32.0 * 60.0 / seconds);
                    p.loadSample (f, error);
                    p.teachDrumRange (neuro::nmf::kick, 0, (int64_t) (0.1 * p.getSample()->sampleRate));
                    p.teachDrumRange (neuro::nmf::snare, (int64_t) spb, (int64_t) (spb + 0.1 * p.getSample()->sampleRate));
                    p.teachDrumRange (neuro::nmf::cymbals, (int64_t) (spb / 2), (int64_t) (spb / 2 + 0.1 * p.getSample()->sampleRate));
                }
                const auto r = p.separateDrums (0); // NMF separation + transcription, auto tempo
                std::printf ("  tempo: estimated %.1f BPM, 8-bar ground truth %.1f BPM%s\n", r.bpm, 32.0 * 60.0 / seconds,
                             bpm != 120.0 ? (" (file tag " + juce::String (bpm) + ")").toRawUTF8() : "");
                const auto& pat = p.getSeqPattern (0);
                std::printf ("\n[Separate drums%s] %s  (%.2f s, %.0f BPM from file metadata%s) -> %d hits, %d steps\n", pass == 1 ? ", taught" : "", f.getFileNameWithoutExtension().toRawUTF8(),
                             seconds, bpm, bpm == 120.0 ? "?" : "", r.hits, r.steps);
                for (int c = 0; c < neuro::numDrumClasses; ++c)
                {
                    const auto& lane = pat.lanes[(size_t) c];
                    if (r.perClass[(size_t) c] == 0) continue;
                    juce::String grid;
                    for (int i = 0; i < juce::jmin (32, lane.length); ++i) grid << (lane.steps[(size_t) i].on ? (i % 4 == 0 ? "X" : "x") : (i % 4 == 0 ? "|" : "."));
                    std::printf ("  %-10s %3d  %s\n", neuro::drumClassName ((neuro::DrumClass) c), r.perClass[(size_t) c], grid.toRawUTF8());
                }
                const auto kickOnDownbeats = fractionOn (pat.lanes[(size_t) neuro::DrumClass::kick], { 0, 8 });
                const auto snareOnBackbeat = fractionOn (pat.lanes[(size_t) neuro::DrumClass::snare], { 4, 12 });
                std::printf ("  groove check: kicks on beats 1/3: %s   snares on beats 2/4: %s\n",
                             kickOnDownbeats < 0 ? "n/a" : juce::String (juce::roundToInt (kickOnDownbeats * 100)).toRawUTF8(),
                             snareOnBackbeat < 0 ? "n/a" : juce::String (juce::roundToInt (snareOnBackbeat * 100)).toRawUTF8());
            }
        }
    }

    void kitSnapshot (const juce::File& outDir, const juce::String& loop)
    {
        NeuroMPCProcessor p;
        juce::String error;
        p.loadSample (juce::File (loop), error);
        p.deconstructLoop (0, 120.0);
        p.waitForBackgroundWork (30000);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<NeuroMPCEditor*> (ed.get());
        for (int tab : { 1, 4, 6, 7 })
        {
            editor->showTab (tab);
            editor->setSize (1140, 840);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = outDir.getChildFile (tab == 6 ? "kit_tab.png" : tab == 7 ? "chop_tab.png" : tab == 1 ? "pad_tab.png" : "seq_tab.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
            std::printf ("snapshot: %s\n", file.getFullPathName().toRawUTF8());
        }
    }

    // CHORD tab snapshot (v1.9): a sample in chord-pads mode.
    void chordSnapshot (const juce::File& outDir, const juce::String& wav)
    {
        NeuroMPCProcessor p;
        juce::String error;
        p.prepareToPlay (48000.0, 512);
        p.loadSample (juce::File (wav), error);
        p.waitForBackgroundWork (30000);
        auto* chords = p.apvts.getParameter (ParamIDs::chords);
        chords->setValueNotifyingHost (chords->convertTo0to1 (2.0f));
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 41, 1.0f), 0); // pad A6: vi
        p.processBlock (buf, midi);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<NeuroMPCEditor*> (ed.get());
        editor->setSize (1140, 840);
        editor->showTab (8);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile ("chord_tab.png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("snapshot: %s\n", file.getFullPathName().toRawUTF8());
    }

    // Key features dump (for training/evaluating key profiles): reads each file of a list, writes
    // path, want, minor, oldTonic, oldMinor, tuning, hpcp[12], bass[12], oldChroma[12]. Parallel.
    void keyFeatures (const juce::File& listFile, const juce::File& outFile)
    {
        juce::StringArray lines;
        lines.addLines (listFile.loadFileAsString());
        std::vector<juce::String> results ((size_t) lines.size());
        std::atomic<int> next { 0 };
        auto work = [&]
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            for (int i; (i = next++) < lines.size();)
            {
                const auto f = juce::StringArray::fromTokens (lines[i], "\t", "");
                if (f.size() < 3) continue;
                std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (f[0])));
                if (r == nullptr) continue;
                const auto len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 90.0));
                juce::AudioBuffer<float> b ((int) juce::jmin (2u, r->numChannels), len);
                r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
                const auto old = neuro::detectKey (b.getArrayOfReadPointers(), b.getNumChannels(), len, r->sampleRate);
                const auto kf = neuro::computeKeyFeatures (b.getArrayOfReadPointers(), b.getNumChannels(), len, r->sampleRate);
                juce::String line;
                line << f[0] << "\t" << f[1] << "\t" << f[2] << "\t" << old.candidates[0].tonic << "\t" << (old.candidates[0].minor ? 1 : 0)
                     << "\t" << kf.tuningCents;
                for (auto v : kf.hpcp) line << "\t" << v;
                for (auto v : kf.bass) line << "\t" << v;
                for (auto v : old.chroma) line << "\t" << v;
                for (auto v : kf.hpcpStart) line << "\t" << v;
                for (auto v : kf.bassStart) line << "\t" << v;
                results[(size_t) i] = line;
            }
        };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < juce::jmax (2u, std::thread::hardware_concurrency()); ++t) pool.emplace_back (work);
        for (auto& t : pool) t.join();
        juce::String all;
        for (auto& l : results) if (l.isNotEmpty()) all << l << "\n";
        outFile.replaceWithText (all);
        std::printf ("wrote %s\n", outFile.getFullPathName().toRawUTF8());
    }

    // Key detection benchmark: list file lines "path<TAB>expectedPc<TAB>minor" (expectedPc -1 = unknown, just report).
    // Uses the plugin's own path: load -> analysis -> getBestKeyEstimate (uncertain allowed, as the song key uses).
    void keyBenchmark (const juce::File& listFile)
    {
        juce::StringArray lines;
        lines.addLines (listFile.loadFileAsString());
        int songOk = 0;
        int labelled = 0, exact = 0, relative = 0, fifth = 0, parallel = 0, none = 0;
        static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        auto keyName = [] (int pc, bool minor) { return juce::String (names[(pc % 12 + 12) % 12]) + (minor ? "m" : ""); };

        for (const auto& line : lines)
        {
            const auto f = juce::StringArray::fromTokens (line, "\t", "");
            if (f.size() < 3) continue;
            NeuroMPCProcessor p;
            p.prepareToPlay (48000.0, 512);
            juce::String error;
            if (! p.loadSample (juce::File (f[0]), error)) { std::printf ("SKIP %s\n", error.toRawUTF8()); continue; }
            p.waitForBackgroundWork (60000);
            p.processBackgroundResults(); // sets the song key, as on a user load
            const auto songRoot = (int) p.apvts.getRawParameterValue (ParamIDs::scaleRoot)->load();
            const auto songMinor = (int) p.apvts.getRawParameterValue (ParamIDs::scale)->load() == neuro::scaleNaturalMinor;
            const auto k = p.getBestKeyEstimate (nullptr, true);
            const auto want = f[1].getIntValue();
            const auto wantMinor = f[2].getIntValue() != 0;
            juce::String got = "none";
            juce::String verdict;

            if (k)
            {
                const auto& c = k->candidates[0];
                got = keyName (c.tonic, c.minor) + (k->status == neuro::KeyEstimate::Status::key ? "" : "?") + " (2nd " + keyName (k->candidates[1].tonic, k->candidates[1].minor) + ")";

                if (want >= 0)
                {
                    ++labelled;
                    // The relative key shares the notes: Am = C. Compare as the major tonic.
                    const auto majGot = c.minor ? (c.tonic + 3) % 12 : c.tonic, majWant = wantMinor ? (want + 3) % 12 : want;
                    if (c.tonic == want && c.minor == wantMinor) { ++exact; verdict = "OK"; }
                    else if (majGot == majWant) { ++relative; verdict = "relative"; }
                    else if ((majGot - majWant + 12) % 12 == 7 || (majWant - majGot + 12) % 12 == 7) { ++fifth; verdict = "fifth"; }
                    else if (c.tonic == want) { ++parallel; verdict = "parallel"; }
                    else verdict = "WRONG";
                }
            }
            else if (want >= 0)
            {
                ++labelled; ++none; verdict = "none";
            }

            if (want >= 0) songOk += (songRoot == want && songMinor == wantMinor) ? 1 : 0;
            std::printf ("%-9s want %-4s song key %-4s detected %-22s %s\n", verdict.toRawUTF8(), want >= 0 ? keyName (want, wantMinor).toRawUTF8() : "?",
                         keyName (songRoot, songMinor).toRawUTF8(),
                         got.toRawUTF8(), juce::File (f[0]).getFileName().substring (0, 60).toRawUTF8());
        }

        if (labelled > 0)
            std::printf ("\nsong key set on load: %d / %d right\n", songOk, labelled);
        if (labelled > 0)
            std::printf ("labelled %d: exact %d (%.0f%%), +relative %d (%.0f%% same notes), fifth %d, parallel %d, none %d\n", labelled, exact,
                         100.0 * exact / labelled, relative, 100.0 * (exact + relative) / labelled, fifth, parallel, none);
    }

    // Library browser snapshot (v1.10): indexes the configured sections, then shows the panel.
    void librarySnapshot (const juce::File& outDir)
    {
        NeuroMPCProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<NeuroMPCEditor*> (ed.get());
        editor->setSize (1140, 840);
        juce::SharedResourcePointer<neuro::SampleLibrary> library;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        editor->showLibrary (true);                      // starts the index
        while (library->isScanning()) juce::Thread::sleep (50);
        std::printf ("indexed %d samples in %.1f s\n", library->size(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        editor->showLibrary (false);
        editor->showLibrary (true);                      // shows the fresh index
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile ("library.png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("snapshot: %s\n", file.getFullPathName().toRawUTF8());
    }

    // Loads the installed plugin binary through the real VST3 / AU hosting APIs and checks the per-class outputs.
    void hostedOutputs (const juce::String& formatName, const juce::String& wav)
    {
        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* format : formats.getFormats())
        {
            if (format->getName() != formatName) continue;
            const auto location = formatName == "VST3" ? juce::File ("~/Library/Audio/Plug-Ins/VST3/Neuro Sampler Suite.vst3").getFullPathName()
                                                       : juce::String ("AudioUnit:Synths/aumu,Nm26,Nrmp");
            format->findAllTypesForFile (found, location);
        }
        std::printf ("\n== Hosted %s: %d plugin(s) found ==\n", formatName.toRawUTF8(), found.size());
        if (found.isEmpty()) return;

        juce::String error;
        auto instance = formats.createPluginInstance (*found[0], 48000.0, 512, error);
        if (instance == nullptr) { std::printf ("  load failed: %s\n", error.toRawUTF8()); return; }
        std::printf ("  loaded '%s', %d output buses:", instance->getName().toRawUTF8(), instance->getBusCount (false));
        for (int b = 0; b < instance->getBusCount (false); ++b) std::printf (" %s", instance->getBus (false, b)->getName().toRawUTF8());
        auto layout = instance->getBusesLayout();
        for (auto& bus : layout.outputBuses) bus = juce::AudioChannelSet::stereo();
        const auto enabled = instance->setBusesLayout (layout);
        std::printf ("\n  enable all 12 stereo outputs: %s (%d output channels)\n", enabled ? "OK" : "REFUSED", instance->getTotalNumOutputChannels());

        // State: a 4-slice kit (kick, snare, closed hat, kick) with explicit class overrides, pads following slices.
        auto xml = juce::parseXML ("<NEURO_MPC version=\"2\"><SAMPLE path=\"" + wav + "\"/><SLICES>"
                                   "<SLICE id=\"1\" start=\"0\" end=\"12000\" drumClass=\"0\"/><SLICE id=\"2\" start=\"12000\" end=\"24000\" drumClass=\"1\"/>"
                                   "<SLICE id=\"3\" start=\"24000\" end=\"36000\" drumClass=\"2\"/><SLICE id=\"4\" start=\"36000\" end=\"48000\" drumClass=\"0\"/></SLICES></NEURO_MPC>");
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
        std::printf ("  state delivered in %s format: %s\n", formatName.toRawUTF8(), loadNeuroState (*instance, state, formatName) ? "OK" : "FAILED");
        instance->prepareToPlay (48000.0, 512);
        for (auto [note, name] : { std::pair { 36, "kick" }, { 37, "snare" }, { 38, "closed hat" } })
        {
            juce::AudioBuffer<float> buffer (instance->getTotalNumOutputChannels(), 512);
            std::vector<float> busPeak ((size_t) instance->getBusCount (false), 0.0f);
            for (int block = 0; block < 30; ++block)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (block == 0) { midi.addEvent (juce::MidiMessage::allNotesOff (1), 0); midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0); }
                instance->processBlock (buffer, midi);
                for (int b = 0; b < instance->getBusCount (false); ++b)
                    busPeak[(size_t) b] = juce::jmax (busPeak[(size_t) b], instance->getBusBuffer (buffer, false, b).getMagnitude (0, 512));
            }
            std::printf ("  %-10s ->", name);
            for (int b = 0; b < instance->getBusCount (false); ++b)
                if (busPeak[(size_t) b] > 0.0f) std::printf (" %s %.3f", instance->getBus (false, b)->getName().toRawUTF8(), busPeak[(size_t) b]);
            std::printf ("\n");
        }
        instance->releaseResources();
    }
}

// A user's session, headless: load a drum loop, Auto-Slice, play pads; Deconstruct; Separate drums. Each pad press is
// rendered with the host's bus layout (main only, or every output enabled as in Logic's multi-output instrument),
// reporting the level on the main output and on the class outputs.
static void workflow (const juce::String& loop, bool allBuses)
{
    NeuroMPCProcessor p;
    if (allBuses)
    {
        auto layout = p.getBusesLayout();
        for (auto& bus : layout.outputBuses) bus = juce::AudioChannelSet::stereo();
        p.setBusesLayout (layout);
    }
    p.prepareToPlay (48000.0, 512);
    juce::String error;
    p.loadSample (juce::File (loop), error);
    p.waitForBackgroundWork (30000);
    auto pressPads = [&] (const char* stage)
    {
        p.processBackgroundResults();
        std::printf ("  [%s] %s: %d slices; pads:", allBuses ? "all outputs on" : "main only", stage, p.getSlices().size());
        for (int pad = 0; pad < 4; ++pad)
        {
            juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), 512);
            juce::MidiBuffer midi;
            p.auditionNote (p.padNote (pad), 0.9f);
            float mainPeak = 0.0f, classPeak = 0.0f;
            for (int b = 0; b < 30; ++b)
            {
                p.processBlock (buf, midi);
                mainPeak = juce::jmax (mainPeak, buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
                for (int c = 2; c < buf.getNumChannels(); ++c) classPeak = juce::jmax (classPeak, buf.getMagnitude (c, 0, 512));
            }
            p.auditionNote (p.padNote (pad), 0.0f);
            std::printf ("  %d %s: main %.2f class %.2f", pad + 1, p.getPads()[pad].name.c_str(), mainPeak, classPeak);
        }
        std::printf ("\n");
    };
    pressPads ("loaded");
    p.editSlices ([&] (auto& set) { set.setStarts (neuro::detectTransients (p.getSample()->buffer.getArrayOfReadPointers(), p.getSample()->buffer.getNumChannels(), p.getSample()->length(), p.getSample()->sampleRate, 0.5f)); });
    p.waitForBackgroundWork (30000);
    pressPads ("Auto-Slice");
    p.deconstructLoop (0);
    p.waitForBackgroundWork (30000);
    pressPads ("Deconstruct");
    p.loadSample (juce::File (loop), error);
    p.separateDrums (0);
    p.waitForBackgroundWork (30000);
    pressPads ("Separate drums");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 3 && juce::String (argv[1]) == "workflow") { workflow (argv[2], false); workflow (argv[2], true); return 0; }
    if (argc == 4 && juce::String (argv[1]) == "keyfeat") { keyFeatures (juce::File (argv[2]), juce::File (argv[3])); return 0; }
    if (argc == 3 && juce::String (argv[1]) == "keys") { keyBenchmark (juce::File (argv[2])); return 0; }
    if (argc == 3 && juce::String (argv[1]) == "library") { librarySnapshot (juce::File (argv[2])); return 0; }
    if (argc == 5 && juce::String (argv[1]) == "tab")
    {
        NeuroMPCProcessor p;
        juce::String error;
        p.prepareToPlay (48000.0, 512);
        p.loadSample (juce::File (argv[3]), error);
        p.editSlices ([] (auto& set) { set.grid (8); });
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<NeuroMPCEditor*> (ed.get());
        editor->setSize (1140, 840);
        editor->showTab (juce::String (argv[4]).getIntValue());
        editor->selectSliceForTest (2);
        editor->jogForTest (0, 7.0, 2, true);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = juce::File (argv[2]).getChildFile ("tab" + juce::String (argv[4]) + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("snapshot: %s\n", file.getFullPathName().toRawUTF8());
        return 0;
    }
    if (argc == 4 && juce::String (argv[1]) == "chords") { chordSnapshot (juce::File (argv[2]), argv[3]); return 0; }
    if (argc < 4) { std::printf ("usage: neuro_field_test <outDir> <kitWav> <loop>...\n"); return 1; }
    const juce::File outDir (argv[1]);
    outDir.createDirectory();
    juce::StringArray loops;
    for (int i = 3; i < argc; ++i) loops.add (argv[i]);
    hostedOutputs ("VST3", argv[2]);
    hostedOutputs ("AudioUnit", argv[2]);
    realLoops (loops);
    kitSnapshot (outDir, loops[0]);
    return 0;
}
