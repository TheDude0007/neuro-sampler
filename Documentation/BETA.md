# Neuro Sampler Suite beta test (about 15 minutes)

Thanks for testing. Things will break; telling us what broke is the whole point.

**You need:** an Apple Silicon Mac (M1 or newer) and Logic Pro, Ableton Live, or just the standalone app. A MIDI pad controller is nice to have.

## Install
1. Download the zip from [Releases](https://github.com/TheDude0007/neuro-sampler/releases/latest) and unzip it.
2. Copy `Neuro Sampler Suite.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `Neuro Sampler Suite.component` to `~/Library/Audio/Plug-Ins/Components/`. In Finder, press Shift-Cmd-G and paste the path.
3. The build isn't notarized yet, so macOS blocks it until you run this once in Terminal:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Neuro Sampler Suite.vst3" ~/Library/Audio/Plug-Ins/Components/"Neuro Sampler Suite.component"
   ```
4. Restart your DAW. In Logic, add it as a stereo instrument: AU Instruments ▸ Neuro Sampler Suite.

## Try these
1. **Chop a drum loop.** Click Load, pick a drum loop, open the **CHOP** tab, choose *Chop: kit*, click **Chop**. Do pads A1, A2 and A3 play the kick, snare and hat?
2. **Fix a chop.** Double-click the waveform to add a slice, drag a marker, press Delete on one. Does it feel precise?
3. **Note Repeat.** Hold **REPEAT**, play a pad, change the rate box to 1/32 or 1/128. In the CHOP tab, lower the gate on a longer sample. Does it roll and glitch the way you'd expect?
4. **Stems.** In the **KIT** tab, click **Separate drums**, then **Show stems**, and drag the WAVs into your DAW. How clean are they?
5. **AI drums (optional, 84 MB download).** Load a full song, then **KIT ▸ Extract drums (AI)**. It's slow: expect about a minute or more for a song.

## Experimental: tell us if these work at all
- Inside Akai's MPC 3 desktop software.
- Syncing with MPC hardware over MIDI clock.
- Playing GW-1 and KAOSS NEXUS together ([setup](GMNI_LINK.md)).

## Tell us
[Send feedback here](https://github.com/TheDude0007/neuro-sampler/issues/new?template=beta_feedback.yml). "I got stuck at step 3" is as useful as a crash report.
