"""
Interactive review and correction of detected strokes.

    python3 Tools/MappingEditor.py Kits/BlackWidow_test/data/wav

Opens one take file at a time: the whole file as a dB envelope on top, a zoom
around the selected stroke below. Anything you move or add is locked, so
re-running detection later leaves your edit alone.

    n / p        next / previous file
    right / left next / previous stroke
    . / ,        nudge the selected start later / earlier (1 ms)
    > / <        nudge by 10 ms
    + / -        nudge by one sample
    click        in the top pane, select the nearest stroke
                 in the zoom pane, move the selected start there
    a            add a stroke at the last click
    d            delete the selected stroke
    r            re-snap the selected start to the nearest attack foot
    u            unlock the selected stroke, letting detection own it again
    m            move the selected stroke to the exact sample the take's
                 reference (master) file uses for it
    M            do that for every stroke in the file at once
    f            toggle full-file / zoom-only layout
    s            save the state file
    q            quit

Press L for loop mode, where the same keys author loop points instead:

    1 / 2 / 3    select loopStart / loopEnd / releaseStart to move
    . , > <      nudge the selected point by 1 ms / 10 ms
    + / -        nudge it by one sample
    click        top pane: put the selected point there (coarse)
                 seam pane: left of the wrap moves loopEnd, right of it moves
                 loopStart, both meaning "put the seam here" (about one
                 sample per pixel)
    z            snap it to the nearest rising zero crossing
    c            cycle the preview crossfade (0, 5, 20, 50, 100 ms)
    e            toggle the preview release: envelope or release region
    space        play the stroke: attack, loops, then the release
    x            create loop points on this stroke, or clear them
    m / M        copy loop points from the take's reference file

The lower pane becomes a seam view: what the loop actually plays across the
wrap, against the plain concatenation and against what would have followed had
it not wrapped. The two diverging at 0 is the discontinuity, and the crossfade
closing that gap is the fix.

Editing changes which stroke sits in which velocity layer, so the layer numbers
update as you work. For a file that belongs to a linked take, the ordering is
inherited from the take's reference file and the panel says so.

Writing mapping.xml stays a separate, explicit step:

    python3 Tools/mapping_build.py <folder> --export <dest>

    Author: Olivier Doare, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
"""

import os
import sys
import argparse

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mapping_lib as ml


ZOOM_MS = 25.0
ENVELOPE_BLOCK_MS = 2.0

# Widths of the seam view, in samples either side of the wrap.
SEAM_SPAN = 400

# Cycled by c. 0 is a hard seam, which is worth hearing at least once.
CROSSFADE_STEPS = (0.0, 5.0, 20.0, 50.0, 100.0)

# Zero-crossing snap window.
SNAP_MS = 5.0

KEYS = """
  n / p        next / previous file        right / left  next / previous stroke
  . / ,        nudge 1 ms later / earlier  > / <         nudge 10 ms
  + / -        nudge one sample
  click        top pane: select nearest    zoom pane: move the selected start
  a            add a stroke at the last click
  d  r  u      delete / re-snap / unlock the selected stroke
  m / M        put the selected stroke / every stroke on the exact sample used
               by this take's reference (master) file
  f            toggle the full-file pane   s  save state   q  quit

  L            loop mode:
    1 / 2 / 3  select loopStart / loopEnd / releaseStart
    . , > <    nudge it 1 / 10 ms          + / -  nudge it one sample
    click      stroke pane: put it there (coarse)
               seam pane: left of the wrap moves loopEnd, right moves
               loopStart, both "put the seam here" (~1 sample per pixel)
    z          snap to the nearest rising zero crossing
    c          cycle the preview crossfade       e  envelope / region release
    space      play it                           x  create / clear the loop
    m / M      copy loop points from this take's reference file

  markers: red detected, orange dashed locked, blue selected,
           green dotted where the reference says the stroke is
  loop mode: green loopStart, red loopEnd, violet releaseStart
"""

LOOP_POINTS = ("loop_start", "loop_end", "release_start")
LOOP_LABELS = {"loop_start": "loopStart", "loop_end": "loopEnd",
               "release_start": "releaseStart"}
LOOP_COLOURS = {"loop_start": "mediumseagreen", "loop_end": "crimson",
                "release_start": "darkviolet"}


class Editor:
    def __init__(self, folder, entries, takes, params, state_path):
        self.folder = folder
        self.entries = sorted(entries, key=lambda e: e.filename)
        self.takes = takes
        self.p = params
        self.state_path = state_path

        self.file_index = 0
        self.hit_index = 0
        self.dirty = False
        self.last_click = None
        self.show_full = True
        self.message = ""
        self.link_notes = []

        # Loop mode. The crossfade and release settings are listening aids, not
        # authoring data: crossfade and releaseMode are SampleGroup attributes,
        # and this tool only ever writes <Sound> lines. They are not saved.
        self.loop_mode = False
        self.loop_point = "loop_start"
        self.crossfade_index = 2          # 20 ms
        self.release_mode = "loop"

        self.mono = None
        self.rate = 44100
        self.env_t = None
        self.env_db = None

        self.fig = None
        self.ax_full = None
        self.ax_zoom = None

    # ── data ───────────────────────────────────────────────────────────────

    @property
    def entry(self):
        return self.entries[self.file_index]

    @property
    def hits(self):
        return self.entry.hits

    @property
    def hit(self):
        return self.hits[self.hit_index] if self.hits else None

    def take_for(self, filename):
        for t in self.takes:
            if filename in t.members:
                return t
        return None

    def load_audio(self):
        path = os.path.join(self.folder, self.entry.filename)
        data, _, self.rate = ml.read_wav(path)
        self.mono = ml.detection_channel(data)

        block = max(1, int(ENVELOPE_BLOCK_MS * self.rate / 1000.0))
        n = len(self.mono) // block
        env = np.abs(self.mono[:n * block]).reshape(n, block).max(axis=1)
        self.env_db = 20.0 * np.log10(np.maximum(env, 1e-6))
        self.env_t = np.arange(n) * block / self.rate

        self.entry.hits.sort(key=lambda h: h.start)
        self.hit_index = min(self.hit_index, max(0, len(self.hits) - 1))

    def recompute(self):
        """Ends, loudnesses and the velocity ladder, after any edit.

        The link is re-verified here rather than only at export. Moving a start
        in one member of a take can push its onset train out of correspondence
        with the reference, and since a member inherits its velocity ordering
        from the reference stroke by stroke, a broken correspondence silently
        maps the wrong stroke to the wrong layer. Better to say so at once."""
        e = self.entry
        e.hits.sort(key=lambda h: h.start)
        for i, h in enumerate(e.hits):
            h.end = e.hits[i + 1].start if i + 1 < len(e.hits) else e.n_frames
            h.rms, h.peak = ml.measure(self.mono, self.rate, h.start, h.end, self.p)
        ml.assign_ranks(e.hits)
        if self.takes:
            ml.refresh_link_status(self.entries, self.takes, self.p)
            self.link_notes = ml.apply_take_ordering(self.entries, self.takes)
        self.dirty = True

    def reference_ghost(self):
        """Where the take's reference file says the selected stroke is, mapped
        into this file by the take's median offset.

        Deliberately still shown when the link is broken: that is when it is
        most useful, since it points at where the stroke belongs. The median is
        taken over every stroke, so one displaced marker barely moves it."""
        take = self.take_for(self.entry.filename)
        if take is None or take.reference == self.entry.filename:
            return None
        ref = next((e for e in self.entries if e.filename == take.reference), None)
        if ref is None:
            return None

        mine = sorted(self.hits, key=lambda h: h.start)
        theirs = sorted(ref.hits, key=lambda h: h.start)
        if len(mine) != len(theirs) or self.hit is None:
            return None

        offsets = ml.take_offsets([m.start for m in mine], [t.start for t in theirs])
        if offsets.size == 0:
            return None
        # By identity: Hit is a value dataclass, so index() would match the
        # first stroke that merely compares equal.
        index = next((i for i, h in enumerate(mine) if h is self.hit), None)
        if index is None:
            return None
        return theirs[index].start + float(np.median(offsets))

    # ── edits ──────────────────────────────────────────────────────────────

    def select_nearest(self, sample):
        if not self.hits:
            return
        self.hit_index = min(range(len(self.hits)),
                             key=lambda i: abs(self.hits[i].start - sample))

    def move_selected(self, sample):
        if self.hit is None:
            return
        sample = int(np.clip(sample, 0, self.entry.n_frames - 1))
        self.hit.start = sample
        self.hit.locked = True
        self.recompute()
        self.select_nearest(sample)
        self.message = f"moved to {sample} and locked"

    def add_at(self, sample):
        sample = int(np.clip(sample, 0, self.entry.n_frames - 1))
        if any(h.start == sample for h in self.hits):
            self.message = "a stroke is already there"
            return
        self.hits.append(ml.Hit(start=sample, end=sample, locked=True))
        self.recompute()
        self.select_nearest(sample)
        self.message = f"added a stroke at {sample}"

    def delete_selected(self):
        if self.hit is None:
            return
        if len(self.hits) == 1:
            self.message = "refusing to delete the last stroke"
            return
        gone = self.hits.pop(self.hit_index)
        self.hit_index = min(self.hit_index, len(self.hits) - 1)
        self.recompute()
        self.message = f"deleted the stroke at {gone.start}"

    def refine_selected(self):
        """Snap to the attack foot near where the marker currently is. Lets you
        click roughly and let the detector place it exactly."""
        if self.hit is None:
            return
        floor = ml.noise_floor(self.mono, self.rate)
        snapped = ml.refine_onset(self.mono, self.rate, self.hit.start, self.p, floor)
        self.hit.start = int(snapped)
        self.hit.locked = True
        self.recompute()
        self.select_nearest(snapped)
        self.message = f"snapped to {snapped}"

    def align_to_reference(self, whole_file=False):
        """Put the selected stroke (or every stroke) on the exact sample the
        take's reference uses.

        Exact, not offset-corrected. Both files come from one multitrack, so
        sample N means the same instant in each. Giving them the same start
        preserves the recording's natural arrival delay between the close mic
        and the distant array, whereas placing each start on its own attack
        foot makes both attacks land together at trigger time and throws that
        delay away."""
        take = self.take_for(self.entry.filename)
        if take is None:
            self.message = "not part of a linked take"
            return
        if take.reference == self.entry.filename:
            self.message = "this file is the take's reference"
            return
        ref = next((e for e in self.entries if e.filename == take.reference), None)
        if ref is None:
            self.message = f"reference {take.reference} is not loaded"
            return

        mine = sorted(self.hits, key=lambda h: h.start)
        theirs = sorted(ref.hits, key=lambda h: h.start)
        if len(mine) != len(theirs):
            self.message = (f"cannot align: {len(mine)} strokes here against "
                            f"{len(theirs)} in the reference")
            return

        if whole_file:
            moved = sum(1 for m, t in zip(mine, theirs) if m.start != t.start)
            for m, t in zip(mine, theirs):
                m.start = t.start
                m.locked = True
            keep = self.hit
            self.recompute()
            if keep is not None:
                self.hit_index = next((i for i, h in enumerate(self.hits) if h is keep),
                                      self.hit_index)
            self.message = f"aligned all {moved} moved stroke(s) to the reference"
        else:
            if self.hit is None:
                return
            index = next(i for i, h in enumerate(mine) if h is self.hit)
            target = theirs[index].start
            delta = target - self.hit.start
            self.hit.start = target
            self.hit.locked = True
            self.recompute()
            self.select_nearest(target)
            self.message = f"aligned to the reference ({delta:+d} samples)"

    # ── loop points ────────────────────────────────────────────────────────

    @property
    def crossfade_ms(self):
        return CROSSFADE_STEPS[self.crossfade_index]

    def ensure_loop(self):
        """Give the stroke a loop to drag if it has none yet.

        Entering loop mode is the request to author one, so this is not done
        behind the user's back at any other moment: a stroke only gains loop
        points when someone looks at it in loop mode."""
        h = self.hit
        if h is None or h.has_loop:
            return False
        h.loop_start, h.loop_end, h.release_start = ml.default_loop_points(
            self.mono, h, self.rate)
        self.dirty = True
        return True

    def toggle_loop_mode(self):
        self.loop_mode = not self.loop_mode
        if not self.loop_mode:
            self.message = "loop mode off"
            return
        if self.ensure_loop():
            self.message = "loop mode: proposed a loop, now drag it"
        else:
            self.message = "loop mode"

    def set_loop_point(self, name):
        self.loop_point = name
        self.message = f"moving {LOOP_LABELS[name]}"

    def move_loop_point(self, sample, snap=False):
        """Put the selected loop point somewhere, keeping the five points in the
        order the engine requires rather than letting them cross over.

        Clamping here instead of at export is the difference between seeing what
        you get and being surprised by it later: the plugin would clamp exactly
        this way on load, quietly."""
        h = self.hit
        if h is None:
            return
        if not h.has_loop and not self.ensure_loop():
            return

        # sampleStart <= loopStart < loopEnd <= releaseStart <= sampleEnd
        #
        # loopEnd runs to sampleEnd, not to releaseStart. Stopping it at
        # releaseStart looks right and is a trap: the two are equal on a fresh
        # loop, so loopEnd's ceiling would be loopEnd itself and it could only
        # ever be dragged earlier. It pushes releaseStart ahead of it instead.
        if self.loop_point == "loop_start":
            lo, hi = h.start, h.loop_end - 1
        elif self.loop_point == "loop_end":
            lo, hi = h.loop_start + 1, h.end
        else:
            lo, hi = h.loop_end, h.end

        sample = int(np.clip(int(sample), lo, hi))
        if snap:
            window = max(1, int(SNAP_MS * self.rate / 1000.0))
            # Snapped inside the legal range, never snapped and then clipped.
            sample = ml.snap_to_zero_crossing(self.mono, sample, window,
                                              bounds=(lo, hi))

        was_touching = (self.loop_point == "loop_end"
                        and h.release_start == h.loop_end)
        setattr(h, self.loop_point, sample)

        if self.loop_point == "loop_end":
            # releaseStart follows loopEnd while the two are touching, which is
            # how a loop starts life, so dragging loopEnd earlier does not
            # silently open a stretch that is neither looped nor played. Once
            # they have been parted on purpose, it stays where it was put.
            if was_touching or 0 <= h.release_start < sample:
                h.release_start = sample

        h.locked = True
        self.dirty = True
        self.message = f"{LOOP_LABELS[self.loop_point]} = {sample}" + ("  snapped" if snap else "")

    def nudge_loop_point(self, ms=None, samples=None):
        """Move the selected loop point, by milliseconds or by exact samples.

        Samples are what the seam is actually made of: at 48 kHz one sample is
        20 microseconds, and a loop end a few samples off the crossing is the
        difference between a clean join and a tick."""
        h = self.hit
        if h is None:
            return
        delta = samples if samples is not None else int(ms * self.rate / 1000.0)
        current = getattr(h, self.loop_point)
        if current < 0:
            current = h.loop_end

        before = current
        self.move_loop_point(current + delta)
        now = getattr(h, self.loop_point)

        unit = f"{samples:+d} samples" if samples is not None else f"{ms:+g} ms"
        if now == before and delta:
            # Silence here would look like a dropped keypress rather than a
            # point already sitting against its limit.
            self.message = f"{LOOP_LABELS[self.loop_point]} is already at its limit ({now})"
        else:
            self.message = f"{LOOP_LABELS[self.loop_point]} {unit} -> {now}"

    def click_seam(self, dx):
        """Move a loop end from the seam pane, where a pixel is about a sample.

        The pane shows two places in the file at once, which is why it took no
        clicks before. But each half is one specific place: left of the wrap is
        the material running up to loopEnd, right of it the material that
        follows loopStart. So each half moves its own end, and the click means
        the same thing in both, "put the seam here" — after which the view
        recentres on the new seam and you can go again.

        releaseStart is not reachable from here on purpose: it plays no part in
        the seam, and nothing in this pane shows where it is."""
        h = self.hit
        if h is None or not h.has_loop or dx is None:
            return

        delta = int(round(dx))
        if delta < 0:
            self.loop_point = "loop_end"
            self.nudge_loop_point(samples=delta)
        else:
            self.loop_point = "loop_start"
            self.nudge_loop_point(samples=delta)

    def snap_loop_point(self):
        h = self.hit
        if h is None or not h.has_loop:
            return
        before = getattr(h, self.loop_point)
        self.move_loop_point(before, snap=True)
        after = getattr(h, self.loop_point)
        moved = after - before

        if moved:
            self.message = f"{LOOP_LABELS[self.loop_point]} snapped {moved:+d} samples"
        elif after + 1 < len(self.mono) and (self.mono[after] < 0) != (self.mono[after + 1] < 0):
            self.message = f"{LOOP_LABELS[self.loop_point]} was already on a crossing"
        else:
            # Not the same thing at all, and worth saying: a decayed tail or a
            # stretch with a DC offset can have no rising crossing within reach,
            # and reporting that as a successful snap would be a lie.
            self.message = (f"no rising zero crossing within {SNAP_MS:g} ms of "
                            f"{LOOP_LABELS[self.loop_point]}; left where it was")

    def toggle_loop_points(self):
        """Create a loop on a stroke that has none, or remove the one it has.

        One key for both because they are the same question asked of different
        states, and because stepping to a stroke without loop points otherwise
        leaves no way to give it any without leaving loop mode."""
        h = self.hit
        if h is None:
            return
        if h.has_loop:
            h.loop_start = h.loop_end = h.release_start = -1
            self.dirty = True
            self.message = "loop points cleared; this stroke exports as a one-shot again"
        else:
            self.ensure_loop()
            self.message = f"proposed a loop at {h.loop_start}..{h.loop_end}"

    def copy_loop_from_reference(self, whole_file=False):
        """Loop points are sample positions in a shared multitrack, so a linked
        member wants the reference's numbers exactly, for the same reason m and
        M copy starts exactly."""
        take = self.take_for(self.entry.filename)
        if take is None:
            self.message = "not part of a linked take"
            return
        if take.reference == self.entry.filename:
            self.message = "this file is the take's reference"
            return
        ref = next((e for e in self.entries if e.filename == take.reference), None)
        if ref is None:
            self.message = f"reference {take.reference} is not loaded"
            return

        mine = sorted(self.hits, key=lambda h: h.start)
        theirs = sorted(ref.hits, key=lambda h: h.start)
        if len(mine) != len(theirs):
            self.message = (f"cannot copy: {len(mine)} strokes here against "
                            f"{len(theirs)} in the reference")
            return

        pairs = zip(mine, theirs) if whole_file else [
            (self.hit, theirs[next(i for i, h in enumerate(mine) if h is self.hit)])]

        copied = 0
        for m, t in pairs:
            if not t.has_loop:
                continue
            m.loop_start, m.loop_end, m.release_start = (t.loop_start, t.loop_end,
                                                         t.release_start)
            m.locked = True
            copied += 1

        self.dirty = copied > 0
        self.message = (f"copied loop points for {copied} stroke(s) from {take.reference}"
                        if copied else "the reference has no loop points to copy")

    def preview(self):
        """Play the selected stroke through its loop.

        sounddevice is optional on purpose: everything else in loop mode works
        without an audio device, and a missing package should cost you the
        listening, not the tool."""
        h = self.hit
        if h is None:
            return
        try:
            import sounddevice as sd
        except Exception as exc:
            self.message = f"no preview: sounddevice unavailable ({exc})"
            return

        audio = ml.render_loop(self.mono, self.rate, h,
                               crossfade_ms=self.crossfade_ms,
                               hold_s=1.5,
                               release_mode=self.release_mode,
                               release_s=0.3)
        try:
            sd.stop()
            sd.play(audio, self.rate)
        except Exception as exc:
            self.message = f"no preview: {exc}"
            return

        self.message = (f"playing {len(audio) / self.rate:.1f} s   "
                        f"crossfade {self.crossfade_ms:g} ms   release {self.release_mode}")

    def cycle_crossfade(self):
        self.crossfade_index = (self.crossfade_index + 1) % len(CROSSFADE_STEPS)
        room = ml.max_crossfade_samples(self.hit) if self.hit else 0.0
        asked = self.crossfade_ms * 0.001 * self.rate
        note = ""
        if self.crossfade_ms > 0.0 and asked > room:
            note = f"   (clamped to {room / self.rate * 1000.0:.1f} ms by this loop)"
        self.message = f"preview crossfade {self.crossfade_ms:g} ms{note}"

    def toggle_release_mode(self):
        self.release_mode = "region" if self.release_mode == "loop" else "loop"
        self.message = f"preview release: {self.release_mode}"

    def unlock_selected(self):
        if self.hit is None:
            return
        self.hit.locked = False
        self.dirty = True
        self.message = "unlocked, detection will own it again"

    def nudge(self, ms=None, samples=None):
        if self.hit is None:
            return
        delta = samples if samples is not None else int(ms * self.rate / 1000.0)
        self.move_selected(self.hit.start + delta)
        self.message = (f"nudged {samples:+d} samples" if samples is not None
                        else f"nudged {ms:+g} ms")

    def step_file(self, delta):
        self.file_index = (self.file_index + delta) % len(self.entries)
        self.hit_index = 0
        self.load_audio()
        self.message = ""

    def step_hit(self, delta):
        if self.hits:
            self.hit_index = (self.hit_index + delta) % len(self.hits)

    def save(self):
        ml.save_state(self.state_path, self.entries, self.takes, self.p)
        self.dirty = False
        self.message = f"saved {os.path.basename(self.state_path)}"

    # ── drawing ────────────────────────────────────────────────────────────

    def draw_loop_panes(self):
        """Top: the whole stroke with its three loop points. Bottom: the seam.

        The stroke, not the file, because loop points live inside one stroke and
        at file scale they would be three lines in the same pixel."""
        h = self.hit
        if h is None:
            return

        lo, hi = int(h.start), int(min(h.end, len(self.mono)))
        if self.show_full and hi > lo:
            self.ax_full.set_visible(True)
            t = np.arange(lo, hi) / self.rate * 1000.0
            self.ax_full.plot(t, self.mono[lo:hi], lw=0.6, color="0.45")

            if h.has_loop:
                self.ax_full.axvspan(h.loop_start / self.rate * 1000.0,
                                     h.loop_end / self.rate * 1000.0,
                                     color="mediumseagreen", alpha=0.10, zorder=0)
                for name in LOOP_POINTS:
                    value = getattr(h, name)
                    if value < 0:
                        continue
                    selected = (name == self.loop_point)
                    self.ax_full.axvline(value / self.rate * 1000.0,
                                         color=LOOP_COLOURS[name],
                                         lw=2.0 if selected else 1.1,
                                         ls="-" if selected else "--",
                                         zorder=5 if selected else 3)
                    self.ax_full.annotate(LOOP_LABELS[name],
                                          (value / self.rate * 1000.0, 0),
                                          xytext=(3, 8), textcoords="offset points",
                                          color=LOOP_COLOURS[name], fontsize=8,
                                          fontweight="bold" if selected else "normal")

            self.ax_full.set_xlim(t[0], t[-1])
            self.ax_full.set_xlabel("ms into the file")
            self.ax_full.set_ylabel("amplitude")
        else:
            self.ax_full.set_visible(False)

        view = ml.seam_view(self.mono, h, self.rate, self.crossfade_ms, span=SEAM_SPAN)
        if view is None:
            return
        x, heard, naive, ahead = view

        self.ax_zoom.plot(x, ahead, lw=0.8, color="0.75",
                          label="if it had not wrapped")
        self.ax_zoom.plot(x, naive, lw=0.8, color="crimson", alpha=0.6,
                          label="hard seam")
        self.ax_zoom.plot(x, heard, lw=1.2, color="deepskyblue",
                          label=f"as played, crossfade {self.crossfade_ms:g} ms")
        self.ax_zoom.axvline(0.0, color="0.3", lw=1.0)
        self.ax_zoom.axhline(0.0, color="0.85", lw=0.5, zorder=0)
        self.ax_zoom.legend(loc="upper right", fontsize=7, framealpha=0.85)
        self.ax_zoom.set_xlim(-SEAM_SPAN, SEAM_SPAN)
        self.ax_zoom.set_xlabel("samples either side of the wrap")
        self.ax_zoom.set_ylabel("amplitude")

    def draw(self):
        e = self.entry
        self.ax_full.clear()
        self.ax_zoom.clear()

        if self.loop_mode:
            self.draw_loop_panes()
        elif self.show_full:
            self.ax_full.set_visible(True)
            self.ax_full.plot(self.env_t, self.env_db, lw=0.5, color="0.55")
            for i, h in enumerate(self.hits):
                selected = (i == self.hit_index)
                self.ax_full.axvline(
                    h.start / self.rate,
                    color="deepskyblue" if selected else ("orange" if h.locked else "crimson"),
                    lw=1.6 if selected else 0.9,
                    ls="--" if h.locked and not selected else "-",
                    alpha=0.95,
                    zorder=5 if selected else 3)
            self.ax_full.set_ylim(-100, 5)
            if len(self.env_t):
                # A little air at either end, or the very common stroke at
                # sample 0 hides under the axis spine.
                duration = self.env_t[-1]
                self.ax_full.set_xlim(-0.005 * duration, duration * 1.005)
            self.ax_full.set_ylabel("dB peak")
            self.ax_full.set_xlabel("seconds")
        else:
            self.ax_full.set_visible(False)

        h = self.hit
        if h is not None and not self.loop_mode:
            span = int(ZOOM_MS * self.rate / 1000.0)
            lo, hi = max(0, h.start - span), min(len(self.mono), h.start + span)
            t = (np.arange(lo, hi) - h.start) / self.rate * 1000.0
            self.ax_zoom.plot(t, self.mono[lo:hi], lw=0.7, color="0.35")
            self.ax_zoom.axvline(0.0, color="deepskyblue", lw=1.4)
            self.ax_zoom.axhline(0.0, color="0.85", lw=0.5, zorder=0)

            ghost = self.reference_ghost()
            if ghost is not None:
                dt = (ghost - h.start) / self.rate * 1000.0
                if abs(dt) < ZOOM_MS:
                    self.ax_zoom.axvline(dt, color="mediumseagreen", lw=1.2, ls=":")
                    self.ax_zoom.annotate(f"reference {dt:+.1f} ms", (dt, 0),
                                          xytext=(4, 6), textcoords="offset points",
                                          color="mediumseagreen", fontsize=8)
                else:
                    # Off the edge of the zoom: say which way and how far, so a
                    # badly placed marker can still be walked back.
                    edge = -ZOOM_MS if dt < 0 else ZOOM_MS
                    self.ax_zoom.annotate(
                        f"{'<-- ' if dt < 0 else ''}reference {dt:+.0f} ms"
                        f"{' -->' if dt > 0 else ''}",
                        (edge, 0), xytext=(6 if dt < 0 else -6, 8),
                        textcoords="offset points",
                        ha="left" if dt < 0 else "right",
                        color="mediumseagreen", fontsize=8, fontweight="bold")

            self.ax_zoom.set_xlim(-ZOOM_MS, ZOOM_MS)
            self.ax_zoom.set_xlabel("ms from the selected start")
            self.ax_zoom.set_ylabel("amplitude")

        take = self.take_for(e.filename)
        if take and take.status.startswith("linked"):
            if take.reference == e.filename:
                others = [m for m in take.members if m != e.filename]
                inherited = f"reference for {len(others)} other file(s)"
            else:
                inherited = f"ordering inherited from {take.reference}"
            take_line = f"take {take.name} OK: {inherited}"
        elif take:
            take_line = f"!! take {take.name} BROKEN: {take.status}"
        else:
            take_line = "not part of a linked take"

        locked_n = sum(1 for x in self.hits if x.locked)
        head = (f"[{self.file_index + 1}/{len(self.entries)}] {e.filename}\n"
                f"{len(self.hits)} strokes (expected {e.expected}), "
                f"{locked_n} locked   |   {take_line}")
        if h is not None:
            head += (f"\nstroke {self.hit_index + 1}/{len(self.hits)} at {h.start}"
                     f"   velocity layer {h.rank + 1}/{len(self.hits)}"
                     f"{'   LOCKED' if h.locked else ''}")
        if self.loop_mode and h is not None:
            if h.has_loop:
                room = ml.max_crossfade_samples(h) / self.rate * 1000.0
                head += (f"\nLOOP  {h.loop_start}..{h.loop_end} "
                         f"({h.loop_length} samples, {h.loop_length / self.rate * 1000.0:.1f} ms)"
                         f"   release from {h.release_start}"
                         f"   |   moving {LOOP_LABELS[self.loop_point]}"
                         f"   |   preview crossfade {self.crossfade_ms:g} ms "
                         f"(room for {room:.0f} ms), release {self.release_mode}")
                problems = ml.loop_warnings(h)
                if problems:
                    head += "\n!! " + "; ".join(problems)
            else:
                head += "\nLOOP  no loop points on this stroke"
        if not self.loop_mode:
            # Loop points are absolute sample indices, so moving a stroke start
            # can strand them outside their own slice. Nothing moves them for
            # you, so at least say it where the moving happens.
            stranded = sum(1 for x in self.hits if ml.loop_warnings(x))
            if stranded:
                head += (f"\n!! {stranded} stroke(s) have loop points outside "
                         "their slice: press L to look")

        if self.message:
            head += f"   |   {self.message}"
        if self.dirty:
            head += "   |   unsaved"

        mine = [n for n in self.link_notes if e.filename in n]
        if mine:
            head += "\n" + "; ".join(mine)

        broken = take is not None and not take.status.startswith("linked")
        self.fig.suptitle(head, fontsize=9, ha="left", x=0.01,
                          color="firebrick" if broken else "black")
        self.fig.canvas.draw_idle()

    # ── events ─────────────────────────────────────────────────────────────

    def on_click(self, event):
        if event.xdata is None:
            return

        if self.loop_mode:
            if event.inaxes is self.ax_full:
                # The stroke pane is for coarse placement: its whole width is
                # the stroke, so a pixel is many samples.
                self.move_loop_point(event.xdata * self.rate / 1000.0)
            elif event.inaxes is self.ax_zoom:
                self.click_seam(event.xdata)
            self.draw()
            return

        if event.inaxes is self.ax_full:
            self.last_click = int(event.xdata * self.rate)
            self.select_nearest(self.last_click)
        elif event.inaxes is self.ax_zoom and self.hit is not None:
            self.last_click = int(self.hit.start + event.xdata * self.rate / 1000.0)
            self.move_selected(self.last_click)
        self.draw()

    def on_key(self, event):
        k = event.key
        self.message = ""

        if k == "q":
            if self.dirty:
                self.message = "unsaved changes: press s to save, or q again to discard"
                self.dirty = False          # a second q now leaves
                self.draw()
                return
            import matplotlib.pyplot as plt
            plt.close(self.fig)
            return

        # Shared by both modes.
        actions = {
            "n": lambda: self.step_file(+1),
            "p": lambda: self.step_file(-1),
            "right": lambda: self.step_hit(+1),
            "left": lambda: self.step_hit(-1),
            "s": self.save,
            "f": lambda: setattr(self, "show_full", not self.show_full),
            "L": self.toggle_loop_mode,
        }

        if self.loop_mode:
            # The nudge and reference keys keep their shape and change their
            # subject: in loop mode they move loop points, not stroke starts.
            actions.update({
                ".": lambda: self.nudge_loop_point(ms=+1.0),
                ",": lambda: self.nudge_loop_point(ms=-1.0),
                ">": lambda: self.nudge_loop_point(ms=+10.0),
                "<": lambda: self.nudge_loop_point(ms=-10.0),
                # One sample. Both spellings of each key, because + and - are
                # shifted on most layouts and toolkits disagree about which
                # character they report.
                "+": lambda: self.nudge_loop_point(samples=+1),
                "=": lambda: self.nudge_loop_point(samples=+1),
                "-": lambda: self.nudge_loop_point(samples=-1),
                "_": lambda: self.nudge_loop_point(samples=-1),
                "1": lambda: self.set_loop_point("loop_start"),
                "2": lambda: self.set_loop_point("loop_end"),
                "3": lambda: self.set_loop_point("release_start"),
                "z": self.snap_loop_point,
                "c": self.cycle_crossfade,
                "e": self.toggle_release_mode,
                "x": self.toggle_loop_points,
                " ": self.preview,
                "m": lambda: self.copy_loop_from_reference(whole_file=False),
                "M": lambda: self.copy_loop_from_reference(whole_file=True),
            })
        else:
            actions.update({
                ".": lambda: self.nudge(ms=+1.0),
                ",": lambda: self.nudge(ms=-1.0),
                ">": lambda: self.nudge(ms=+10.0),
                "<": lambda: self.nudge(ms=-10.0),
                "+": lambda: self.nudge(samples=+1),
                "=": lambda: self.nudge(samples=+1),
                "-": lambda: self.nudge(samples=-1),
                "_": lambda: self.nudge(samples=-1),
                "d": self.delete_selected,
                "r": self.refine_selected,
                "u": self.unlock_selected,
                "m": lambda: self.align_to_reference(whole_file=False),
                "M": lambda: self.align_to_reference(whole_file=True),
            })

        if k == "a" and not self.loop_mode:
            if self.last_click is None:
                self.message = "click where you want it first"
            else:
                self.add_at(self.last_click)
        elif k in actions:
            actions[k]()
        else:
            return
        self.draw()

    def run(self):
        import matplotlib.pyplot as plt

        # These defaults would swallow the keys the editor uses: s saves a png,
        # p pans, r resets the view, left/right navigate the view stack.
        for name in ("save", "pan", "home", "back", "forward",
                     "zoom", "grid", "grid_minor", "fullscreen",
                     "xscale", "yscale", "quit", "quit_all"):
            key = f"keymap.{name}"
            if key in plt.rcParams:
                plt.rcParams[key] = []

        self.fig, (self.ax_full, self.ax_zoom) = plt.subplots(
            2, 1, figsize=(14, 7), gridspec_kw={"height_ratios": [2, 1]})
        self.fig.subplots_adjust(top=0.86)
        self.fig.canvas.mpl_connect("key_press_event", self.on_key)
        self.fig.canvas.mpl_connect("button_press_event", self.on_click)

        self.load_audio()
        self.draw()
        print(KEYS)
        plt.show()


# ─── optional Tk front end ─────────────────────────────────────────────────
#
# Used only when the editor is started with no folder argument, and when it
# closes. Everything works without it; if tkinter or a display is missing the
# command line behaviour is unchanged.

def _tk():
    try:
        import tkinter
    except ImportError:
        return None
    try:
        root = tkinter.Tk()
    except Exception:
        return None                     # no display
    root.withdraw()
    return root


def pick_folder():
    """Ask for the wav folder. Returns None if there is no usable Tk."""
    root = _tk()
    if root is None:
        return None
    from tkinter import filedialog
    folder = filedialog.askdirectory(title="Choose the take wav folder "
                                           "(for example Kits/<kit>/data/wav)")
    root.destroy()
    return folder or None


def finish_dialog(editor, mapping_path):
    """On quit, offer to save the state and to write mapping.xml.

    Writing the mapping is off by default and names its destination, because
    overwriting a kit's mapping.xml should be something you chose rather than
    something that happened."""
    root = _tk()
    if root is None:
        return False

    import tkinter as tk
    from tkinter import filedialog, messagebox

    root.deiconify()
    root.title("Finish editing")

    strokes = sum(len(e.hits) for e in editor.entries)
    locked = sum(1 for e in editor.entries for h in e.hits if h.locked)
    broken = [t.name for t in editor.takes if not t.status.startswith("linked")]

    frame = tk.Frame(root, padx=14, pady=12)
    frame.pack(fill="both", expand=True)

    tk.Label(frame, justify="left", anchor="w",
             text=(f"{len(editor.entries)} files, {strokes} strokes, "
                   f"{locked} locked\n"
                   + ("unsaved changes" if editor.dirty else "no unsaved changes"))
             ).pack(anchor="w")

    if broken:
        tk.Label(frame, justify="left", anchor="w", fg="firebrick",
                 text="unverified take(s): " + ", ".join(broken)).pack(anchor="w", pady=(4, 0))

    save_state = tk.BooleanVar(value=editor.dirty)
    write_xml = tk.BooleanVar(value=False)
    dest = tk.StringVar(value=mapping_path)

    tk.Checkbutton(frame, text=f"Save state  ({os.path.basename(editor.state_path)})",
                   variable=save_state).pack(anchor="w", pady=(10, 0))
    tk.Checkbutton(frame, text="Write mapping.xml to:",
                   variable=write_xml).pack(anchor="w", pady=(6, 0))

    row = tk.Frame(frame)
    row.pack(fill="x", padx=(22, 0))
    tk.Entry(row, textvariable=dest, width=52).pack(side="left", fill="x", expand=True)

    def browse():
        chosen = filedialog.asksaveasfilename(
            title="Write mapping.xml to", defaultextension=".xml",
            initialfile=os.path.basename(dest.get()),
            initialdir=os.path.dirname(dest.get()))
        if chosen:
            dest.set(chosen)
            write_xml.set(True)

    tk.Button(row, text="...", command=browse, width=3).pack(side="left", padx=(6, 0))

    result = {"go": False}

    def done():
        result["go"] = True
        root.quit()

    buttons = tk.Frame(frame)
    buttons.pack(anchor="e", pady=(14, 0))
    tk.Button(buttons, text="Discard and quit", command=root.quit).pack(side="left", padx=(0, 8))
    tk.Button(buttons, text="Done", command=done, default="active").pack(side="left")
    root.bind("<Return>", lambda _e: done())

    root.mainloop()
    do_save, do_write, target = save_state.get(), write_xml.get(), dest.get()
    root.destroy()

    if not result["go"]:
        print("quit without writing anything")
        return False

    if do_save:
        ml.save_state(editor.state_path, editor.entries, editor.takes, editor.p)
        editor.dirty = False
        print(f"wrote {editor.state_path}")

    if do_write and target:
        template = target if not os.path.exists(mapping_path) else mapping_path
        if not os.path.exists(template):
            ml.create_scaffold(editor.entries, target)
            template = target
            print(f"no existing mapping, generated a scaffold in {target}")
        notes = ml.write_mapping_xml(template, editor.entries, target)
        print(f"wrote {target}")
        for n in notes:
            print(f"  {n}")
    return True


# ─── entry point ───────────────────────────────────────────────────────────

def build_editor(folder, state_path, use_state, params):
    if use_state and os.path.exists(state_path):
        entries, takes, p = ml.load_state(state_path)
        print(f"loaded {state_path}")
        # The state carries hit lists but not audio metadata for files added
        # since; anything missing is simply not offered for editing.
        entries = [e for e in entries
                   if os.path.exists(os.path.join(folder, e.filename))]
    else:
        print(f"analysing {folder} ...")
        previous = None
        if os.path.exists(state_path):
            try:
                previous, _, _ = ml.load_state(state_path)
            except Exception as exc:
                print(f"warning: could not read {state_path}: {exc}")
        entries, takes, p, notes = ml.analyse_folder(folder, params, previous=previous)
        for n in notes:
            print(f"  {n}")
    if not entries:
        raise SystemExit("no conventionally named wav files found")
    # A state file carries the status it was saved with; recheck against the
    # hits actually present so the panel is true from the first frame.
    ml.refresh_link_status(entries, takes, p)
    return Editor(folder, entries, takes, p, state_path)


def selftest(folder, state_path):
    """Drives the editor's state machine without a display, so the edit logic
    is covered even though the clicking is not."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import shutil
    import tempfile

    # Every run starts from nothing. A state file left by the previous run is
    # picked up as `previous` by build_editor, whose locked starts then shift
    # the hits these checks are written against: the second run of an unchanged
    # tool would fail where the first passed.
    scratch = tempfile.mkdtemp(prefix="mappingeditor-selftest-")
    state_path = os.path.join(scratch, "mapping_state.json")

    ed = build_editor(folder, state_path, use_state=False, params=ml.DetectParams())
    ed.fig, (ed.ax_full, ed.ax_zoom) = plt.subplots(2, 1)
    ed.load_audio()
    ed.draw()

    checks, failures = 0, []

    def check(label, ok):
        nonlocal checks
        checks += 1
        if not ok:
            failures.append(label)

    start_file = ed.entry.filename
    n0 = len(ed.hits)

    ed.step_hit(+1)
    check("select moves", ed.hit_index == 1)

    before = ed.hit.start
    ed.nudge(+1.0)
    check("nudge moves by 1 ms", ed.hit.start == before + int(ed.rate / 1000))
    check("nudge locks", ed.hit.locked)
    check("nudge keeps the count", len(ed.hits) == n0)
    check("ends follow the move",
          all(ed.hits[i].end == ed.hits[i + 1].start for i in range(len(ed.hits) - 1)))
    check("last end is the file length", ed.hits[-1].end == ed.entry.n_frames)
    check("ranks stay a permutation",
          sorted(h.rank for h in ed.hits) == list(range(len(ed.hits))))

    ed.refine_selected()
    check("refine keeps the count", len(ed.hits) == n0)

    gap = (ed.hits[0].start + ed.hits[1].start) // 2
    ed.add_at(gap)
    check("add grows the count", len(ed.hits) == n0 + 1)
    check("added stroke is locked", any(h.start == gap and h.locked for h in ed.hits))
    check("ranks still a permutation",
          sorted(h.rank for h in ed.hits) == list(range(len(ed.hits))))

    ed.select_nearest(gap)
    ed.delete_selected()
    check("delete shrinks the count", len(ed.hits) == n0)
    check("delete removed the right one", all(h.start != gap for h in ed.hits))

    ed.unlock_selected()
    check("unlock clears the flag", not ed.hit.locked)

    ed.step_file(+1)
    check("next file changes file", ed.entry.filename != start_file)
    ed.step_file(-1)
    check("previous file comes back", ed.entry.filename == start_file)

    ed.on_key(type("E", (), {"key": "right", "xdata": None, "inaxes": None})())
    check("key dispatch works", ed.hit_index != 0 or len(ed.hits) == 1)

    # The link watchdog: break a member's correspondence with its reference and
    # make sure it is noticed, then put it back and make sure it recovers.
    # Deliberately not the file the checks above edited, so the two are
    # independent.
    member = next((e for e in ed.entries
                   for t in ed.takes
                   if t.status.startswith("linked")
                   and e.filename in t.members and e.filename != t.reference
                   and e.filename != start_file), None)
    if member is None:
        check("a linked member exists to test the watchdog", False)
    else:
        take = ed.take_for(member.filename)
        ed.file_index = ed.entries.index(member)
        ed.load_audio()
        ed.hit_index = 2
        good = ed.hit.start
        check("link starts out verified", take.status.startswith("linked"))
        check("ghost marker is available", ed.reference_ghost() is not None)

        ed.move_selected(good + int(0.150 * ed.rate))     # 150 ms out
        check("a badly moved start breaks the link",
              not take.status.startswith("linked"))
        ghost = ed.reference_ghost()
        check("the ghost survives a broken link", ghost is not None)
        check("and still points near where the stroke belongs",
              ghost is not None and abs(ghost - good) < 0.020 * ed.rate)

        ed.move_selected(good)
        check("putting it back repairs the link", take.status.startswith("linked"))

        n_before = len(ed.hits)
        # Captured once: after the insert this midpoint is itself hits[1], so
        # recomputing it would land halfway between two strokes and select the
        # wrong one to delete.
        spurious = (ed.hits[0].start + ed.hits[1].start) // 2
        ed.add_at(spurious)
        check("a stroke count mismatch breaks the link",
              not take.status.startswith("linked"))
        ed.select_nearest(spurious)
        check("the spurious stroke is the selected one", ed.hit.start == spurious)
        ed.delete_selected()
        check("removing it restores the count", len(ed.hits) == n_before)
        check("and repairs the link", take.status.startswith("linked"))

        # m / M: align onto the reference's exact samples.
        ref_entry = next(e for e in ed.entries if e.filename == take.reference)
        ref_starts = [h.start for h in sorted(ref_entry.hits, key=lambda h: h.start)]

        ed.hit_index = 3
        ed.align_to_reference(whole_file=False)
        check("m lands on the reference sample exactly",
              ed.hits[3].start == ref_starts[3])
        check("m locks the stroke", ed.hits[3].locked)
        check("m leaves the other strokes alone",
              sum(1 for h, r in zip(sorted(ed.hits, key=lambda x: x.start),
                                    ref_starts) if h.start == r) < len(ref_starts))

        ed.align_to_reference(whole_file=True)
        check("M aligns every stroke",
              [h.start for h in sorted(ed.hits, key=lambda x: x.start)] == ref_starts)
        check("M locks them all", all(h.locked for h in ed.hits))
        check("M leaves the link verified", take.status.startswith("linked"))
        check("aligned files report a zero offset", "offset 0 samples" in take.status)

        ed.file_index = ed.entries.index(ref_entry)
        ed.load_audio()
        ed.align_to_reference(whole_file=True)
        check("M on the reference itself is refused",
              "reference" in ed.message and "is the take" in ed.message)

        ed.file_index = ed.entries.index(next(e for e in ed.entries
                                              if e.filename == start_file))
        ed.load_audio()

    # ── loop mode ──────────────────────────────────────────────────────────

    ed.hit_index = 1
    h = ed.hit
    check("a fresh stroke has no loop points", not h.has_loop)

    ed.toggle_loop_mode()
    check("L turns loop mode on", ed.loop_mode)
    check("entering loop mode proposes a loop", h.has_loop)
    check("the proposal is inside the stroke",
          h.start <= h.loop_start < h.loop_end <= h.end)
    check("and releaseStart starts at loopEnd", h.release_start == h.loop_end)

    # The engine's ordering, defended against every way of crossing the points.
    ed.set_loop_point("loop_start")
    ed.move_loop_point(h.loop_end + 5000)
    check("loopStart cannot pass loopEnd", h.loop_start < h.loop_end)
    ed.move_loop_point(h.start - 5000)
    check("loopStart cannot go before sampleStart", h.loop_start >= h.start)

    ed.set_loop_point("loop_end")
    ed.move_loop_point(h.loop_start - 5000)
    check("loopEnd cannot pass loopStart", h.loop_end > h.loop_start)
    ed.move_loop_point(h.end + 5000)
    check("loopEnd cannot go past sampleEnd", h.loop_end <= h.end)

    # The trap this was: on a fresh loop releaseStart equals loopEnd, so making
    # releaseStart the ceiling froze loopEnd in place, draggable only earlier.
    h.loop_start, h.loop_end, h.release_start = h.start, h.start + 1000, h.start + 1000
    ed.set_loop_point("loop_end")
    ed.move_loop_point(h.start + 4000)
    check("loopEnd can be dragged later when releaseStart is touching it",
          h.loop_end == h.start + 4000)
    check("and pushes releaseStart ahead of it",
          h.release_start == h.loop_end)

    ed.set_loop_point("release_start")
    ed.move_loop_point(h.loop_end - 5000)
    check("releaseStart cannot precede loopEnd", h.release_start >= h.loop_end)
    ed.move_loop_point(h.end + 5000)
    check("releaseStart cannot go past sampleEnd", h.release_start <= h.end)
    check("a legal loop reports no warnings", ml.loop_warnings(h) == [])

    # Nudging retargets: the same keys must move the point, not the stroke.
    ed.set_loop_point("loop_start")
    before_start, before_loop = h.start, h.loop_start
    ed.nudge_loop_point(ms=+1.0)
    check("nudge in loop mode moves loopStart",
          h.loop_start == before_loop + int(ed.rate / 1000))
    check("and leaves the stroke start alone", h.start == before_start)

    # Single-sample nudges, the resolution the seam is actually made of.
    at = h.loop_start
    ed.nudge_loop_point(samples=+1)
    check("+ moves loopStart exactly one sample", h.loop_start == at + 1)
    ed.nudge_loop_point(samples=-1)
    check("- moves it back exactly one sample", h.loop_start == at)
    check("the message counts samples, not milliseconds", "samples" in ed.message)

    ed.set_loop_point("loop_start")
    ed.move_loop_point(h.start)                       # hard against its limit
    ed.nudge_loop_point(samples=-1)
    check("a nudge into a limit says so rather than looking dropped",
          h.loop_start == h.start and "limit" in ed.message)
    ed.move_loop_point(at)

    # Clicking the seam pane: each half moves its own end, by exactly the
    # offset clicked, and selects that end so the header agrees.
    ed.set_loop_point("release_start")
    end_before, start_before = h.loop_end, h.loop_start
    ed.click_seam(-30.0)
    check("a click left of the wrap moves loopEnd by that many samples",
          h.loop_end == end_before - 30)
    check("and selects loopEnd", ed.loop_point == "loop_end")
    check("without touching loopStart", h.loop_start == start_before)

    end_before = h.loop_end
    ed.click_seam(+12.4)
    check("a click right of the wrap moves loopStart",
          h.loop_start == start_before + 12)
    check("and selects loopStart", ed.loop_point == "loop_start")
    check("without touching loopEnd", h.loop_end == end_before)

    # releaseStart tracks loopEnd while the two are touching, so dragging
    # loopEnd earlier cannot silently open a stretch that is neither looped
    # nor played.
    h.loop_end = h.release_start = start_before + 4000
    ed.set_loop_point("loop_end")
    ed.nudge_loop_point(samples=-500)
    check("releaseStart follows loopEnd while they touch",
          h.release_start == h.loop_end)
    h.release_start = h.loop_end + 700
    parted = h.release_start
    ed.nudge_loop_point(samples=-100)
    check("and stays put once they have been parted on purpose",
          h.release_start == parted and h.loop_end < parted)

    # Zero-crossing snapping on real audio: land on a rising crossing, or say
    # there was none. The stroke foot sits on a very quiet low-frequency drift
    # that can go 5 ms without crossing zero, so "always finds one" would be a
    # false requirement.
    ed.set_loop_point("loop_start")
    window = int(SNAP_MS * ed.rate / 1000.0)
    # Counted inside the legal range, which is what the snap searches. Counting
    # the raw window instead would demand it find a crossing it must refuse,
    # since one below sampleStart is not a legal place for loopStart.
    available = ml.zero_crossings(ed.mono,
                                  max(h.loop_start - window, h.start),
                                  min(h.loop_start + window, h.loop_end - 1),
                                  True).size
    ed.snap_loop_point()
    i = h.loop_start
    landed = i + 1 < len(ed.mono) and (ed.mono[i] < 0) != (ed.mono[i + 1] < 0)
    check("snap lands on a sign change when one is in reach",
          landed if available else True)
    check("and says so plainly when none is",
          available > 0 or "no rising zero crossing" in ed.message)
    check("snap stays within its window",
          abs(h.loop_start - before_loop - int(ed.rate / 1000)) <= window + 1)

    # The same on a signal that certainly has crossings, where the right answer
    # is known: a 200 Hz sine crosses upward every 240 samples at 48 kHz.
    sine = np.sin(2 * np.pi * 200.0 * np.arange(4800) / 48000.0).astype(np.float32)
    for probe, expect in ((250, 240), (1200, 1200), (1300, 1200), (1400, 1440)):
        got = ml.snap_to_zero_crossing(sine, probe, 200)
        check(f"snap {probe} -> {expect} on a 200 Hz sine", got == expect)
    check("snap respects its bounds",
          ml.snap_to_zero_crossing(sine, 250, 200, bounds=(245, 260)) == 250)
    check("snap ignores falling crossings",
          sine[ml.snap_to_zero_crossing(sine, 370, 200)] <= 0.0)

    # The preview renderer, which is the part that has to agree with the engine.
    audio = ml.render_loop(ed.mono, ed.rate, h, crossfade_ms=20.0,
                           hold_s=0.2, release_mode="loop", release_s=0.05)
    check("preview renders audio", audio.size > 0)
    check("preview is finite", bool(np.all(np.isfinite(audio))))
    check("preview ends quiet (envelope release ran)", abs(float(audio[-1])) < 0.05)

    region = ml.render_loop(ed.mono, ed.rate, h, crossfade_ms=20.0,
                            hold_s=0.2, release_mode="region")
    check("region release renders too", region.size > 0)

    # The renderer itself is checked on a signal built for the purpose rather
    # than on whatever drum stroke happens to be second in the folder: a 220 Hz
    # sine whose loop is not a whole number of periods, so a hard seam has to
    # step and the answers are known in advance.
    synth_rate = 48000
    t = np.arange(60000) / synth_rate
    tone = (np.sin(2 * np.pi * 220.0 * t) * 0.7).astype(np.float32)
    synth = ml.Hit(start=0, end=40000, loop_start=6000, loop_end=11000,
                   release_start=20000)
    natural = 2 * np.pi * 220.0 / synth_rate      # the sine's own steepest step

    played = ml.render_loop(tone, synth_rate, synth, crossfade_ms=0.0,
                            hold_s=1.0, release_mode="loop", release_s=0.05)
    check("a held note keeps sounding well past one loop length",
          played.size > 8 * synth.loop_length)
    check("and stays inside the material it was given",
          float(np.abs(played).max()) <= 0.7 + 1e-6)

    def worst_step(sig):
        return float(np.max(np.abs(np.diff(sig)))) / max(float(np.abs(sig).max()), 1e-12)

    hard_seam = worst_step(ml.render_loop(tone, synth_rate, synth, crossfade_ms=0.0,
                                          hold_s=0.5, release_s=0.05))
    faded = worst_step(ml.render_loop(tone, synth_rate, synth, crossfade_ms=20.0,
                                      hold_s=0.5, release_s=0.05))
    check("a hard seam on a mismatched loop really does step", hard_seam > 3 * natural)
    check("the crossfade removes it", faded < 1.2 * natural)

    check("the crossfade is clamped by the loop and the attack",
          ml.max_crossfade_samples(synth) == 5000 - 1)

    # A stroke's end is the next stroke's start, so for sustained material the
    # slice runs well past the sound. The proposal has to follow the sound, not
    # the slice, or it lands in digital silence where there is nothing to loop
    # and no zero crossing to snap to.
    held = np.concatenate([tone[:24000], np.zeros(24000, dtype=np.float32)])
    quiet = ml.Hit(start=0, end=48000)
    check("sounding_end finds the end of the sound, not of the slice",
          23000 < ml.sounding_end(held, quiet) <= 24010)
    ls, le, _ = ml.default_loop_points(held, quiet, synth_rate)
    check("the proposed loop stays inside the sounding part", le <= 24010)
    check("and is not empty", ls < le)
    check("the proposal has signal to loop",
          float(np.abs(held[ls:le]).max()) > 0.1)

    # The release region: the head must end up in the tail, not in the loop.
    region_out = ml.render_loop(tone, synth_rate, synth, crossfade_ms=20.0,
                                hold_s=0.3, release_mode="region")
    check("the release region plays out to sampleEnd",
          region_out.size >= 0.3 * synth_rate + (synth.end - synth.release_start) - 2)
    check("and joins the tail without a step",
          worst_step(region_out) < 1.3 * natural)

    # The seam view, on the same signal and for the same reason.
    view = ml.seam_view(tone, synth, synth_rate, crossfade_ms=0.0)
    check("seam view returns four curves", view is not None and len(view) == 4)
    _, hard_curve, naive, ahead = view
    _, faded_curve, _, _ = ml.seam_view(tone, synth, synth_rate, crossfade_ms=20.0)

    def step_at_seam(curve):
        mid = len(curve) // 2
        return float(np.max(np.abs(np.diff(curve[mid - 2:mid + 2]))))

    check("the seam view shows the hard seam stepping",
          step_at_seam(hard_curve) > 3 * natural)
    check("and shows the crossfade closing it",
          step_at_seam(faded_curve) < 1.2 * natural)
    check("the naive curve is the hard seam", np.allclose(naive, hard_curve, atol=1e-6))
    check("the ahead curve continues past loopEnd instead of wrapping",
          not np.allclose(ahead[len(ahead) // 2:], naive[len(naive) // 2:], atol=1e-6))

    # Real audio still has to survive the same path, even if the numbers are
    # not sharp on a decaying drum tail.
    real = ml.render_loop(ed.mono, ed.rate, h, crossfade_ms=20.0, hold_s=0.3)
    check("the renderer handles real material", real.size > 0 and np.all(np.isfinite(real)))

    # Copying loop points to a linked member.
    if member is not None:
        take = ed.take_for(member.filename)
        ref_entry = next(e for e in ed.entries if e.filename == take.reference)
        source = sorted(ref_entry.hits, key=lambda x: x.start)[1]
        source.loop_start, source.loop_end, source.release_start = (
            source.start + 100, source.start + 900, source.start + 900)

        ed.file_index = ed.entries.index(member)
        ed.load_audio()
        ed.hit_index = 1
        ed.copy_loop_from_reference(whole_file=False)
        mine1 = sorted(ed.hits, key=lambda x: x.start)[1]
        check("m in loop mode copies the reference's loop points exactly",
              (mine1.loop_start, mine1.loop_end) == (source.loop_start, source.loop_end))

        ed.file_index = ed.entries.index(next(e for e in ed.entries
                                              if e.filename == start_file))
        ed.load_audio()

    ed.toggle_loop_mode()
    check("L turns loop mode off again", not ed.loop_mode)

    ed.hits[0].locked = True
    locked_start = ed.hits[0].start
    ed.save()
    check("save clears dirty", not ed.dirty)
    prev, _, _ = ml.load_state(state_path)
    entries2, _, _, _ = ml.analyse_folder(folder, ml.DetectParams(), previous=prev)
    same = [e for e in entries2 if e.filename == start_file][0]
    check("locked start survives re-detection",
          any(h.start == locked_start and h.locked for h in same.hits))
    check("count is still right after re-detection", len(same.hits) == same.expected)

    plt.close(ed.fig)
    shutil.rmtree(scratch, ignore_errors=True)
    print(f"\nselftest: {checks - len(failures)}/{checks} checks passed")
    for f in failures:
        print(f"  FAILED: {f}")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", nargs="?",
                    help="folder holding the take wav files "
                         "(omit it to be asked in a dialog)")
    ap.add_argument("--state", help="state file (default <folder>/../mapping_state.json)")
    ap.add_argument("--mapping", help="mapping.xml offered on quit "
                                      "(default <folder>/../mapping.xml)")
    ap.add_argument("--from-state", action="store_true",
                    help="open the saved state without re-detecting")
    ap.add_argument("--selftest", action="store_true",
                    help="exercise the edit logic headlessly and exit")
    args = ap.parse_args()

    chosen = args.folder or pick_folder()
    if not chosen:
        raise SystemExit("no folder given (and no dialog available)")
    folder = os.path.abspath(chosen)
    if not os.path.isdir(folder):
        raise SystemExit(f"not a folder: {folder}")
    state_path = args.state or os.path.join(os.path.dirname(folder), "mapping_state.json")
    mapping_path = args.mapping or os.path.join(os.path.dirname(folder), "mapping.xml")

    if args.selftest:
        raise SystemExit(selftest(folder, state_path))

    editor = build_editor(folder, state_path, args.from_state, ml.DetectParams())
    editor.run()

    if not finish_dialog(editor, mapping_path) and editor.dirty:
        print("quit with unsaved changes; nothing was written")


if __name__ == "__main__":
    main()
