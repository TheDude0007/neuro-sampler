# Chords (v1.9)

Chords come from the **song key** (the Scale and Scale Root boxes, or the Play In key when set), so they are always in key. Engine: `Source/Core/Harmony.h` (diatonic chords, names, Roman numerals) and `VoiceEngine::playChord`.

## Modes (CHORD tab ▸ Chords)
- **Off.**
- **Harmonize:** a pitched, non-drum slice plays the key's chord on its own note. For example, an A slice in C major plays A minor (A C E). Keys mode harmonizes the key you play. Pads, MIDI, the arp, the sequencer and Note Repeat all go through it. Drums and unpitched slices play as themselves.
- **Chord pads:** in Pads mode, pad *n* of a bank is a chord:
  - pads 1–7 = degrees I … VII, pad 8 = I an octave up;
  - pads 9–16 = the same chords one shape richer (triad → 7th, 7th → 9th, sus → 7sus4, power → triad).

  Chord pads play the **chord sound**: the Keys-mode slice, which you set with **Sound: selected slice**. Empty pads play chords too.

## Shapes
Shapes are stacked from the scale, so the chord quality follows the key:
- **Triad, 7th, 9th** — 9ths are named maj9 / m9 / 9.
- **sus2, sus4.**
- **Power:** root, 5th, octave.
- **7sus4.**

Scales with fewer than 5 notes, and Chromatic, fall back to major.

## Voicing
- **Close:** each tone at the nearest pitch to its slice's own pitch, so it moves as little as possible and sounds cleanest. Thirds and sevenths below E3 go up an octave to avoid mud, and no two tones share a note.
- **Drop 2:** the second-highest tone drops an octave.
- **Wide:** the root drops an octave (not below C1).

## Strum, level and choke
- **Strum:** 0–120 ms between tones, started from low to high.
- **Level:** each tone plays at 1/√n of the level, so chords are about as loud as single notes.
- **Choke:** a chord chokes its group once and then plays all its tones.

## Timbre chords
Four slots (Root, 3rd, 5th, 7th/9th) can each name a slice. That chord tone is then played by that slice, retuned to the chord tone nearest its own pitch. For example, root = bass stem, 3rd = vocal chop, 5th = synth pad.
- **Same:** uses the played slice.
- **A slot pointing at a missing slice** falls back to the played slice.
- **An unpitched slot slice** plays as itself.

## Pitch mode
- **Keep length (default):** every tone uses a background render of its slice at a whole-semitone shift of −7 … +7, so it keeps the slice's length.
  - Renders are made for: the chord sound, the timbre-chord slices, and in Harmonize mode every tonal slice.
  - Until a render is ready, and for wider shifts, the tone is resampled.
- **Resample:** instant. Higher tones are shorter (the classic sampler sound).

## Display
The CHORD tab shows the chord just played (for example "Am  vi") and the chord chart of the 16 pads for the current key and shape.

## Parameters (automatable, MIDI-mappable)
`chords`, `chordShape`, `chordVoicing`, `chordRender`, `chordStrum`, `chordVoiceRoot`, `chordVoice3rd`, `chordVoice5th`, `chordVoice7th`.

## Tests (`neuro_engine_tests`, measured from the plugin output with Goertzel filters)
- **Harmonize, resample:** a 220 Hz tone (A3) in C major plays E3 + A3 + C4, with D4 absent, and the chord is A.
- **Harmonize, keep length:** all tones last 96000 samples (the original length).
- **Strum 50 ms:** only E3 sounds in the first 40 ms.
- **Chord pads:**
  - A1 = C (C4 E4 G3) and A5 = G (G3 B3 D3);
  - the chart reads Dm (ii), Bdim (vii°), C' (octave), and G7 on A13.
- **Drop 2:** C4 moves to C3.
- **Missing timbre slot:** falls back to the played slice.
- **Drums:** a slice classed as a drum is not harmonized.

## Not yet (roadmap)
- **v1.12:** a theory brain that suggests "what comes next" chords, plus MIDI chord in/out.
- **v1.13:** a melodic engine (a progression-aware arp, Tonnetz walks, Euclidean and harmonic-series patterns).
