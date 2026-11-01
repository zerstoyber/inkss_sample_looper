# INKSS Sample Looper

Ein Sampler für den Mac. Er läuft als eigenes Programm und als Plugin in Ableton Live (AU und VST3). Samples lassen sich importieren, aufnehmen und neu aufnehmen, loopen, pitchen und zum Takt der DAW legen.

Das ist eine Testversion. Die Oberfläche ist auf Englisch.

## Was es kann

- Bis zu 16 Samples, per Drag-and-Drop oder Import
- Aufnahme von externem Audio und Resample der Ausgabe
- Pro Sample: Lautstärke, Pan, Pitch, Fine, 3-Band-EQ, Loop-Start und Loop-Ende
- Rückwärts abspielen
- Sync zum Host-Tempo, inklusive Quantisierung von Play, Key, Record und Resample
- Speed: bei Sync folgt die Loop-Länge dem Pitch und bleibt am Takt
- Globale Lautstärke, Pitch und ein 3-Band-EQ auf der Ausgabe
- Reihenfolge der Samples über die Pfeile am Namen. Neue Samples landen unten

## Bauen

Voraussetzungen auf dem Mac:

- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.22 oder neuer
- Ninja (`brew install cmake ninja`, falls die Tools fehlen)
- Git, weil JUCE beim ersten Build heruntergeladen wird

Im Projektordner:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Der erste Build dauert länger, weil JUCE 9.0.4 geholt und übersetzt wird.

Danach liegt das Programm hier:

```text
build/INKSSSampler_artefacts/Release/Standalone/INKSS Sampler.app
```

AU und VST3 werden nach dem Build installiert:

- `~/Library/Audio/Plug-Ins/Components/INKSS Sampler.component`
- `~/Library/Audio/Plug-Ins/VST3/INKSS Sampler.vst3`

Die App komplett beenden und neu öffnen. In Ableton das Plugin neu laden oder die Plug-ins neu scannen.

## Testen

1. Die Standalone-App öffnen oder das Plugin auf eine MIDI-Spur legen.
2. Audio auf die Liste ziehen.
3. Play, Loop und Sync ausprobieren.
4. Für Aufnahme in der Standalone unter Options die Eingabe wählen. In Ableton die Spur aufnehmen lassen, wenn MIDI oder Audio ankommen soll.

Aufgenommene Dateien liegen in:

```text
~/Library/Application Support/Inkasso/INKSS Sampler/Samples/
```

## Lizenz von JUCE

Das Programm ist mit [JUCE](https://juce.com) 9.0.4 gebaut. JUCE steht unter der AGPL-3.0, solange keine kommerzielle JUCE-Lizenz vorliegt. Wer eine gebaute Version weitergibt, muss die Bedingungen der AGPL einhalten oder eine JUCE-Lizenz haben. Diese Testdateien sind zum Ausprobieren gedacht.
