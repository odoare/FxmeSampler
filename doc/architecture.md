# FxmeSampler architecture

Reference for the code structure, the invariants that are not visible from any
single file, and the decisions that would otherwise have to be rediscovered.
The mapping.xml format itself is specified in the README (Configuration:
mapping.xml) and is not repeated here.

## Repository and dependency chain

FxmeSampler is a suite, not a single plugin. One shared engine under Source/ is
compiled into one plugin per kit under Kits/, each with its own embedded
samples, presets, artwork and mapping.xml.

```
FxmeSampler/
  CMakeLists.txt            root: JUCE, FxmeTools, shared assets, kit selection
  Source/                   the shared engine (compiled into every kit)
  Kits/BlackWidow/          BlackWidowDrums  (PLUGIN_CODE BWDR)
    data/                   everything that ships: mapping.xml, wav, Presets, img
    art/                    working sources, never embedded
  Kits/Century/             CenturyDrums     (PLUGIN_CODE CTDR)
  FxmeFX/                   submodule: the effect components (EQ, comp, tube, ...)
    lib/FxmeTools/          submodule of FxmeFX: shared controls, DSP, WDL
      WDL/                  submodule of FxmeTools: convolution engine
  img/                      assets shared by all kits
  packaging/windows/        NSIS installer script
  MyKits/                   kit authoring area (gitignored)
  Tools/                    Python helpers for generating mapping.xml
  ../JUCE                   JUCE 8 as a SIBLING directory, not a submodule
```

The submodules nest three deep, so a plain clone is not enough:

```sh
git submodule update --init --recursive
```

FxmeTools arrives through FxmeFX rather than directly. That is deliberate: the
project already depends on FxmeFX for the effects, and a second independent
FxmeTools checkout would risk two different versions in one build.

The project moved off the older FxmeJuceTools module (which used to be installed
at ../JUCE/usermodules) in August 2026. The two cannot coexist: both define
fxme::FxmeSlider, fxme::FxmeButton, fxme::FxmeMeters and fxme::CracksGenerator,
and JuceHeader.h includes every linked module, so linking both is a redefinition
error. FxmeTools has no equivalent of FxmeJuceTools' TitleBar (its TopBar is the
full plugin header bar with logo and version, not a plain label), so TitleBar was
copied into Source/TitleBar.h as a plugin-local component.

## CMake structure

The root CMakeLists does four things, in this order, and the order matters:

1. Sets CMAKE_OSX_ARCHITECTURES and CMAKE_OSX_DEPLOYMENT_TARGET before
   project(). See the macOS section below.
2. Adds JUCE from the sibling directory.
3. Includes FxmeFX/lib/FxmeTools/cmake/FxmeTools.cmake, which registers the
   FxmeTools JUCE module once and defines fxmetools_attach().
4. Includes FxmeFX/Source/Common/CommonAssets.cmake, which registers
   FxmeCommonBinaryData (the shared FX-Mechanics logo). Every FxmeFX effect
   component includes Common/TopBar.h, which embeds that logo, so this target is
   required even though the kits never draw the bar.

Each kit CMakeLists then declares its plugin, its own BinaryData target, the
shared engine sources, the FxmeFX component sources, and finishes with
fxmetools_attach(<target>).

The BinaryData contents are globbed, not listed by hand: data/wav/*.wav,
data/<presets>/*.xml and data/img/*.{png,jpg}, each with CONFIGURE_DEPENDS so
the build re-scans those directories and reconfigures when they change. Only
mapping.xml and the few assets from outside the kit directory stay explicit.

The rule that follows from this is: everything in data/ ships. GIMP projects,
photo originals and unused takes belong in art/, which is never scanned. Each
kit prints its embedded counts at configure time ("BlackWidow: embedding 32
samples, 12 presets, 11 images"); those numbers are the tripwire for something
having landed in data/ by accident.

The hand-written lists this replaced had already drifted in both directions: a
finished preset that never shipped, and two images that had been deleted from
the working tree while the list still named them. That helper links the module and compiles the WDL
convolution engine (convoengine.cpp, fft.c, resample.cpp) from FxmeTools' own
WDL submodule, reached through <convoengine.h> by ConvolReverb.

Build one kit rather than both while iterating:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DKIT=BlackWidow
cmake --build build --config Release --target BlackWidowDrumsBinaryData --parallel 1
cmake --build build --config Release --target BlackWidowDrums_VST3 --parallel 2
```

Never build with -j$(nproc). Release links with LTO, and a full-core build of
this project exhausts RAM. The BinaryData targets are built first, serially,
because they are hundreds of megabytes of embedded WAV; both have
INTERPROCEDURAL_OPTIMIZATION OFF and -O0 for the same reason (there is nothing
to optimise in a byte array, and optimising one costs gigabytes).

Building does not install. Copy the .vst3 to the VST3 folder and make the DAW
rescan, since it caches the loaded module.

Two knobs sit beside KIT. KIT accepts None, which builds no kit at all, and
BUILD_DEV_HOST (default OFF) adds Dev/, the standalone kit loader described
under "Loading a kit at runtime". They are separate because the release
workflow builds this whole project: anything on by default is compiled into
every release. Dev/CMakeLists.txt mirrors a kit's, minus the plugin wrapper,
and spells out the JucePlugin_ macros by hand — the shared engine reads a
handful of them and juce_add_gui_app defines none.

```sh
cmake -B build-dev -DCMAKE_BUILD_TYPE=Release -DKIT=None -DBUILD_DEV_HOST=ON
cmake --build build-dev --parallel 8
```

## Runtime structure

```
FxmeSamplerAudioProcessor
  ├─ Sampler   sounds, sample groups, 64 voices, renders to an N-channel buffer
  └─ Mixer     strips consuming those channels, a stereo mix bus, master, buses
```

The processor exposes four stereo output buses (Main, Aux 1, Aux 2, Aux 3).

Everything is driven by mapping.xml, which the processor asks its
fxsampler::ResourceProvider for by name — BinaryData for a compiled kit, a
folder on disk for the dev host. It is read three times, for three different
purposes:

1. In createParameterLayout, on throwaway Sampler and Mixer instances, purely to
   enumerate the parameters. The APVTS layout has to exist before the real
   objects do, which is why the temporaries exist.
2. In the processor constructor, to build the real Sampler and Mixer.
3. Immediately after, assignParameters caches a std::atomic<float>* for every
   parameter onto the object that uses it.

That third step is what keeps the audio thread cheap: parameters are read
through cached atomic pointers, never looked up by string in processBlock.
getRawParameterValue appears only in the assignParameters functions.

Per block, processBlock does: clear the output buffer, resize the sampler buffer
if the host changed block size (with avoidReallocating), read the playhead BPM
and push it to the mixer only when it changed, then sampler.updateParams(),
sampler.processBlock() into samplerOutputBuffer, and mixer.processBlock() from
that buffer into the output.

Mixer::processBlock walks the strips in order, handing each one a consecutive
slice of the sampler's channels (currentInputChannel advances by the strip's
getNumInputChannels()). Strips sum into the stereo mixBuffer and/or directly
into the output buses; the master strip then processes mixBuffer into the
output. Solo is global: if any strip is soloed, only soloed strips process.

Strip types, selected by the type attribute in mapping.xml: ambisonic,
ambisonicmono, stereo, ms, mono, stereoreverb, reverb, plus MasterStrip and
BusStrip which are created implicitly. Each strip optionally owns an effect
chain, selected by the effectChain attribute: Dynamics (EQ, compressor, tube,
transient), Reverb (convolution reverb, EQ, delay), Delay, or None.

The effect components themselves come from FxmeFX, so the mixer strips get the
same EQ, compressor, tube and convolution reverb that ship as standalone
plugins.

The ambisonic strips decode through fxme::AmbixToStereo (FxmeTools), a virtual
mid/side pair: a cardioid pointed at the strip's azimuth and elevation for the
mid, a horizontal figure-of-eight a quarter turn from it for the side, matrixed
as L = M + width * S. It reads only W, Y, Z and X, and adds into the
destination rather than overwriting, so several strips can sum into one bus.
Source/AmbixToMS.h re-exports it under the old name.

### Where resources come from

Source/ResourceProvider.h owns every name-to-bytes lookup: mapping.xml, the
samples, the artwork. EmbeddedResources reads BinaryData and is the default
argument everywhere, so a kit target needs no knowledge of it. FolderResources
reads a directory, indexed recursively at construction and cached on first
read, falling back to the embedded set for anything the folder lacks.

Two things stayed outside it. Impulse responses are handed to ConvolReverb as
resource *names* and it resolves them from BinaryData itself, inside the FxmeFX
submodule — so a folder-loaded kit gets the IRs the host binary embeds, not its
own. And factory presets still come from BinaryData; a folder-loaded kit gets
its presets/ directory as the *user* preset folder instead, which is better for
authoring anyway since saving puts them where they will be embedded from.

Lifetime is the one trap: Sound::data points into the provider, so the provider
must outlive every Sampler built from it. Dev/Main.cpp declares it before the
processor for exactly that reason.

### Loading a kit at runtime

Dev/Main.cpp (target FxmeSamplerDev, `-DBUILD_DEV_HOST=ON`) is a JUCE
application, not a plugin, and the distinction is forced rather than stylistic:
a mapping determines how many parameters exist, and a host reads the parameter
list as soon as the constructor returns, so the kit has to be chosen before the
processor exists. An application can do that; a plugin cannot, because the DAW
constructs the processor.

Reloading therefore replaces the whole processor rather than reloading the
Sampler — which is also why the threading warning below does not apply to it.
AudioProcessorPlayer is detached first, so no audio thread is running while the
old processor, its editor and its provider are destroyed and new ones built.

## Playback regions and loop crossfade

This section is the reference implementation in prose. The Python authoring
tool renders its loop preview offline and has to agree with the engine sample
for sample, so if the two ever disagree, this is the arbiter.

A sound carries five ordered points, all indices into the source file:

```
sampleStart <= loopStart < loopEnd <= releaseStart <= sampleEnd
```

loopEnd and sampleEnd are exclusive, matching the convention sampleEnd already
used. The points are clamped into order when the mapping loads, and any
correction is logged rather than accepted silently. Unset points (-1) default
inwards, not to zero: loopStart to sampleStart, loopEnd to sampleEnd,
releaseStart to loopEnd. Defaulting loopStart to 0 would be wrong here, because
a sound is a slice of a longer take, so 0 is somewhere in an earlier stroke.

The loop crossfade runs backwards from loopEnd. With loopLen = loopEnd -
loopStart and a crossfade of X samples, for a read position p:

```
while p in [loopEnd - X, loopEnd):
    g   = (p - (loopEnd - X)) / X
    out = a(g) * read(p) + b(g) * read(p - loopLen)

on p >= loopEnd:  p -= loopLen
```

The second read head is simply p - loopLen. As p sweeps loopEnd-X to loopEnd it
sweeps loopStart-X to loopStart, which is exactly the material the loop is
about to jump into, so no second accumulator is needed and the seam is
continuous by construction: at g=0 the output is the plain read, and at g=1 it
is read(loopStart), which is what the sample after the wrap will be.

Two constraints follow, and each voice clamps X against them at note start
rather than trusting the mapping:

  X < loopLen                    the fade cannot be longer than the loop
  loopStart - X >= sampleStart   the fade reads backwards into the attack

Gain laws, with g running 0 to 1:

  equal power:  a = cos(g * pi/2),  b = sin(g * pi/2)
  linear:       a = 1 - g,          b = g

Equal power is the default. It holds summed power constant, which suits loop
halves that are only loosely correlated (room tails, ensemble, noise). Linear
holds summed amplitude constant and is better when the two sides are nearly the
same waveform, where equal power would bulge by 3 dB in the middle.

### The release region

With releaseMode="region", note-off does not run the ADSR release over the loop.
Instead the main head jumps to releaseStart and plays the tail once, and the
loop it left carries on under a second head so it can be faded out rather than
cut. The envelope holds at its sustain level throughout and the voice ends when
the tail reaches sampleEnd, so what is heard is the instrument's own decay. A
choke still fades the voice out, tail and all.

This is a genuinely different mechanism from the loop crossfade, not a reuse of
it. On note-off from an arbitrary position there is no algebraic relationship
between the two sides to exploit, so the outgoing head needs its own
accumulator. Both heads advance by the same increment, so the fade holds at any
pitch. Progress is read off the main head rather than counted down:

```
R = releaseFade, frozen at note-off
while p - releaseStart < R:
    g   = (p - releaseStart) / R
    out = b(g) * read(p) + a(g) * body(loopHead)
```

with the same a and b the group uses for its seam, so a group fades the same way
wherever it fades. R is max(5 ms, crossfade), clamped to sampleEnd -
releaseStart. The floor is the difference between the two fades: a loop seam can
be authored away by choosing matching zero crossings, so crossfade="0" is a
legitimate choice there, but the release jump leaves the loop at whatever phase
the note-off fell on and always steps.

body() above is the whole loop mechanism, seam crossfade included, not a plain
read. The outgoing head is still inside the loop and can still reach loopEnd
during the fade; giving it a bare seam puts back exactly the click the release
fade exists to remove, and measurably so: at a 50 ms fade the worst step across
the note-off is 4x the waveform's own steepest step with a bare seam and 1.0x
with this. It costs a third read only while a release fade and a seam fade
overlap, which is a few milliseconds per note-off.

Nothing takes the release path unless the sound loops, the group asks for
releaseMode="region", and releaseStart < sampleEnd. Without a tail to jump to it
falls back to fading the loop out, which is at least audible.

## Threading contract

There are no locks anywhere in Source/, and this is deliberate rather than an
oversight. The invariant that makes it safe:

Every structural mutation (Sampler::loadSamplesFromXml, Mixer::loadFromXml,
assignParameters) runs in the processor constructor, before the host has the
processor. The editor only reads (getStrips, getSampleGroups, getMasterStrip).
Sampler::addSound has no callers at all. prepare() runs from prepareToPlay,
which no host overlaps with processBlock, and which JUCE's own
AudioProcessorPlayer locks around in the standalone build. Because mapping.xml
is compiled into BinaryData, a kit cannot change at runtime.

The audio callbacks previously took a juce::CriticalSection that no other thread
could contend for. It was removed in August 2026 along with the members
themselves, and the contract above is documented on both class declarations.

If a kit ever needs reloading at runtime, do not reintroduce a lock around
processBlock. Publish the new sounds and strips with an atomic pointer swap, or
suspend processing from the message thread, so the audio thread never blocks.

Allocation on the audio thread: every buffer is sized to the worst case in
prepare(), and every process() that resizes passes avoidReallocating so a
smaller host block only changes the reported size. This applies to
samplerOutputBuffer in the processor, mixBuffer in the Mixer, and tempBuffer and
busBuffer in every MixerStrip. Any new buffer must follow the same pattern.

Nothing in the audio path prints. std::cout in a note-on handler was removed in
August 2026; loader diagnostics use DBG, which compiles out of Release.

## State and presets

The saved state is the raw APVTS XML, written with MemoryOutputStream and read
back with XmlDocument::parse. It is not copyXmlToBinary/getXmlFromBinary.

This matters more than it looks. setCurrentProgram feeds embedded factory preset
XML straight into setStateInformation, so both paths share the format. Switching
to the copyXmlToBinary pair would silently fail to load every existing session
and every factory preset, because getXmlFromBinary expects a magic header that
plain XML does not have. If that migration ever happens, the read path has to
accept both forms.

getStateInformation writes version="1" on the state root. setStateInformation
reads it with a default of 0, which covers both sessions saved before versioning
and the factory presets (plain APVTS dumps with no attribute). Nothing migrates
yet; the branch point is marked in the code.

Presets are handled by fxme::PresetManager, owned by the processor and shared
with the editor. It must be declared after apvts, since members are constructed
in declaration order and the manager takes a reference to the state.

Factory presets are the XML files under each kit's preset directory, embedded in
BinaryData. The manager keeps every embedded *_xml resource whose root tag
matches the APVTS state type, which is Parameters, so mapping_xml (root
Mapping) is skipped without needing to be special-cased.

User presets are files under

    <user app data>/FxmeSampler/<plugin name>/Presets

so the two kits share a suite folder but keep separate banks, which they must:
their parameter sets are entirely different and a Century preset is meaningless
in Black Widow.

The host's program menu (getNumPrograms and friends) is a view onto the factory
bank rather than a second list, so the host menu and the editor's preset browser
always agree on what is loaded. User presets are deliberately not exposed there,
because the program list has to be fixed in size while the user bank changes at
runtime. getCurrentProgram reports 0 when the manager returns -1, which happens
when the loaded state is a user preset or an unsaved edit. changeProgramName is
a no-op: factory presets live in the binary, and users rename their own presets
in the browser.

Both banks use the same file format as the session state (the APVTS state XML),
which is why the raw-XML decision above matters to presets too.

The manager offers onBeforeSave, onBeforeLoad and onAfterLoad hooks for
processors that keep state outside apvts.state. This one does not, so they are
unused. If per-installation state (say a sample path) is ever added, that is
where it has to be protected from being overwritten by a preset load.

### Authoring a factory preset

Factory presets are tuned in the plugin and then promoted by hand. The Welcome
tab used to carry Save and Load buttons for this, writing loose XML through a
FileChooser; they were removed once the preset manager arrived, and the
equivalent workflow is:

1. Tune the kit in the plugin, then Save As in the preset browser. The file
   lands in <user app data>/FxmeSampler/<plugin name>/Presets.
2. Copy it into the kit's preset directory (Kits/BlackWidow/data/Presets or
   Kits/Century/Data/presets).
3. Rebuild the kit's BinaryData target. The directory is globbed, so there is
   no list to edit; the preset count printed at configure time should go up by
   one.

Two details make this work without any editing of the file. The saved XML
carries the free-text display name in a presetName attribute, and
buildFactoryList prefers that attribute over the name derived from the
BinaryData symbol, so spaces and punctuation survive even though the file name
was legalised. The file also carries presetIsFactory="0" from having been saved
as a user preset, which is harmless: applyStateXml overwrites both properties
from the bank the preset was actually loaded from.

An earlier build also wrote a copy of the state to ~/Documents/samplerdata.xml
on every host save. That debug leftover was removed in August 2026.

## GUI

The editor is thin: FxmeSamplerAudioProcessorEditor owns a MixerComponent, which
builds the tabs: Welcome, Levels (one strip component per mixer strip), one tab
per effect chain, and Sampler. The Welcome tab pairs the kit artwork and blurb
from mapping.xml on the left with an fxme::PresetComponent on the right, sharing
the processor's manager. Its split is computed in one place (computeAreas) so
paint() and resized() cannot drift apart. Controls are fxme::FxmeSlider throughout (never a bare juce::Slider
with a TextBox and a separate Label).

The FxmeFX effect components are embedded directly in the effect tabs. They were
written for a roughly square slot, so TubeComponent takes an optional
knobsInSingleRow constructor flag (default false) that lays its four knobs in one
row and tightens the header margins, for the wide but short slot an effect tab
gives it. EffectChainDynamicsComponent passes true. The same treatment is worth
considering for CompressorComponent and TransientComponent, which sit in the same
column with the same geometry.

Source/Theme.h (namespace fxsampler::theme) holds the palette, the geometry
ratios and two helpers: accentSlider, which applies one accent colour to a
slider, and paintPanelBackground, which draws the house diagonal gradient. That
gradient had been copy-pasted into four paint() methods with two different
darkening depths, hence the darkenSteps argument. Tune the look there rather
than with literals in components.

The editor owns the single fxme::FxmeLookAndFeel and sets it with
setLookAndFeel, clearing it in its destructor. JUCE resolves a component's
look-and-feel by walking up its parents, so every child inherits it without
setting its own. It is declared before the components it serves, because members
are destroyed in reverse declaration order and the look-and-feel has to outlive
them. Previously each strip component and each sample group owned an instance,
declared after the sliders pointing at it, which inverted that order.

The editor also owns an fxme::InfoButton (parked over the spare space at the
right end of the tab bar) and the single fxme::TextEntryFocusFixer, declared
after the child components. The fixer covers TextEditors under the editor, which
in practice means FxmeSlider's right-click value entry; it deliberately steps
aside for modal dialogs, so the preset browser's naming AlertWindow is not its
responsibility.

## macOS and CI

The release workflow builds Linux, Windows and macOS universal, and publishes
only on a version tag. It can be exercised by hand with workflow_dispatch.

Two macOS rules that have already caused a broken release elsewhere:

The if(APPLE) block setting CMAKE_OSX_ARCHITECTURES and
CMAKE_OSX_DEPLOYMENT_TARGET must sit before project(). That is where CMake
configures the Apple toolchain and reads them; set afterwards, the deployment
target is ignored and the architectures are unreliable. On GitHub's Apple
Silicon runners the result is an arm64-only bundle that is named universal,
installs happily, and fails on every Intel Mac. The workflow also passes both as
-D on the configure line, because a command-line -D lands in the cache before any
CMakeLists line runs.

The deployment target is 10.13, not 11.0. 11.0 excludes Catalina even with a
correct x86_64 slice, and the plugin is simply skipped during the scan. The
arm64 slice is clamped up by the toolchain anyway, so a low target costs nothing.

The verify step checks all six macOS bundles (VST3, AU and Standalone for both
kits) for both slices and exits non-zero when one is missing. A verify step that
only prints lipo -info is how an arm64-only release ships unnoticed.

The plugins are not signed or notarised. CI applies an ad-hoc signature, which
only makes the bundles internally consistent and does nothing for Gatekeeper.
The reason a downloaded plugin never appears in a DAW is the quarantine
attribute, and the fix is the xattr command documented in the README under
Installing, macOS.

## Conventions

Line endings under Source/ are mixed per file (24 CRLF, 15 LF at the time of
writing), with no .gitattributes normalising them. A script or editor that
rewrites a whole file must preserve whatever that file already uses, or the diff
becomes the entire file. Adding a .gitattributes to settle this would be worth
doing at some point.

Changes often span the submodule boundary. Commit FxmeFX first, then the bumped
pointer plus the project changes in the parent. FxmeTools is shared by roughly a
dozen projects, so keep its public API backward-compatible and additive, and
preserve APVTS parameter IDs so existing presets and sessions still load.

## Known gaps

Ordered as a plan; the first two are done.

1. Done (August 2026). Removed the Documents file write, the audio-thread
   prints, and added state versioning.
2. Done (August 2026). Removed the audio-thread locks and the last reallocating
   setSize in the audio path.
3. Done (August 2026). fxme::PresetManager, the browser in the Welcome tab, and
   the host program menu as a view onto the factory bank. This also retired the
   Welcome tab's old Save/Load buttons, which wrote loose XML through a
   FileChooser and called apvts.replaceState directly, bypassing the manager.
   Still optional: an fxme::PresetBarComponent parked in the tab bar's spare
   space, so the current preset name and dirty marker stay visible from the
   other tabs.
4. Done (August 2026). Source/Theme.h, one FxmeLookAndFeel owned by the editor,
   an InfoButton and the TextEntryFocusFixer. The FxmeFX effect components keep
   their own look-and-feel instances: they are shared with the standalone
   plugins and are not this project's to change.
5. Done (August 2026). AmbixToMS moved to FxmeTools as fxme::AmbixToStereo
   (dsp/AmbixToStereo.h), rebuilt on dsp/Ambisonics.h. Source/AmbixToMS.h is now
   a re-export so the local type name still works; Source/AmbixToMS.cpp is gone
   and both kit CMakeLists dropped it.
6. Done (August 2026). Everything is named FxmeSampler: the CMake project, the
   workspace file and the CI checkout path. .gitignore was rewritten (the stale
   per-artifact paths became wildcards, __pycache__ was added, and the tracked
   workspace file is an explicit exception to the *.code-workspace rule). The
   orphaned Presets/*.xml were deleted, along with the Projucer-era Builds/ and
   the in-source JUCE/ and CMakeFiles/ residue. The unused images under img/
   were kept deliberately: they are plausibly the authoring originals for the
   per-kit copies under Kits/*/data/img/.

Remaining, not yet done:

7. A .gitattributes to settle the mixed line endings. Deferred because
   `* text=auto` renormalises files as they are next committed, which spreads a
   whole-file diff across unrelated commits. Worth doing as one deliberate
   commit that touches every source file at once, not as a side effect.
8. Optionally an fxme::PresetBarComponent in the tab bar, so the current preset
   name and dirty marker stay visible from the tabs other than Welcome.
