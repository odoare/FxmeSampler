# FxmeSampler

FxmeSampler is a JUCE-based sampling instrument plugin featuring, a flexible multi-channel mixer, built-in convolution reverb, and advanced velocity mapping. All configurations are defined via a portable `mapping.xml` file embedded in the plugin resources.

Licence: LGPL3, see LICENSE file

** Contact: olivier.doare@ensta.fr **

## Overview

The instrument's architecture is defined by a `mapping.xml` file embedded in the plugin's binary resources. This file controls:
*   **Sample Mapping:** Which samples play on which notes/velocities.
*   **Voice Architecture:** Envelopes (ADSR), looping, and playback modes (One-shot).
*   **Routing:** How samples are routed to mixer channels.
*   **Mixer Layout:** Definition of strips, buses, and effect chains.
*   **UI Customization:** Colors, icons, and welcome screen.

> **Effects as standalone plugins:** All the effects used in FxmeSampler (EQ, dynamics, reverb, delay, saturation, etc.) are also available as independent VST3/AU plugins in the [FxmeFX](https://github.com/odoare/FxmeFX) repository.

## Installation

Download the latest release from the [Releases](../../releases) page and follow the instructions for your platform.

### macOS

The macOS builds are universal (Intel and Apple Silicon) and run on macOS 10.13 or later. They are **not signed or notarized**, so macOS quarantines anything downloaded through a browser. This is the usual reason a freshly installed plugin never shows up in a DAW, with no error message at all.

1. Download `FxmeSampler-macOS.pkg`.
2. Because the installer is unsigned, double-clicking it will be refused. Right-click the `.pkg` and choose **Open**, then confirm. The installer copies the VST3 to `/Library/Audio/Plug-Ins/VST3/` and the AU to `/Library/Audio/Plug-Ins/Components/`.
3. Remove the quarantine flag from the installed bundles (this is the step that makes them visible to the DAW):
   ```bash
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Black Widow Drums.vst3"
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/CenturyDrums.vst3"
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Black Widow Drums.component"
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/CenturyDrums.component"
   ```
4. Restart your DAW. Logic Pro users may need to trigger a rescan in **Logic Pro → Plug-in Manager**.

If you prefer not to use the installer, `FxmeSampler-VST3-macOS-universal.zip` and `FxmeSampler-AU-macOS-universal.zip` contain the same bundles; copy them to the folders above (or to `~/Library/Audio/Plug-Ins/…` for a per-user install) and run the same `xattr` command on them.

### Windows

1. Download `BlackWidowDrums-Windows-Setup.exe` and/or `CenturyDrums-Windows-Setup.exe` (one installer per kit).
2. Right-click and choose **Run as administrator**, then follow the installer steps — it copies the VST3 to `C:\Program Files\Common Files\VST3\`.
3. In Reaper, go to **Options → Preferences → Plug-ins → VST** and click **Re-scan** to detect the new plugin.

### Linux

1. Download `FxmeSampler-VST3-Linux-x86_64.zip`.
2. Unzip and copy the `.vst3` bundle to `~/.vst3/`:
   ```bash
   unzip FxmeSampler-VST3-Linux-x86_64.zip
   cp -r vst3_staging/*.vst3 ~/.vst3/
   ```
3. In Reaper, go to **Options → Preferences → Plug-ins → VST** and click **Re-scan**.

## Building

This project uses CMake and requires JUCE 8 as a sibling directory (`../JUCE`). Everything else comes from the [FxmeFX](https://github.com/odoare/FxmeFX) submodule, which itself carries [FxmeTools](https://github.com/odoare/FxmeTools) (shared controls, look-and-feel, DSP) and WDL (convolution engine) as nested submodules, so the checkout has to be recursive:

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
```

Artefacts are written per kit, to `build/Kits/BlackWidow/BlackWidowDrums_artefacts/Release/` and `build/Kits/Century/CenturyDrums_artefacts/Release/`. A single kit can be built with `-DKIT=BlackWidow` or `-DKIT=Century`.

### The dev host

Compiling a kit to hear it is a slow way to author one. `FxmeSamplerDev` is a
standalone application that takes a kit folder — a `mapping.xml` plus its
samples and artwork, never compiled — and plays it:

```bash
cmake -B build-dev -DCMAKE_BUILD_TYPE=Release -DKIT=None -DBUILD_DEV_HOST=ON
cmake --build build-dev --parallel 8
./build-dev/Dev/FxmeSamplerDev_artefacts/Release/FxmeSamplerDev [kit folder]
```

(It embeds only the shared artwork and impulse responses — a few megabytes
rather than a kit's few hundred — so unlike a kit build it takes a full
parallel build without running the machine out of memory.)

It is off by default (`BUILD_DEV_HOST=OFF`), and `-DKIT=None` keeps it from
building a kit plugin alongside. **Load kit…** picks a folder, **Reload**
re-reads it after an edit, and the last folder is remembered between runs. The
window holds the kit's own editor, unchanged — every parameter, preset and
effect a compiled kit has.

Presets saved while testing go to the kit folder's `presets/` directory rather
than the platform preset folder, so they are already in place to be embedded
when the kit is compiled.

To check a mapping without opening a window — from a script, or before a
build — use `--check`, which exits non-zero if anything the mapping names
cannot be found:

```console
$ FxmeSamplerDev --check Kits/BassTest/data
Folder      : /home/doare/src/FxmeSampler/Kits/BassTest/data
Mapping     : /home/doare/src/FxmeSampler/Kits/BassTest/data/mapping.xml
Files       : 4
Presets dir : /home/doare/src/FxmeSampler/Kits/BassTest/data/presets
Groups      : 1
Strips      : 2
Parameters  : 184
Channels    : 2
OK
```

Two things do not come from the folder. Shared artwork and the impulse
responses are embedded in the dev host itself, because `ConvolReverb` resolves
IR names through `BinaryData` directly; a mapping naming one of the standard
IRs works, a mapping naming an IR of its own does not. And a kit folder can
only be loaded at startup or by the Load button — never by a DAW — because the
parameter list is fixed the moment the processor is constructed. That is the
whole reason this is an application and not a plugin.

> **Note:** The embedded demo kit files (wav samples and mapping.xml) currently live under `MyKits/FakeAmbixKit/Data/`, which is gitignored. Move them to a tracked location and update the paths in `CMakeLists.txt` before running CI builds.

## Configuration: `mapping.xml`

The `mapping.xml` file is the core configuration file. Below is the complete specification of its structure and attributes.

### Root Element
The root element must be `<Mapping>`, matching the file name.

```xml
<Mapping>
    <!-- Child elements go here -->
</Mapping>
```

The older spelling `<Mappings>` is still accepted, so a kit that has not been
converted keeps loading.

### 1. Welcome Tab
Optional. Defines the content for the "Welcome" tab in the UI.

```xml
<WelcomeTab text="My Instrument" img="logo.png" />
```

| Attribute | Type | Description |
| :--- | :--- | :--- |
| `text` | String | The text displayed on the welcome screen. |
| `img` | String | The filename of the image resource to display. |

### 2. Master Settings
Optional. Configures the master output strip.

```xml
<Master channels="10" img="master_icon.png" color="55,169,181"/>
```

| Attribute | Type | Description |
| :--- | :--- | :--- |
| `channels` | Integer | Total number of internal channels feeding the mixer strips. This must correspond to the sum of channels required by all defined strips (e.g., 4 for Ambisonic, 2 for Stereo). |
| `img` | String | Icon resource for the master strip. |
| `color` | String | Color for the strip (format: "r,g,b" or color name). |

### 3. Mixer Configuration
The `<Mixer>` element contains definitions for channel strips and buses.

#### `<Strip>`
Defines a mixer channel strip.

```xml
<Strip type="ambisonicmono" name="Piano_Top" img="piano.jpg" color="148,153,77" effectChain="dynamics"/>
```

| Attribute | Type | Description |
| :--- | :--- | :--- |
| `type` | String | See Strip Types below. |
| `name` | String | Display name of the strip. |
| `img` | String | Icon resource name. |
| `color` | String | Strip color. |
| `effectChain` | String | Effect chain type: `Dynamics` (default), `Amp`, `Reverb` or `Delay`. See Effect Chains below. |
| `resource` | String | Comma-separated list of Impulse Response (IR) filenames, for a reverb strip, an `Amp` chain's cabinet, or a `Reverb` chain. |

##### Strip Types
| Type | Input Channels | Description |
| :--- | :--- | :--- |
| `mono` | 1 | Single-channel input with standard panning to the stereo mix. |
| `stereo` | 2 | Standard stereo input with balance and M/S-based width control. |
| `ms` | 2 | Mid-Side encoded input, decoded to stereo with width adjustment. |
| `ambisonic` | 4 | First-order Ambisonic (B-Format) input decoded to stereo with Azimuth, Elevation, and Width controls. |
| `ambisonicmono`| 5 | Hybrid strip: 4-ch Ambisonic field + 1-ch Proximity Mono mic. Includes a Mix crossfade between decoded field and mono source. |
| `reverb` | 1 | Mono input processed through a mono convolution reverb engine. |
| `stereoreverb` | 1 | Mono input processed through a stereo convolution reverb engine. |

#### `<Bus>`
Defines an auxiliary bus (always stereo).

```xml
<Bus name="RoomReverb" effectChain="Reverb" resource="room_ir.wav" color="purple"/>
```

| Attribute | Type | Description |
| :--- | :--- | :--- |
| `name` | String | Name of the bus. |
| `effectChain` | String | `Dynamics`, `Amp`, `Reverb`, `Delay`, or `None`. See Effect Chains below. |
| `resource` | String | Comma-separated list of IR filenames, for a `Reverb` bus or an `Amp` bus's cabinet. |
| `img` | String | Icon resource name. |
| `color` | String | Bus color. |

**Note:** To prevent feedback loops, a Bus can only send audio to other Buses defined *sequentially after* it in the XML.

#### Effect Chains

Every strip and bus owns one effect chain, chosen by `effectChain=`. Each gets
its own tab in the plugin, named after the strip.

| `effectChain` | Contents | Order |
| :--- | :--- | :--- |
| `Dynamics` (default) | Transient, EQ, compressor, tube | Transient is always first; the other three are permutable |
| `Amp` | EQ, compressor, tube, cabinet IR | The cabinet is always last; the other three are permutable |
| `Reverb` | Convolution reverb, EQ, stereo delay | Fixed |
| `Delay` | Stereo delay | — |
| `None` | Nothing (buses only) | — |

The permutable chains expose an Order selector at the top of their tab. Its
options are the choice parameter's own strings, so what the box shows and what
the audio does cannot drift apart — the fixed stage is named in every option
(`Trans -> Comp -> Tube -> EQ`, `EQ -> Comp -> Tube -> Cab`).

An `Amp` chain's cabinet offers an independent IR and gain per output channel.
Its IR list comes from the strip's `resource=` attribute, the same way a
`Reverb` chain's convolution reverb does — and when the strip names none, it
falls back to the **factory cabinet set**: the ~700 kB of IRs that ship with the
FxmeFX Cab plugin, embedded in every target by `cmake/FactoryCabinets.cmake`. So
`effectChain="Amp"` is playable with nothing else added to the mapping, and
naming your own cabinets in `resource=` replaces that set rather than adding to
it.

Cabinet IRs are looked up in `BinaryData` directly rather than through the
resource provider, so your own cabinets have to be embedded — in `data/wav/`
alongside the samples — and a folder-loaded kit sees the factory set instead of
its own.

### 4. Sample Groups
`<SampleGroup>` elements define shared properties for a set of sounds, such as envelopes and routing.

```xml
<SampleGroup name="Snare" channels="0:6, 1:7" muteGroup="1" midiChannel="10"
             oneShot="false" loop="true" groupLevel="-3.0" minVelocityGain="-20.0"
             attack="0.001" decay="0.2" sustain="0.5" release="0.3" detune="0.0"/>
```

| Attribute | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | Required | Unique identifier for the group. |
| `channels` | String | "0,1" | Output routing. Format: `src:dest` or `dest`. <br>Example: `0:6, 1:7` maps source ch 0 to output 6, source ch 1 to output 7. |
| `muteGroup` | Integer | 0 | Sounds in the same non-zero mute group cut each other off (e.g., Open/Closed Hi-Hat). |
| `midiChannel`| String | "0" | MIDI channel (1-16) or "omni" (0). |
| `oneShot` | Boolean | true | If `true`, plays full sample ignoring note-off. If `false`, enters release phase on note-off. |
| `loop` | Boolean | false | If `true`, loops the sample between `loopStart` and `loopEnd`. Requires `oneShot="false"`. |
| `crossfade` | Float | 0.0 | Loop crossfade length in milliseconds. 0 gives a hard seam. Clamped per sound against the loop length and the attack region (see below). |
| `crossfadeShape` | String | "equalPower" | `equalPower` or `linear`. Equal power suits loosely correlated loop halves (room tails, ensemble); linear suits two nearly identical waveforms. |
| `releaseMode` | String | "loop" | `loop` keeps looping and lets the release envelope fade it out. `region` jumps to the sound's `releaseStart` and plays the tail once. |
| `attack` | Float | 0.001 | Attack time in seconds. |
| `decay` | Float | 0.0 | Decay time in seconds. |
| `sustain` | Float | 1.0 | Sustain level (0.0 to 1.0). |
| `release` | Float | 0.1 | Release time in seconds. |
| `detune` | Float | 0.0 | Pitch offset in semitones. |
| `groupLevel` | Float | 0.0 | Static gain offset for the entire group in dB. |
| `minVelocityGain` | Float | -40.0 | The gain in dB applied when MIDI velocity is at its minimum (1). This defines the floor of the velocity-to-gain scaling; 0.0 results in fixed volume regardless of velocity. |

### 5. Sounds
`<Sound>` elements define individual samples.

```xml
<Sound name="Snare_Hit" group="Snare" resource="snare.wav" 
       basePitch="60" noteLow="60" noteHigh="60" velLow="0" velHigh="127"
       sampleStart="0" sampleEnd="-1" loopStart="500" loopEnd="2000"/>
```

| Attribute | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | Required | Name of the sound. |
| `group` | String | - | Name of the parent `<SampleGroup>`. Inherits properties from the group. |
| `resource` | String | Required | Filename of the audio sample in Binary Data. |
| `basePitch` | Integer | 60 | MIDI note number where sample plays at original pitch. |
| `noteLow` | Integer | - | Lowest MIDI note that triggers this sound. |
| `noteHigh` | Integer | - | Highest MIDI note that triggers this sound. |
| `velLow` | Integer | 0 | Lowest velocity. |
| `velHigh` | Integer | 127 | Highest velocity. |
| `sampleStart`| Integer | 0 | Sample index to start playback. |
| `sampleEnd` | Integer | -1 | Sample index to stop playback, exclusive (-1 = end of file). |
| `loopStart` | Integer | -1 | Sample index where the loop begins (-1 = `sampleStart`). |
| `loopEnd` | Integer | -1 | Sample index where the loop ends, exclusive (-1 = `sampleEnd`). |
| `releaseStart` | Integer | -1 | Sample index where the release tail begins, used by `releaseMode="region"` (-1 = `loopEnd`). |

#### Playback regions

A sound is described by five ordered points:

```
sampleStart <= loopStart < loopEnd <= releaseStart <= sampleEnd

|<-- attack -->|<---- loop ---->|<------ release tail ------>|
sampleStart  loopStart       loopEnd/releaseStart        sampleEnd
```

*   **Attack** (`sampleStart` to `loopStart`) plays once when the note starts.
*   **Loop** (`loopStart` to `loopEnd`) repeats while the note is held.
*   **Release tail** (`releaseStart` to `sampleEnd`) plays once after note-off,
    when the group uses `releaseMode="region"`.

`loopEnd` and `sampleEnd` are exclusive: the last sample played is the one
before them. Points given out of order are clamped back into order on load and
the correction is logged, rather than being silently accepted.

A one-shot sound ignores the loop and release points entirely and plays
`sampleStart` to `sampleEnd`, which is what all the kits in this repository
currently do.

The crossfade runs *backwards* from `loopEnd`, blending in the material
immediately before `loopStart`, so it consumes part of the attack region. Each
voice clamps the requested length to the smaller of the loop length and the
distance from `sampleStart` to `loopStart`, so an over-long `crossfade` is
reduced rather than reading outside the sound.

With `releaseMode="region"`, note-off moves playback to `releaseStart` and
crossfades out of the loop, which keeps running underneath for the length of the
fade. The ADSR `release` is not used: the envelope holds at `sustain` and the
voice ends when the tail reaches `sampleEnd`, so the decay you hear is the
recorded one. A choke (mute group) still fades the voice out.

That jump fade reuses `crossfade`, but with a 5 ms floor and clamped to the
length of the tail. The floor is there because a loop seam can be authored away
by picking matching zero crossings, whereas the release jump leaves the loop at
whatever point the note-off happened to fall on, so `crossfade="0"` would click
on every release.

`loop`, `crossfade` and `releaseMode` are also per-group controls in the Sampler
tab and automatable parameters, with the value in `mapping.xml` as the default.
The panel greys out whatever the current mode makes inert: the loop controls
while a group is `oneShot`, the crossfade and release mode while it is not
looping, and the ADSR **Release** knob whenever a looping group is in `region`
mode, where the recorded tail does the decay instead.

`crossfadeShape` has no control: it is a property of the recorded material
rather than something to perform with, so it stays in the mapping.

## Resource Handling

A mapping refers to every file by its plain name (e.g. `my sample.wav`), and
`Source/ResourceProvider.h` turns that name into bytes. Two sources exist:

1.  **Embedded** — JUCE's `BinaryData`, which is what a compiled kit always
    uses. Names are matched three ways: the identifier `juce_add_binary_data`
    would have generated (spaces and dots become underscores, other punctuation
    is dropped, a leading digit gets an underscore in front), that identifier
    ignoring case, and finally the original file name ignoring case.
2.  **A folder on disk** — used by the dev host described under *Building*. The
    folder is indexed recursively, so `wav/`, `img/` and `presets/` are a
    convention rather than a requirement, and anything the folder does not hold
    falls back to the embedded set.

The point of matching real file names first is that the same `mapping.xml`
resolves identically either way: a kit that plays in the dev host plays the
same once compiled.

# Made with FxmeSampler

## Examplekits folder

In the ExampleKits folder, there are a few small-size projects to show how to use FxmeSampler. For now there are only drum sampler examples, but more will come in the future.

## Black Widow Drums
The **Black Widow Drums** project is the first published drum sampler showcase for FxmeSampler's advanced spatial and processing features. Recorded on a Gretsch Black Widow kit, it utilizes an original microphone array and signal processing chain to deliver a studio-ready drum sound.

![image info](./doc/BlackWidowPhoto.png)

### Microphone Configuration
*   **Overheads:** Rode NT-SF1 First-Order Ambisonic microphone, capturing a full 360° sound field.
*   **Kick:** Blue Kick Ball.
*   **Snare/Toms:** Three Shure SM57s (Snare, High Tom, Low Tom).

### Advanced Ambisonic-Mono Routing
Each drum element leverages the **ambisonicmono** hybrid strip. This routes the 4-channel B-Format overhead signal plus the dedicated proximity microphone into a single channel strip. This allows for:
*   **Spatial Sculpting:** Precision control over the elevation, azimuth, and width of the overhead "view" for each specific drum.
*   **The Virtual MS Mic:** The ability to treat the ambisonic field as a virtual Mid-Side pair that can be panned and balanced.
*   **Hybrid Mixing:** An equal-power mix control to blend between the localized spatial field and the punch of the close-mic proximity signal.

### Room Modeling
To optimize performance and reduce the plugin's memory footprint, the room sound is not played back from raw multi-channel files. Instead, a **transfer function** was calculated between the omnidirectional (W) component of the ambisonic overheads and the physical room microphones. 

This resulted in **Impulse Responses (IRs)**. The Room channel in the mixer functions as a real-time convolution engine, applying these IRs to the dry signals. This provides an authentic room character while saving gigabytes of sample data.

### Processing & Mix Architecture
*   **Channel Processing:** Every strip features a dedicated effect chain consisting of a **4-band EQ**, **Dynamics (Compressor/Limiter)**, and **Tube Saturation** for harmonic enhancement.
*   **Parallel Compression Bus:** A dedicated stereo bus for "New York style" parallel compression to add weight and density to the kit.
*   **Spatial FX Bus:** A secondary bus hosting a combined **Delay and Convolution Reverb** for additional depth and atmosphere.

![image info](./doc/BlackWidowGUI.png)
