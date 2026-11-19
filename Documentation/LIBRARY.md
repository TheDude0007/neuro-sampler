# Sample library (v1.10)

**Library** (top bar) opens a browser over the waveform. It indexes your own sample folders where they are. Nothing is copied or uploaded, so commercial packs stay on your Mac.

## Sections and folders
- A library has **sections** (for example Drums, Melodies, Vocals). Each section indexes one or more folders, including every subfolder.
- **+ Folder** adds a folder to the current section. **+ Section** creates a new section. **Rescan** indexes everything again.
- The settings live in `~/Library/Application Support/NEURO MPC/Library.json`. The index is cached next to it in `Library index.tsv`, so the browser opens instantly. The folders are rescanned once per session in the background (about 51,000 files take 2.7 s).
- Formats: WAV, AIFF, FLAC, MP3, OGG.

## What each sample is (`Source/Core/SampleCategory.h`)
The type is read from the file name first, then from the folders (the innermost folder first), the way producer packs name things:
- **One-shots:** Kick, Snare, Clap, Rim, Closed Hat, Open Hat, Cymbal, 808, Tom, Perc, FX, Vox, Instrument.
- **Loops:** Drum Loop, Break, Hat Loop, Perc Loop, Melodic Loop.
- **Other.**

How names are read:
- **Tokens:** pack slang counts, for example `KWKIK066` → kick, `kd`/`kyx` → kick, `HH`/`hyhatz` → hat, `oh` → open hat, `snr` → snare.
- **Loops:** a sample is a loop if its name says loop, break or a BPM, or if it sits in a loop folder and its name doesn't name a drum. So `KICKS & BREAKS/ARAAB_KICK_…` stays a kick.
- **BPM:** `96 BPM`, `118_BPM`, `bpm095`, `108BPM`.
- **Key:** `E Min`, `Cmin`, `F#m`, `f sharp major`, or a note in brackets: `808 (C)`, `[Eb]`.
- **Result:** on a 51,407-file drum library, 91% of samples get a type.

## Browsing
- **Search:** every word you type must appear in the file's path, for example `808 zay`, `140 bpm`, `cymatics hat`.
- **Filters:** type (All, One-shots, Loops, or one type) and pack (the top folder under the library folder).
- **Preview:** a click previews the sample through the plugin's output, after the gain, at the same level as the mix. The arrow keys browse while previewing. **Stop** ends it, and the preview stops after 20 s.
- **Load / double-click:** loads the sample to slice, chop or play (best for loops and breaks).

## Kit builder
- **Choosing pads:** 16 kit pads in MPC layout (A1 bottom left). Click a pad to target it. **-> Kit pad** (or Return) puts the selected sample there and moves to the next pad. Right-click a pad to clear it.
- **Auto kit:** picks a kit in MPC order: A1 Kick, A2 Snare, A3 Closed Hat, A4 Open Hat, A5 Clap, A6 808, A7 Perc, A8 Rim, A9 Kick, A10 Snare, A11 Closed Hat, A12 Cymbal, A13 Perc, A14 Tom, A15 FX, A16 Vox.
  - With **Same pack**, every sound comes from the pack of the chosen kick when that pack has the type.
  - The same sound is never used twice.
- **Kit -> pads:** writes one WAV to `~/Music/NEURO MPC/Kits/` with each sound (up to 6 s) followed by 50 ms of silence.
  - The kit WAV is resampled to 48 kHz stereo and loaded with one slice per sound, so slice *n* is on pad *n*, named after its sample.
  - An empty kit pad keeps its place as a short silence.
  - Projects save the kit WAV like any sample, so they reopen even if the original packs move.

## Tests
- `neuro_core_tests` `sampleCategoryTests`: real file and folder names from producer packs, covering types, loops vs one-shots, BPM and key.
- `neuro_engine_tests`:
  - a preview plays with no sample loaded and Stop silences it;
  - **Kit -> pads** with 3 sounds and an empty pad gives 4 slices; slice 1 is 0.5 s + 50 ms long, the pads are named, and pad A2 plays sound 2 at its pitch.
- `neuro_field_test library <outDir>` indexes the configured library and writes `library.png`.
