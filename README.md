# INKSS Sample Looper

A sampler for Mac. It runs as its own app and as a plugin in Ableton Live (AU and VST3). Import, record, and resample audio, then loop it, pitch it, and lock it to the DAW tempo.

This is a test build. The interface is in English.

A prebuilt download for Apple silicon Macs is attached to the [GitHub release](https://github.com/zerstoyber/inkss_sample_looper/releases). Building from source is described below.

## What it does

- Up to 32 samples, by drag and drop or import
- Record external audio, and resample the output
- Per sample: volume, pan, pitch, fine, 3-band EQ, loop start and loop end
- Reverse playback
- Sync to the host tempo, including quantization for play, key, record, and resample
- Speed: while synced, pitch also changes the loop length and the loop stays on the grid
- Global volume, pitch, and a 3-band EQ on the output
- Reorder samples with the arrows on the name. New samples are added at the bottom

## Build

On a Mac you need:

- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.22 or newer
- Ninja (`brew install cmake ninja` if they are missing)
- Git, because the first build downloads JUCE

From the project folder:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The first build takes longer because it fetches and compiles JUCE 9.0.4.

The app is written to:

```text
build/INKSSSampler_artefacts/Release/Standalone/INKSS Sampler.app
```

AU and VST3 are installed after the build:

- `~/Library/Audio/Plug-Ins/Components/INKSS Sampler.component`
- `~/Library/Audio/Plug-Ins/VST3/INKSS Sampler.vst3`

Quit the app completely and open it again. In Ableton, reload the plugin or rescan plug-ins.

## Try it

1. Open the standalone app, or put the plugin on a MIDI track.
2. Drop audio onto the list.
3. Try play, loop, and sync.
4. To record in the standalone app, choose the input under Options. In Ableton, arm the track when MIDI or audio should reach the plugin.

Recorded files are stored in:

```text
~/Library/Application Support/Inkasso/INKSS Sampler/Samples/
```

## JUCE license

The program is built with [JUCE](https://juce.com) 9.0.4. JUCE is under AGPL-3.0 unless you have a commercial JUCE license. Sharing a built copy means following the AGPL or holding a JUCE license. These files are for trying the instrument.
