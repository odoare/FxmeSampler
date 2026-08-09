# Mapping tools

Python helpers that turn a folder of multi-stroke take recordings into the
`<Sound>` entries of a kit's `mapping.xml`. They find where each stroke starts,
decide which stroke belongs in which velocity layer, let you correct anything
they got wrong, and write the result back without disturbing the rest of the
file.

    pip install numpy scipy matplotlib

Everything here operates on one kit's wav folder at a time, for example
`Kits/BlackWidow/data/wav`.

## The three scripts

| file | what it is |
| --- | --- |
| `mapping_lib.py` | the library: reading wavs, onset detection, take linking, the state file, the mapping.xml rewriter. No UI. |
| `mapping_build.py` | batch driver: report, QC images, export, round-trip check. |
| `MappingEditor.py` | interactive review and correction. |

`MStoLR.py` is unrelated to mapping. It converts a mid/side stereo wav to
left/right.

## Typical session

```sh
# 1. See what detection makes of the kit.
python3 Tools/mapping_build.py Kits/BlackWidow/data/wav --report

# 2. Optionally render one QC image per file and flick through them.
python3 Tools/mapping_build.py Kits/BlackWidow/data/wav --qc /tmp/qc

# 3. Fix whatever needs fixing, press s to save.
python3 Tools/MappingEditor.py Kits/BlackWidow/data/wav

# 4. Write mapping.xml.
python3 Tools/mapping_build.py Kits/BlackWidow/data/wav --export newmapping.xml
```

Export writes wherever you point it. Overwriting `data/mapping.xml` is a
deliberate act, so aim somewhere else first and diff.

## Filename convention

Detection is driven by the file name, which must be

    <NumStrokes> <Group> <Family> <MuteGroup> <BasePitch> <NoteLow> <NoteHigh>.wav

for example `16 AmbixTomH AmbixTomH 0 47 47 47.wav`. Files that do not parse are
skipped silently, which is how the room impulse responses in the same folder
stay out of the way.

The stroke count matters more than it looks. Because the file states how many
strokes it contains, the detector can take the N strongest onset candidates
instead of hunting for a threshold that yields the right number.

## How detection works

**Onsets come from spectral flux**, the half-wave-rectified frame-to-frame
increase in log magnitude. Flux responds to increases in spectral energy, so a
cymbal decaying for twenty seconds contributes nothing and a stroke landing on
a ringing tail is still found. The previous generation of this tool used an
absolute RMS gate that required the signal to fall below a fixed threshold and
stay quiet for several hundred milliseconds, which is why cymbals, hi-hats and
toms failed on it.

**Short lead-in is handled, and it is a property of the analysis, not of any
one kit.** Spectral flux compares each analysis frame with the one before it,
so an onset falling inside the very first frame has nothing to be contrasted
against and produces almost no flux. Detection therefore tests whether the
first FFT window (1024 samples, 23 ms at 44.1 kHz) already contains signal,
comparing against the file's own noise floor rather than its peak, since the
first stroke of a crescendo can be 40 dB below the last.

If it does, the first stroke is pinned to sample 0 and the remaining N-1 are
searched for. If it does not, all N are detected normally. The behaviour
across lead-in lengths, measured:

| lead-in before the first stroke | result |
| --- | --- |
| none (take begins mid-stroke, as BlackWidow does) | first stroke at 0, rest detected |
| under one window (< 23 ms) | first stroke pinned to 0, absorbing the lead-in |
| 50 ms and above | every stroke detected, within about 35 samples |

So a kit recorded with generous silence in front works without any adjustment.
The only cost is that a lead-in shorter than one window gets swallowed into the
first sample range, which is a negligible amount of room tone, and it fails
safe: the alternative is missing that stroke and silently promoting some other
peak into its place.

**Onsets are then refined to the attack foot** by walking back from the attack
peak to the last sample still at the pre-stroke level. Two traps are worth
remembering, because both produced plausible-looking but wrong results during
development:

- The envelope must be the analytic-signal magnitude, not the rectified
  waveform. A 100 Hz tom passes through zero twice per cycle and those dips are
  deep enough to be mistaken for the foot.
- It must be a backward search for the last quiet sample, not a minimum. Ahead
  of a clean stroke the signal is digital silence, and the minimum of a flat run
  is its first sample, which drags the onset to the edge of the search window.

Two more traps live in the peak picking, both of which lose the *first* stroke
of a file while leaving the stroke count correct (N peaks are taken regardless,
so a spurious one elsewhere quietly takes its place):

- The running-median baseline must be padded with the flux's own median, not
  with its edge value. Edge replication copies the first frame across half the
  window, so a stroke near the start becomes its own baseline and subtracts to
  zero.
- `scipy.signal.find_peaks` requires a strictly lower neighbour on both sides
  and so can never return the first or last frame. The detection curve is
  padded before peak picking to make both ends ordinary interior points.

For reference, the detector these replaced placed every start a systematic 101
samples (2.3 ms) early, from smearing the envelope with a centred window and
then subtracting a further millisecond on purpose.

## Velocity layers

Strokes are ranked by RMS over a **fixed window** from the onset (200 ms by
default), not by summing to the next onset. Fixed length is the point: it makes
strokes comparable, and it stops the last stroke of a file being rated over
whatever happened to be left.

The softest stroke becomes velocity layer 0. The 0 to 127 range is split evenly
across the strokes present.

## Takes and linked files

The Ambix and Prox files of one instrument are the same performance through two
microphone sets. They are layered on the same MIDI note, so a velocity layer
must mean the same physical stroke in both. Detecting them independently does
not guarantee that, and in the mapping these tools replaced it frequently did
not hold: whole halves of some instruments' velocity ranges played the close mic
of one stroke against the room mic of another.

Files are grouped into a **take** by stroke count, note range and frame count
(two mic sets cut from one multitrack are frame-identical). The grouping is then
**verified against the audio**: a genuine take shows a constant per-stroke
offset between its files, because the drum and the mics do not move, so the
flight time between them is the same for every stroke. What is checked is how
far the worst stroke departs from the take's median offset.

The **reference** (the close mic, chosen as the file with fewest channels)
decides the velocity ordering for the whole take. Members inherit it stroke by
stroke. An unverified take falls back to per-file ranking, on the grounds that a
wrong shared ordering is worse than the disagreement it replaces.

One subtlety in the arithmetic: the stroke that every file places at sample 0
has an offset of 0 by construction, not by physics. It is excluded from the
offset estimate, otherwise it drags the median towards zero and inflates any
measure of spread by the whole propagation delay.

## `--align-members`

Off by default. When on, each verified take's members start on the exact samples
its reference uses, rather than on their own attack feet.

This is an audible choice, not a correctness fix. The files come from one
multitrack, so sample N is the same instant in each. Equal starts reproduce the
recording's natural arrival delay between the close mic and the distant array
(2 to 3.6 ms on this kit, depending on the drum). Per-file attack feet make both
attacks land together at trigger time and discard that delay, which pulls the
ambisonic layer forward and changes the comb filtering between the mic sets.

Hand-locked strokes are never moved by it. It is a detection parameter, so it is
saved in the state file and reused by `--redetect`.

## The state file

`data/mapping_state.json` beside `mapping.xml` holds the hit lists, the lock
flags and the parameters that produced them. It is the authoring record and is
meant to be committed.

Anything you move or add in the editor is **locked**. A locked stroke survives
re-detection: each one consumes the detector's nearest version of the same
stroke, so moving a marker moves it rather than leaving a duplicate behind, and
a locked stroke with nothing near it is treated as one you added.

The state wins whenever it exists. `--redetect` is needed to analyse the audio
again, precisely so that the command you reach for after an editing session
cannot quietly re-derive your work.

## What export does, and does not do

`--export` rewrites **only the runs of `<Sound>` lines** and copies every other
byte across untouched. The mapping files are hand-maintained (multi-line `<Bus>`
elements, chosen blank lines, the odd trailing space) and re-serialising a
parsed DOM would reformat all of it and make a real change impossible to see in
a diff. Attribute order, indentation and any attribute the tool does not know
about survive.

"Template" is not a header section. There is no point in the file where copying
stops and generating begins. The writer walks the whole file line by line and
replaces **runs of consecutive `<Sound>` lines that share a `resource`**,
leaving everything else exactly where it was. So the `<SampleGroup>` lines
sitting between those runs, the blank lines separating them, and anything you
add between them are all preserved in place, not just the header.

Within a run, the **first existing line is used as the pattern** for the new
ones. Only `velLow`, `velHigh`, `sampleStart` and `sampleEnd` are substituted,
so attribute order, spacing and any attribute the tool has never heard of are
inherited by every regenerated layer. A run grows or shrinks freely when the
stroke count changes.

Two consequences: a resource with no `<Sound>` run in the file is reported and
skipped rather than invented, and a run whose resource has no state is left
untouched.

### When there is no mapping.xml yet

Export generates a scaffold instead of refusing. The parts that follow from the
file names are filled in for real: one `<SampleGroup>` per group with sequential
channel assignments, a `<Strip>` per group typed from its channel count (1 mono,
2 stereo, 4 ambisonic), a `<Master>` with the total channel count, and all the
`<Sound>` lines. Everything else is a placeholder marked `TODO`, because no wav
file implies artwork, colours, which strips share a bus, or reverb sends.

Treat it as a starting point to edit, not an output. Once it exists, every later
export preserves whatever you did to it.

`--verify` proves the writer is faithful by seeding the state from `mapping.xml`
and writing it straight back out. It should report byte-identical. That is what
makes a later diff meaningful: anything that moved, moved because a stroke
moved.

## Editor keys

```
n / p        next / previous file        right / left  next / previous stroke
. / ,        nudge 1 ms later / earlier  > / <         nudge 10 ms
click        top pane: select nearest    zoom pane: move the selected start
a            add a stroke at the last click
d  r  u      delete / re-snap / unlock the selected stroke
m / M        put the selected stroke / every stroke on the exact sample used
             by this take's reference file
f            toggle the full-file pane   s  save state   q  quit

markers: red detected, orange dashed locked, blue selected,
         green dotted where the reference says the stroke is
```

`r` is the one to know: click roughly where the stroke is and press `r`, and it
snaps to the nearest attack foot using the same refinement the detector uses.

Run the editor with **no folder argument** and it asks for one in a dialog. On
quit it offers a second dialog: save the state, write `mapping.xml`, or neither.
Writing the mapping is off by default and names its destination, since
overwriting a kit's mapping should be a choice rather than a side effect. If
the destination does not exist yet, the scaffold above is generated into it.

Both dialogs need tkinter and a display. Without either, the editor behaves
exactly as it always did and the folder must be given on the command line.

The header states the take relationship and re-verifies the link after every
edit. Moving a start far enough that its file stops corresponding to the
reference turns the header red and names the offending stroke, because a broken
correspondence would otherwise silently map the wrong stroke to the wrong layer.

## Tests

    python3 Tools/mapping_build.py --selftest     # detection, 43 checks
    python3 Tools/MappingEditor.py <folder> --selftest   # editing, 38 checks

The detection self-test needs no audio: it synthesises takes of decaying noise
bursts at known positions, across a range of lead-in lengths and stroke counts,
and checks the count, the onset positions to within 4 ms, and that the velocity
ladder follows the synthesised crescendo. It exists because the lead-in bugs
above were silent, producing a correct stroke count from the wrong strokes.

The editor self-test drives the whole edit state machine headlessly. It covers
the logic but not the mouse.

`mapping_build.py <folder> --verify` is the third check and is described above.

## Things that will bite

- **Everything in `data/` is embedded in the plugin.** The kit CMakeLists globs
  that directory wholesale. Never write QC images or working files there;
  `--qc` refuses a destination under a `data/` folder for this reason.
- **Century is not a candidate for these tools.** Its samples are already cut
  into individual files, so there is nothing to detect.
- The QC overlay panel is the useful one. It stacks every stroke aligned on its
  own detected onset, so a misplaced start is the one curve that does not rise
  with the others.
