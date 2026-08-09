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

KEYS = """
  n / p        next / previous file        right / left  next / previous stroke
  . / ,        nudge 1 ms later / earlier  > / <         nudge 10 ms
  click        top pane: select nearest    zoom pane: move the selected start
  a            add a stroke at the last click
  d  r  u      delete / re-snap / unlock the selected stroke
  m / M        put the selected stroke / every stroke on the exact sample used
               by this take's reference (master) file
  f            toggle the full-file pane   s  save state   q  quit

  markers: red detected, orange dashed locked, blue selected,
           green dotted where the reference says the stroke is
"""


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

    def unlock_selected(self):
        if self.hit is None:
            return
        self.hit.locked = False
        self.dirty = True
        self.message = "unlocked, detection will own it again"

    def nudge(self, ms):
        if self.hit is None:
            return
        self.move_selected(self.hit.start + int(ms * self.rate / 1000.0))
        self.message = f"nudged {ms:+g} ms"

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

    def draw(self):
        e = self.entry
        self.ax_full.clear()
        self.ax_zoom.clear()

        if self.show_full:
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
        if h is not None:
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

        actions = {
            "n": lambda: self.step_file(+1),
            "p": lambda: self.step_file(-1),
            "right": lambda: self.step_hit(+1),
            "left": lambda: self.step_hit(-1),
            ".": lambda: self.nudge(+1.0),
            ",": lambda: self.nudge(-1.0),
            ">": lambda: self.nudge(+10.0),
            "<": lambda: self.nudge(-10.0),
            "d": self.delete_selected,
            "r": self.refine_selected,
            "u": self.unlock_selected,
            "m": lambda: self.align_to_reference(whole_file=False),
            "M": lambda: self.align_to_reference(whole_file=True),
            "s": self.save,
            "f": lambda: setattr(self, "show_full", not self.show_full),
        }
        if k == "a":
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
