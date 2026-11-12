# Pitch & Key Analysis (Phase 7)

Pitch detection and key detection are separate tasks with separate results. Both report uncertainty instead of guessing.

## Fundamental pitch: `Source/Core/PitchDetector.h`
- **YIN** (de Cheveigné & Kawahara, 2002) on a mono mixdown: 25 ms frames, 40–2000 Hz, threshold 0.15, parabolic lag interpolation, first 8 s of the region.
- **Aggregation:** the median over voiced frames, refined by averaging frames within ±50 cents.
- **Confidence:** voiced fraction × stability (share of frames near the median) × periodicity (1 − aperiodicity).
- **Polyphony check.** YIN reports a chord's common period: A–C♯–E is periodic at A1. The candidate f0 is therefore checked in the spectrum (1 s Hann-windowed Goertzel):
  - f0 + 2·f0 must hold at least 10% of the harmonic energy (h = 1..8);
  - partials h = 1..4 must peak *at* h·f0, not ±50 cents beside it.

  If either check fails, the result is marked `ambiguous` and capped at confidence 0.4.
- **Status:**
  - `pitched`: confidence ≥ 0.6;
  - `uncertain`: some voicing, low confidence or ambiguous;
  - `unpitched`: noise or silence;
  - `tooShort`: shorter than one analysis frame.
- **Output:** frequency, fractional MIDI note, nearest note and cents. MIDI note m = 69 + 12·log2(f/440).

## Key: `Source/Core/KeyDetector.h`
- **Chroma:** a Goertzel bank over C2–B7 on Hann-windowed ~170 ms frames (8192 samples at 48 kHz) with 50% overlap, using the first 60 s.
- **Overtone subtraction (key only):** each note loses 0.3 × (1, 1/1.5, 1/2, 1/2.5) of the notes an octave, a 12th, two octaves and a 17th below, so a note's 3rd harmonic doesn't pull the key toward its dominant.
- **Profiles:** Pearson correlation with the 24 "bgate" key profiles (Faraldo et al., values as published in Essentia).
- **`key`** only when best r ≥ 0.6, the margin over the runner-up is ≥ 0.05, and at least 4 pitch classes each hold > 5% of the energy. Otherwise **`uncertain`**, still reporting the top candidates. Silence gives **`unavailable`**.
- Runs on the whole sample and on every slice.

### Real recordings (v1.8)
Eight labelled melody loops (Future Bass & Melodic Trap pack). Two labels don't match their audio: "Nimbus E" plays E♭–B♭–F–G (E♭ major) and "Twinkle B♭m" plays E–F♯–B–D–A. On the other seven (Nimbus scored as E♭):

| Method | Right |
|---|---|
| Krumhansl–Kessler, raw chroma (v1.7) | 3 / 7 |
| Temperley, EDMA or Shaath profiles (best settings) | 4 / 7 |
| **bgate + overtone subtraction (v1.8)** | **5 / 7** (two of them flagged uncertain, so not applied automatically) |

The two misses, "Kin Am" (reads G major, uncertain) and "Proper F" (reads A minor, and the melody has no B♭), are ambiguous melodies. Short monophonic melodies are the hard case; full songs and chord loops give clearer chroma.

## Chords: `Source/Core/ChordDetector.h`
- Template matching (cosine similarity) of the chroma against 16 chord types on 12 roots: major, m, dim, aug, sus2, sus4, 7, maj7, m7, m7b5, dim7, 6, m6, add9, madd9, m(maj7).
- **Bass:** a bass chroma (C2–B3, weighted toward the lowest notes) breaks ties between chords with the same notes (Am7 vs C6) and names inversions (C/E).
- **`chord`** when the score is ≥ 0.8 and beats the runner-up; **`uncertain`** from 0.65; at least 3 clear pitch classes. A region the pitch detector hears as one note counts as a chord only if every chord tone holds ≥ 30% of the strongest (a note's overtones hold ~10%; a major triad is harmonics 4:5:6).
- Tested: C, Am, Am7, C6, G7, Fmaj7, Bdim, C/E, Dsus4, E♭maj7, F♯m7b5 all named correctly; noise and single notes are not chords; a D–G–A–D loop cut into 4 slices reads D G A D.

## Song key and Play In (v1.8)
- **Song key** = the Scale + Scale Root parameters. When you load a sample, its confident key sets them (minor → Natural minor). Not on project or preset restore, and not while **Song Key Lock** is on. After **Split stems**, the key of Bass + Other + Vocals (up to 4 min) is used for the song and for any stem loaded from its folder. **Key... > Use detected key** applies the best guess even when uncertain.
- **Play In** (Original, C … B): musical slices (a clear note or chord, tonal class, a set root, or not yet analysed) move from the song key by the smallest shift, −6 … +5 st. **Keep length** uses background renders (resampling until ready); **Resample** is instant and changes length. **All slices** moves drums too. Keys mode is not shifted (you play the notes); its scale snapping, the arp and Chord-Lock use the Play In key.
- Tested: song key D major from a D–G–A–D loop; kept on restore and when locked; A3 tone with song key A, Play In C → 60.02 with length kept (96,000 samples), Resample → 60.00 at 80,726 samples; Play In G → 55.00; Original → 57.00.

## Integration
- Analysis runs on a `BackgroundWorker` thread, the same coalescing worker as stretch rendering. It covers the whole sample and every slice, and is requested again when a slice's region changes. A stale result is never shown: the UI says "analysing…" until the result for the current region arrives.
- **Manual correction:** each slice has a `rootNote` (the Root dropdown, which defaults to the detected pitch) that persists with the project. Detected results are recomputed after a restore; the algorithms are deterministic.

## Measured results (`neuro_core_tests`, `neuro_engine_tests`; 48 kHz synthetic signals)
| Input | Result |
|---|---|
| Sines A1–A6 (55–1760 Hz) | pitched, error ≤ 1 cent (A6: +1.0 c), confidence 1.00 |
| A4 + 30 cents | 69.301 (+30.1 c) |
| G3, 8 harmonics | G3, < 5 c |
| A2 with a −20 dB fundamental, strong 2nd/3rd harmonics | pitched, A2 |
| White noise / silence | unpitched |
| A–C♯–E triad / A–B♭ cluster / C–F♯ tritone | uncertain, ambiguous (without the polyphony check: falsely pitched at A1 / 69.5 / 32.3) |
| I–IV–V–I in C major, F♯ major | correct key, r 0.96, margin 0.22 |
| i–iv–V–i in A minor | A minor, r 0.90, margin 0.30 |
| Single note A4 | uncertain (A minor vs A major, margin 0.04) |
| Chromatic scale | uncertain (r 0.31) |
| 2 s stereo sample + 2 slices, end to end | 141 ms |

## Known limitations
- **Inputs:** the automated tests use synthetic signals; real recordings were measured on 8 labelled melody loops only (above). Full mixed songs are not yet measured.
- **Polyphony check:** it is heuristic. A sparse chord whose lowest note carries strong low harmonics may still read as that note.
- **Key profiles:** major/minor profiles suit Western tonal music. Relative major/minor pairs are often close, so expect `uncertain` on short or modal material. Modes (Dorian …) are never detected, only chosen.
- **Chords:** names come from chroma, so chords with the same notes in a weak bass (Am7 / C6) can swap, and busy mixes with drums read as `uncertain` more often than clean chord loops.
- **Coverage:** only the first 8 s (pitch) and 60 s (key) of a region are analysed.
