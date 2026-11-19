#pragma once

#include "../Library/SampleLibrary.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The sample library browser (opens over the waveform). Search your own folders by type, pack, name, BPM and key;
// click to preview, double-click to load, build a 16-pad kit from one-shots (or let Auto kit pick one).
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::Timer
{
public:
    explicit LibraryPanel (NeuroMPCProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &sectionBox, &addFolderButton, &addSectionButton, &rescanButton, &search, &typeBox,
                                                                 &packBox, &status, &list, &previewToggle, &loadButton, &addToKitButton, &stopButton,
                                                                 &kitTitle, &autoKitButton, &samePackToggle, &kitToPadsButton, &clearKitButton, &closeButton })
            addAndMakeVisible (c);

        for (auto& slot : slots)
            addAndMakeVisible (slot);

        search.setTextToShowWhenEmpty ("Search: name, pack, 140 bpm, Cmin...", juce::Colours::grey);
        search.onTextChange = [this] { refresh(); };
        search.onReturnKey = [this] { list.grabKeyboardFocus(); };

        typeBox.addItem ("All types", 1);
        typeBox.addItem ("One-shots", 2);
        typeBox.addItem ("Loops", 3);
        typeBox.addSeparator();
        for (int t = 0; t < neuro::numSampleTypes; ++t)
            typeBox.addItem (neuro::sampleTypeNames[t], 10 + t);
        typeBox.setSelectedId (1, juce::dontSendNotification);
        typeBox.onChange = [this] { refresh(); };
        packBox.onChange = [this] { refresh(); };
        sectionBox.onChange = [this] { packsShownFor = -1; refresh(); };

        addFolderButton.setTooltip ("Add a folder of samples to this section (indexed in place, nothing is copied)");
        addFolderButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Add a sample folder", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().isDirectory())
                                          library->addFolder (currentSection(), fc.getResult());
                                  });
        };
        addSectionButton.setTooltip ("New section (e.g. Melodies, Vocals, FX), then add folders to it");
        addSectionButton.onClick = [this]
        {
            auto* w = new juce::AlertWindow ("New section", "Name the section, then add folders to it.", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "Melodies");
            w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
            {
                if (r == 1)
                {
                    const auto index = library->addSection (w->getTextEditorContents ("name"));
                    rebuildSections();
                    sectionBox.setSelectedId (index + 1);
                }
            }), true);
        };
        rescanButton.setTooltip ("Index the folders again (after adding or removing samples)");
        rescanButton.onClick = [this] { library->rescan(); };

        list.setModel (this);
        list.setRowHeight (20);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff101218));
        list.setMultipleSelectionEnabled (false);

        previewToggle.setToggleState (true, juce::dontSendNotification);
        previewToggle.setTooltip ("Play each sample when you select it (arrow keys browse)");
        loadButton.setTooltip ("Load the selected sample to slice and play (double-click does the same)");
        loadButton.onClick = [this] { loadSelected(); };
        addToKitButton.setTooltip ("Put the selected sample on the highlighted kit pad, then move to the next pad");
        addToKitButton.onClick = [this] { addSelectedToKit(); };
        stopButton.onClick = [this] { proc.stopPreview(); };

        kitTitle.setText ("KIT  (click a pad to target it, right-click to clear)", juce::dontSendNotification);
        kitTitle.setFont (juce::FontOptions (11.0f));

        for (int i = 0; i < (int) slots.size(); ++i)
        {
            auto& slot = slots[(size_t) i];
            slot.setClickingTogglesState (false);
            slot.onClick = [this, i]
            {
                if (juce::ModifierKeys::currentModifiers.isPopupMenu()) { kit[(size_t) i] = {}; }
                else if (kit[(size_t) i].path.isNotEmpty() && targetSlot == i && previewToggle.getToggleState()) { proc.previewFile (kit[(size_t) i].path); }
                targetSlot = i;
                updateSlots();
            };
        }

        autoKitButton.setTooltip ("Pick a kit for you in MPC order: kick, snare, hats, clap, 808, percs, cymbal, tom, FX, vox");
        autoKitButton.onClick = [this] { autoKit(); };
        samePackToggle.setToggleState (true, juce::dontSendNotification);
        samePackToggle.setTooltip ("Auto kit takes every sound from one pack when it can (a kit that belongs together)");
        kitToPadsButton.setTooltip ("Build the kit: one WAV in ~/Music/NEURO MPC/Kits, a slice per sound, on pads A1-A16");
        kitToPadsButton.onClick = [this] { buildKit(); };
        clearKitButton.onClick = [this] { kit = {}; targetSlot = 0; updateSlots(); };
        closeButton.onClick = [this] { proc.stopPreview(); setVisible (false); if (onClose) onClose(); };

        status.setFont (juce::FontOptions (11.0f));
        status.setJustificationType (juce::Justification::centredRight);
        rebuildSections();
        updateSlots();
        startTimerHz (4);
    }

    ~LibraryPanel() override { list.setModel (nullptr); }

    std::function<void()> onClose, onLoaded;

    void visibilityChanged() override
    {
        if (isVisible())
        {
            library->rescanIfStale();
            refresh();
            search.grabKeyboardFocus();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xf0181a22));
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRect (getLocalBounds());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (24);
        closeButton.setBounds (top.removeFromRight (24));
        top.removeFromRight (6);
        sectionBox.setBounds (top.removeFromLeft (130));
        top.removeFromLeft (4);
        for (auto* b : { &addFolderButton, &addSectionButton, &rescanButton })
        {
            b->setBounds (top.removeFromLeft (b == &rescanButton ? 64 : 70));
            top.removeFromLeft (4);
        }
        top.removeFromLeft (6);
        typeBox.setBounds (top.removeFromLeft (130));
        top.removeFromLeft (4);
        packBox.setBounds (top.removeFromLeft (200));
        top.removeFromLeft (6);
        status.setBounds (top.removeFromRight (130));
        search.setBounds (top);

        r.removeFromTop (6);
        auto right = r.removeFromRight (juce::jmin (330, r.getWidth() / 3));
        r.removeFromRight (8);
        auto buttons = r.removeFromBottom (24);
        r.removeFromBottom (4);
        list.setBounds (r);
        previewToggle.setBounds (buttons.removeFromLeft (90));
        for (auto* b : { &loadButton, &addToKitButton, &stopButton })
        {
            b->setBounds (buttons.removeFromLeft (b == &stopButton ? 50 : 90));
            buttons.removeFromLeft (4);
        }

        kitTitle.setBounds (right.removeFromTop (16));
        auto kitButtons = right.removeFromBottom (24);
        right.removeFromBottom (4);
        const auto cell = juce::jmin (right.getWidth() / 4, right.getHeight() / 4);
        auto grid = right.withSizeKeepingCentre (cell * 4, cell * 4);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) // MPC layout: pad 1 bottom left
                slots[(size_t) ((3 - row) * 4 + col)].setBounds (grid.getX() + col * cell, grid.getY() + row * cell, cell - 3, cell - 3);
        for (auto* b : std::initializer_list<juce::Component*> { &autoKitButton, &samePackToggle, &kitToPadsButton, &clearKitButton })
        {
            b->setBounds (kitButtons.removeFromLeft (b == &kitToPadsButton ? 90 : b == &samePackToggle ? 84 : 70));
            kitButtons.removeFromLeft (4);
        }
    }

    // ---- ListBoxModel ----
    int getNumRows() override { return (int) results.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) results.size())
            return;

        const auto& e = results[(size_t) row];
        if (selected) g.fillAll (juce::Colour (0xff3a3f10));
        else if (row % 2) g.fillAll (juce::Colours::white.withAlpha (0.03f));

        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (juce::Colour (0xffffe14a).withAlpha (0.85f));
        g.drawText (neuro::sampleTypeNames[(int) e.info.type], r.removeFromLeft (84), juce::Justification::centredLeft);
        auto meta = juce::String();
        if (e.info.bpm > 0) meta << e.info.bpm << " bpm ";
        if (e.info.keyPc >= 0) meta << neuro::pitchClassName (e.info.keyPc) << (e.info.minor ? "m" : "");
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawText (meta, r.removeFromRight (90), juce::Justification::centredRight);
        g.drawText (e.pack, r.removeFromRight (juce::jmin (260, r.getWidth() / 3)), juce::Justification::centredLeft, true);
        g.setColour (juce::Colours::white);
        g.drawText (e.name, r, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int row) override
    {
        if (previewToggle.getToggleState() && row >= 0 && row < (int) results.size())
            proc.previewFile (results[(size_t) row].path);
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }
    void returnKeyPressed (int) override { addSelectedToKit(); }

    juce::String getTooltipForRow (int row) override { return row >= 0 && row < (int) results.size() ? results[(size_t) row].path : juce::String(); }

private:
    NeuroMPCProcessor& proc;
    juce::SharedResourcePointer<neuro::SampleLibrary> library;
    juce::ComboBox sectionBox, typeBox, packBox;
    juce::TextButton addFolderButton { "+ Folder" }, addSectionButton { "+ Section" }, rescanButton { "Rescan" };
    juce::TextEditor search;
    juce::Label status, kitTitle;
    juce::ListBox list;
    juce::ToggleButton previewToggle { "Preview" }, samePackToggle { "Same pack" };
    juce::TextButton loadButton { "Load" }, addToKitButton { "-> Kit pad" }, stopButton { "Stop" };
    juce::TextButton autoKitButton { "Auto kit" }, kitToPadsButton { "Kit -> pads" }, clearKitButton { "Clear" }, closeButton { "X" };
    std::array<juce::TextButton, 16> slots;
    std::array<neuro::LibraryEntry, 16> kit {};
    std::vector<neuro::LibraryEntry> results;
    std::unique_ptr<juce::FileChooser> chooser;
    int targetSlot = 0, shownVersion = -1, packsShownFor = -1;
    std::mt19937 rng { std::random_device {}() };

    int currentSection() const { return juce::jmax (0, sectionBox.getSelectedId() - 1); }

    int currentType() const
    {
        const auto id = typeBox.getSelectedId();
        return id == 2 ? neuro::libraryOneShots : id == 3 ? neuro::libraryLoops : id >= 10 ? id - 10 : neuro::libraryAllTypes;
    }

    void rebuildSections()
    {
        const auto keep = sectionBox.getSelectedId();
        sectionBox.clear (juce::dontSendNotification);
        const auto sections = library->getSections();
        for (size_t i = 0; i < sections.size(); ++i)
            sectionBox.addItem (sections[i].name, (int) i + 1);
        sectionBox.setSelectedId (keep > 0 && keep <= (int) sections.size() ? keep : 1, juce::dontSendNotification);
    }

    void refresh()
    {
        if (packsShownFor != currentSection() || shownVersion != library->getVersion())
        {
            const auto keep = packBox.getText();
            packBox.clear (juce::dontSendNotification);
            packBox.addItem ("All packs", 1);
            const auto packs = library->getPacks (currentSection());
            for (int i = 0; i < packs.size(); ++i)
                packBox.addItem (packs[i], i + 2);
            const auto index = packs.indexOf (keep);
            packBox.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
            packsShownFor = currentSection();
        }

        const auto pack = packBox.getSelectedId() > 1 ? packBox.getText() : juce::String();
        results = library->query (currentSection(), currentType(), search.getText(), pack);
        list.updateContent();
        list.repaint();
        updateStatus();
    }

    void updateStatus()
    {
        const auto sections = library->getSections();
        const auto noFolders = currentSection() < (int) sections.size() && sections[(size_t) currentSection()].roots.isEmpty();
        status.setText (library->isScanning() ? "Indexing... " + juce::String (library->getScanCount())
                        : noFolders ? juce::String ("Add a folder ->") : juce::String ((int) results.size()) + " samples",
                        juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (! isVisible())
            return;

        if (shownVersion != library->getVersion())
        {
            rebuildSections();
            refresh();
            shownVersion = library->getVersion();
        }

        updateStatus();
    }

    const neuro::LibraryEntry* selected() const
    {
        const auto row = list.getSelectedRow();
        return row >= 0 && row < (int) results.size() ? &results[(size_t) row] : nullptr;
    }

    void loadSelected()
    {
        if (const auto* e = selected())
        {
            proc.stopPreview();
            juce::String error;
            if (proc.loadSample (juce::File (e->path), error) && onLoaded)
                onLoaded();
        }
    }

    void addSelectedToKit()
    {
        if (const auto* e = selected())
        {
            kit[(size_t) targetSlot] = *e;
            targetSlot = (targetSlot + 1) % (int) kit.size();
            updateSlots();
        }
    }

    void autoKit()
    {
        using T = neuro::SampleType;
        static constexpr T order[] { T::kick, T::snare, T::closedHat, T::openHat, T::clap, T::bass808, T::perc, T::rim,
                                     T::kick, T::snare, T::closedHat, T::cymbal, T::perc, T::tom, T::fx, T::vox };
        juce::String pack;

        if (samePackToggle.getToggleState())
            if (const auto kick = library->pickRandom (currentSection(), T::kick, packBox.getSelectedId() > 1 ? packBox.getText() : juce::String(), rng))
                pack = kick->pack;

        for (size_t i = 0; i < kit.size(); ++i)
        {
            auto pick = library->pickRandom (currentSection(), order[i], pack, rng);
            // Avoid the same sound twice (the second kick/snare/hat is a different one when the pack has it).
            for (int tries = 0; pick && tries < 4 && std::any_of (kit.begin(), kit.begin() + (long) i, [&] (const auto& k) { return k.path == pick->path; }); ++tries)
                pick = library->pickRandom (currentSection(), order[i], pack, rng);
            kit[i] = pick.value_or (neuro::LibraryEntry {});
        }

        targetSlot = 0;
        updateSlots();
    }

    void buildKit()
    {
        std::vector<juce::File> files;
        int last = -1;
        for (int i = 0; i < (int) kit.size(); ++i)
            if (kit[(size_t) i].path.isNotEmpty())
                last = i;

        if (last < 0)
            return;

        juce::String name;
        for (int i = 0; i <= last; ++i)
        {
            files.push_back (kit[(size_t) i].path.isNotEmpty() ? juce::File (kit[(size_t) i].path) : juce::File());
            if (name.isEmpty() && kit[(size_t) i].pack.isNotEmpty())
                name = kit[(size_t) i].pack.substring (0, 40);
        }

        proc.stopPreview();
        juce::String error;
        if (proc.buildKit (files, name + " kit", error))
        {
            if (onLoaded) onLoaded();
            closeButton.triggerClick();
        }
        else
        {
            status.setText (error, juce::dontSendNotification);
        }
    }

    void updateSlots()
    {
        for (int i = 0; i < (int) slots.size(); ++i)
        {
            auto& slot = slots[(size_t) i];
            const auto& e = kit[(size_t) i];
            slot.setButtonText ("A" + juce::String (i + 1) + "\n" + (e.path.isEmpty() ? juce::String ("-") : e.name.substring (0, 14)));
            slot.setTooltip (e.path.isEmpty() ? "Empty: select a sample and press -> Kit pad (or Return)" : e.path);
            slot.setColour (juce::TextButton::buttonColourId, i == targetSlot ? juce::Colour (0xff6a5c10)
                                                              : e.path.isEmpty() ? juce::Colour (0xff22252e) : juce::Colour (0xff343a48));
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};
