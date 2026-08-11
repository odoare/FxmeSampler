"""
Batch driver for the mapping tools: detect strokes, report on the result, and
render QC images so the whole kit can be eyeballed at once.

    python3 Tools/mapping_build.py Kits/BlackWidow/data/wav --report
    python3 Tools/mapping_build.py Kits/BlackWidow/data/wav --report --save
    python3 Tools/mapping_build.py Kits/BlackWidow/data/wav --qc mapping_qc

Nothing here writes mapping.xml. The state file it saves (data/mapping_state.json
by default) is the authoring record: hit lists, lock flags, and the parameters
that produced them.

    Author: Olivier Doare, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
"""

import os
import sys
import glob
import tempfile
import argparse

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mapping_lib as ml


def onset_quality_db(mono, sample_rate, start, span_ms=5.0):
    """Level just before the start against level just after, in dB. A clean
    onset sits well below zero. Near zero means the start landed inside the
    stroke or inside the tail of the previous one."""
    n = max(1, int(span_ms * sample_rate / 1000.0))
    before = np.abs(mono[max(0, start - n):start])
    after = np.abs(mono[start:start + n])
    if before.size == 0 or after.size == 0 or after.max() <= 0:
        return float("nan")
    return 20.0 * np.log10(max(float(before.max()), 1e-9) / float(after.max()))


def report(folder, entries, takes, p):
    print(f"\nfolder: {folder}")
    print(f"files:  {len(entries)}   takes: {len(takes)}\n")

    header = f"{'file':44s} {'exp':>3s} {'got':>3s} {'z0':>3s} {'onset dB':>9s}  notes"
    print(header)
    print("-" * len(header))

    problems = []
    for e in sorted(entries, key=lambda x: x.filename):
        path = os.path.join(folder, e.filename)
        data, _, rate = ml.read_wav(path)
        mono = ml.detection_channel(data)
        q = [onset_quality_db(mono, rate, h.start) for h in e.hits if h.start > 0]
        med = float(np.median(q)) if q else float("nan")
        worst = float(np.max(q)) if q else float("nan")

        note = "; ".join(e.warnings)
        if len(e.hits) != e.expected:
            problems.append(e.filename)
        elif not np.isnan(worst) and worst > -6.0:
            note = note or f"one onset only {worst:.0f} dB above its own floor"
            problems.append(e.filename)

        print(f"{e.filename[:44]:44s} {e.expected:3d} {len(e.hits):3d} "
              f"{'Y' if e.starts_at_zero else 'n':>3s} {med:9.1f}  {note}")

    print("\nlinked takes")
    print("-" * 60)
    if not takes:
        print("  (none proposed)")
    for t in sorted(takes, key=lambda x: x.name):
        flag = " " if t.status.startswith("linked") else "!"
        print(f" {flag} {t.name:14s} ref={t.reference}")
        for m in t.members:
            if m != t.reference:
                print(f"   {'':14s}     {m}")
        print(f"   {'':14s} {t.status}")

    # Loop points, when a kit has any. Silent for the drum kits, which have
    # none, rather than printing an empty section on every run.
    looped = [(e, h) for e in entries for h in e.hits if h.has_loop]
    bad_loops = [(e, h, w) for e in entries for h in e.hits
                 for w in [ml.loop_warnings(h)] if w]
    if looped or bad_loops:
        print("\nloop points")
        print("-" * 60)
        by_file = {}
        for e, h in looped:
            by_file.setdefault(e.filename, []).append(h)
        for name, hits in sorted(by_file.items()):
            spans = ", ".join(f"L{h.rank + 1} {h.loop_length} sa" for h in hits)
            print(f"  {name[:44]:44s} {len(hits)} looped: {spans}")
        for e, h, warns in bad_loops:
            for w in warns:
                print(f"  !! {e.filename} layer {h.rank + 1}: {w}")

    print()
    if problems:
        print(f"{len(problems)} file(s) need a look: " + ", ".join(problems))
    else:
        print("all files detected the expected number of strokes")
    if bad_loops:
        print(f"{len(bad_loops)} stroke(s) have loop points the engine would clamp")
    bad_links = [t.name for t in takes if not t.status.startswith("linked")]
    if bad_links:
        print("rejected links: " + ", ".join(bad_links))
    print()


def contact_sheet(folder, entries, p, outdir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    os.makedirs(outdir, exist_ok=True)
    for e in sorted(entries, key=lambda x: x.filename):
        path = os.path.join(folder, e.filename)
        data, _, rate = ml.read_wav(path)
        mono = ml.detection_channel(data)

        fig, (ax_full, ax_zoom) = plt.subplots(
            2, 1, figsize=(14, 6), gridspec_kw={"height_ratios": [2, 1]})

        # Whole file as a peak envelope in dB, so a 60 dB decay is visible.
        blk = max(1, int(0.002 * rate))
        n = len(mono) // blk
        env = np.abs(mono[:n * blk]).reshape(n, blk).max(axis=1)
        t = np.arange(n) * blk / rate
        ax_full.plot(t, 20 * np.log10(np.maximum(env, 1e-6)), lw=0.5, color="0.55")
        for h in e.hits:
            ax_full.axvline(h.start / rate, color="crimson", lw=0.9, alpha=0.9)
        ax_full.set_ylim(-100, 5)
        ax_full.set_xlim(0, max(t[-1] if n else 1.0, 1e-3))
        ax_full.set_ylabel("dB peak")
        ax_full.set_title(f"{e.filename}    expected {e.expected}, found {len(e.hits)}"
                          f"{'    (first stroke at sample 0)' if e.starts_at_zero else ''}")

        # Every stroke overlaid around its own onset. A misplaced start shows up
        # immediately as a curve that does not rise with the others.
        pre, post = int(0.005 * rate), int(0.015 * rate)
        tt = (np.arange(-pre, post) / rate) * 1000.0
        for h in e.hits:
            lo, hi = h.start - pre, h.start + post
            if lo < 0 or hi > len(mono):
                continue
            seg = np.abs(mono[lo:hi])
            m = seg.max()
            if m > 0:
                ax_zoom.plot(tt, seg / m, lw=0.7, alpha=0.75)
        ax_zoom.axvline(0.0, color="crimson", lw=1.0)
        ax_zoom.set_xlabel("ms relative to detected onset")
        ax_zoom.set_ylabel("normalised")
        ax_zoom.set_xlim(tt[0], tt[-1])

        fig.tight_layout()
        out = os.path.join(outdir, os.path.splitext(e.filename)[0] + ".png")
        fig.savefig(out, dpi=90)
        plt.close(fig)
        print(f"  wrote {out}")


def synth_take(n_strokes, sample_rate, lead_ms, gap_ms=800.0, seed=0):
    """A synthetic take: exponentially decaying noise bursts of rising level,
    optionally preceded by room tone. Returns (mono, true_starts)."""
    rng = np.random.default_rng(seed)
    lead = int(lead_ms * sample_rate / 1000.0)
    gap = int(gap_ms * sample_rate / 1000.0)
    total = lead + gap * n_strokes
    out = rng.standard_normal(total).astype(np.float32) * 1e-4      # room tone

    starts = []
    for i in range(n_strokes):
        start = lead + i * gap
        starts.append(start)
        length = min(gap - 1, int(0.6 * sample_rate))
        env = np.exp(-np.arange(length) / (0.08 * sample_rate))
        level = 0.05 + 0.9 * i / max(1, n_strokes - 1)              # crescendo
        burst = rng.standard_normal(length).astype(np.float32) * env * level
        out[start:start + length] += burst
    return out, starts


def selftest():
    """Detection against takes whose true onsets are known by construction.

    The lead-in cases are the point: a take that starts a few frames into the
    file put its first stroke where the flux baseline cancelled it and where
    find_peaks structurally could not report it, and because N peaks are taken
    regardless, the count still came out right while the strokes were wrong."""
    import tempfile
    from scipy.io import wavfile

    sample_rate = 44100
    cases = [(n, lead) for lead in (0, 10, 20, 50, 500, 2000) for n in (4, 10)]
    # Lead-in shorter than one analysis window cannot be resolved, and the
    # first stroke is pinned to 0 instead. See begins_inside_a_hit.
    window_ms = ml.DetectParams().n_fft / sample_rate * 1000.0
    checks, failures = 0, []

    def check(label, ok):
        nonlocal checks
        checks += 1
        if not ok:
            failures.append(label)

    with tempfile.TemporaryDirectory() as folder:
        truth = {}
        for i, (n, lead) in enumerate(cases):
            mono, starts = synth_take(n, sample_rate, lead, seed=i)
            name = f"{n} G{i} F{i} 0 60 60 60.wav"
            wavfile.write(os.path.join(folder, name), sample_rate, mono)
            truth[name] = (starts, lead)

        entries, takes, p, _ = ml.analyse_folder(folder, link=False)
        check("every synthetic file was picked up", len(entries) == len(cases))

        tol = int(0.004 * sample_rate)      # 4 ms
        for e in entries:
            starts, lead = truth[e.filename]
            label = f"n={len(starts)} lead={lead}ms"

            check(f"{label}: stroke count", len(e.hits) == len(starts))
            if len(e.hits) != len(starts):
                continue

            found = sorted(h.start for h in e.hits)
            if lead < window_ms:
                check(f"{label}: first stroke pinned to 0", found[0] == 0)
                pairs = list(zip(found[1:], starts[1:]))
            else:
                pairs = list(zip(found, starts))
            worst = max((abs(f - t) for f, t in pairs), default=0)
            check(f"{label}: onsets within 4 ms (worst {worst})", worst <= tol)

            # The bursts rise monotonically, so the velocity ladder must too.
            by_time = sorted(e.hits, key=lambda h: h.start)
            check(f"{label}: velocity ladder follows the crescendo",
                  [h.rank for h in by_time] == sorted(h.rank for h in by_time))

    # ── the writer, where loop points meet a mapping that predates them ────
    #
    # The guarantee under test is asymmetric on purpose. A stroke with no loop
    # points must produce the line it always produced, byte for byte, or every
    # existing kit's diff fills with noise. A stroke with them must gain the
    # attributes even though no template has them to replace.
    with tempfile.TemporaryDirectory() as folder:
        template = [
            '<?xml version="1.0" encoding="UTF-8"?>',
            "<Mapping>",
            '  <SampleGroup name="G" channels="0,1" oneShot="true"/>',
            '  <Sound name="F" group="G" resource="a.wav" basePitch="60" noteLow="60"'
            ' noteHigh="60" velLow="0" velHigh="127" sampleStart="0" sampleEnd="1000"/>',
            "</Mapping>",
        ]
        src = os.path.join(folder, "mapping.xml")
        ml._write_text(src, template, "\n", True)

        entry = ml.FileEntry(filename="a.wav", expected=2, group="G", family="F",
                             mute_group=0, base_pitch=60, note_low=60, note_high=60,
                             n_frames=2000)
        entry.hits = [ml.Hit(start=0, end=1000, rank=0),
                      ml.Hit(start=1000, end=2000, rank=1)]

        plain = os.path.join(folder, "plain.xml")
        ml.write_mapping_xml(src, entry_list := [entry], plain)
        written = open(plain).read()
        check("a stroke with no loop emits no loop attributes",
              "loopStart" not in written and "releaseStart" not in written)

        entry.hits[0].loop_start = 400
        entry.hits[0].loop_end = 900
        entry.hits[0].release_start = 950
        looped = os.path.join(folder, "looped.xml")
        ml.write_mapping_xml(src, entry_list, looped)
        text = open(looped).read()
        check("loop attributes are inserted into a template that lacks them",
              'loopStart="400"' in text and 'loopEnd="900"' in text
              and 'releaseStart="950"' in text)
        check("only the stroke that has them gets them", text.count("loopStart") == 1)
        check("the insertion stays inside the element",
              'releaseStart="950"/>' in text)

        # Read back, write again: a mapping that already carries loop points has
        # to survive the trip untouched, or a second export would churn the file.
        entries2 = [ml.FileEntry(filename="a.wav", expected=2, group="G", family="F",
                                 mute_group=0, base_pitch=60, note_low=60,
                                 note_high=60, n_frames=2000)]
        ml.seed_entries_from_mapping(entries2, looped)
        check("loop points survive being read back",
              (entries2[0].hits[0].loop_start, entries2[0].hits[0].loop_end,
               entries2[0].hits[0].release_start) == (400, 900, 950))
        again = os.path.join(folder, "again.xml")
        ml.write_mapping_xml(looped, entries2, again)
        check("a mapping with loop points round-trips byte-identically",
              open(looped, "rb").read() == open(again, "rb").read())

        # Clearing must remove them, not leave a stale loopStart behind.
        entries2[0].hits[0].loop_start = -1
        entries2[0].hits[0].loop_end = -1
        entries2[0].hits[0].release_start = -1
        cleared = os.path.join(folder, "cleared.xml")
        ml.write_mapping_xml(looped, entries2, cleared)
        check("clearing a loop removes the attributes",
              "loopStart" not in open(cleared).read())
        check("and gives back the line it started with",
              open(cleared, "rb").read() == open(plain, "rb").read())

        bad = ml.Hit(start=100, end=900, loop_start=50, loop_end=1000, release_start=40)
        problems = ml.loop_warnings(bad)
        check("out-of-order loop points are reported", len(problems) == 3)
        check("a legal loop reports nothing",
              ml.loop_warnings(ml.Hit(start=0, end=1000, loop_start=100,
                                      loop_end=900, release_start=900)) == [])

    print(f"\ndetection selftest: {checks - len(failures)}/{checks} checks passed")
    for f in failures:
        print(f"  FAILED: {f}")
    return 1 if failures else 0


def verify_roundtrip(folder, mapping_path):
    """Seeds the state from mapping.xml and writes it straight back out. If the
    writer is faithful the result is byte-identical, which is what makes a later
    diff meaningful: anything that moves, moved because a hit moved.

    No audio is read, so this is instant."""
    if not os.path.exists(mapping_path):
        print(f"no mapping to verify: {mapping_path}")
        return 1

    entries = [e for e in (ml.parse_filename(p)
                           for p in sorted(glob.glob(os.path.join(folder, "*.wav"))))
               if e is not None]

    lines, _, _ = ml._read_text(mapping_path)
    blocks = ml.sound_blocks(lines)
    try:
        ml.seed_entries_from_mapping(entries, mapping_path)
    except ml.UncutMapping as exc:
        print(exc)
        return 1
    covered = {e.filename for e in entries if e.hits}
    regenerated = [b for b in blocks if b[3] in covered]

    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as tmp:
        out = tmp.name
    try:
        notes = ml.write_mapping_xml(mapping_path, entries, out)
        before = open(mapping_path, "rb").read()
        after = open(out, "rb").read()
        identical = before == after

        print(f"mapping:      {mapping_path}")
        print(f"<Sound> runs: {len(blocks)} in file, {len(regenerated)} regenerated from state")
        for n in notes:
            print(f"  {n}")
        untouched = len(blocks) - len(regenerated)
        if untouched:
            print(f"  {untouched} run(s) had no matching wav and were passed through")

        if identical and len(regenerated) == len(blocks):
            print(f"\nround-trip is byte-identical ({len(before)} bytes, "
                  f"every run rewritten from state)")
            return 0
        if identical:
            print("\nfile is unchanged, but not every run was rewritten from state, "
                  "so this does not prove much")
            return 1

        import difflib
        diff = list(difflib.unified_diff(before.decode().split("\n"),
                                         after.decode().split("\n"),
                                         "before", "after", n=0, lineterm=""))
        print(f"\nround-trip DIFFERS ({len(diff)} diff lines):")
        for line in diff[:40]:
            print("  " + line)
        return 1
    finally:
        os.unlink(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", nargs="?", help="folder holding the take wav files")
    ap.add_argument("--selftest", action="store_true",
                    help="run detection against synthetic takes with known onsets")
    ap.add_argument("--state", help="state file (default <folder>/../mapping_state.json)")
    ap.add_argument("--report", action="store_true", help="print the analysis report")
    ap.add_argument("--qc", metavar="DIR", help="write QC images to DIR")
    ap.add_argument("--save", action="store_true", help="write the state file")
    ap.add_argument("--from-state", action="store_true",
                    help="require the state file (it is used by default when present)")
    ap.add_argument("--redetect", action="store_true",
                    help="analyse the audio again instead of using the state file")
    ap.add_argument("--pre-roll-ms", type=float, default=None,
                    help="silence to leave in front of each onset (default 0)")
    ap.add_argument("--mapping", help="mapping.xml (default <folder>/../mapping.xml)")
    ap.add_argument("--seed-from-xml", action="store_true",
                    help="take hits from mapping.xml instead of detecting them")
    ap.add_argument("--export", metavar="DEST",
                    help="write mapping.xml to DEST, regenerating only <Sound> lines")
    ap.add_argument("--verify", action="store_true",
                    help="seed from mapping.xml, re-export, and prove the file is unchanged")
    ap.add_argument("--no-link", action="store_true",
                    help="rank every file on its own instead of sharing a take's ordering")
    ap.add_argument("--align-members", dest="align_members", action="store_true",
                    default=None,
                    help="start each linked take's members on the reference's exact "
                         "samples, preserving the recorded delay between mic sets")
    ap.add_argument("--no-align-members", dest="align_members", action="store_false",
                    help="start every file on its own attack foot (the default)")
    args = ap.parse_args()

    if args.selftest:
        raise SystemExit(selftest())

    if not args.folder:
        ap.error("a folder is required (or use --selftest)")
    folder = os.path.abspath(args.folder)
    if not os.path.isdir(folder):
        raise SystemExit(f"not a folder: {folder}")

    state_path = args.state or os.path.join(os.path.dirname(folder), "mapping_state.json")
    mapping_path = args.mapping or os.path.join(os.path.dirname(folder), "mapping.xml")

    if args.verify:
        raise SystemExit(verify_roundtrip(folder, mapping_path))

    # The saved state is the authoring record, so it wins whenever it exists.
    # Re-detecting has to be asked for, because it recomputes every stroke that
    # is not locked and would quietly undo an editing session.
    have_state = os.path.exists(state_path)
    use_state = (args.from_state or have_state) and not args.redetect

    if use_state and not have_state:
        raise SystemExit(f"no state file to read: {state_path}")

    if use_state:
        entries, takes, p = ml.load_state(state_path)
        hits = sum(len(e.hits) for e in entries)
        locked = sum(1 for e in entries for h in e.hits if h.locked)
        print(f"source: {state_path}")
        print(f"        {len(entries)} files, {hits} strokes, {locked} locked "
              f"(pass --redetect to analyse the audio again)")
        for flag, name in ((args.pre_roll_ms, "--pre-roll-ms"),
                           (args.align_members, "--align-members")):
            if flag is not None:
                print(f"        {name} ignored: it only affects detection "
                      "(add --redetect to apply it)")
    else:
        previous = None
        p = ml.DetectParams()
        if have_state:
            try:
                previous, _, saved = ml.load_state(state_path)
                p = saved            # reuse the parameters the state was made with
                print(f"reusing detection parameters from {state_path}")
            except Exception as exc:
                print(f"warning: could not read locked hits from {state_path}: {exc}")
        if args.pre_roll_ms is not None:
            p.pre_roll_ms = args.pre_roll_ms
        if args.align_members is not None:
            p.align_members = args.align_members
        print(f"source: fresh detection of {folder}"
              + ("  (members aligned to their reference)" if p.align_members else ""))
        entries, takes, p, link_notes = ml.analyse_folder(
            folder, p, link=not args.no_link, previous=previous)
        for n in link_notes:
            print(f"  {n}")

    if not entries:
        raise SystemExit("no conventionally named wav files found")

    if args.seed_from_xml:
        try:
            missing = ml.seed_entries_from_mapping(entries, mapping_path)
        except ml.UncutMapping as exc:
            raise SystemExit(str(exc))
        print(f"seeded hits from {mapping_path}")
        for m in missing:
            print(f"  no <Sound> block for {m}, keeping detected hits")

    if args.report or not (args.qc or args.save or args.export):
        report(folder, entries, takes, p)

    if args.qc:
        outdir = os.path.abspath(args.qc)
        # data/ is globbed wholesale into the plugin binary by the kit
        # CMakeLists, so QC images must never be written under it.
        if os.sep + "data" + os.sep in outdir + os.sep:
            raise SystemExit(f"refusing to write QC images under a data/ folder: {outdir}\n"
                             "everything in data/ is embedded in the plugin.")
        contact_sheet(folder, entries, p, outdir)

    if args.export:
        template = mapping_path
        if not os.path.exists(mapping_path):
            # Nothing to carry across, so lay down a scaffold and write into it.
            # The routing parts are guesses marked TODO; the sample groups and
            # channel assignments do follow from the file names.
            groups = ml.create_scaffold(entries, args.export)
            template = args.export
            print(f"no {os.path.basename(mapping_path)} found, generated a scaffold "
                  f"with {len(groups)} sample group(s): {', '.join(groups)}")
            print("  every TODO in it needs a real value before the kit will look right")

        ml.refresh_link_status(entries, takes, p)
        broken = [t for t in takes if not t.status.startswith("linked")]
        for t in broken:
            print(f"  warning: take {t.name} is not verified ({t.status}); "
                  "its files were ranked independently")

        notes = ml.write_mapping_xml(template, entries, args.export)
        print(f"wrote {args.export} (template {template})")
        for n in notes:
            # Every block of a fresh scaffold grows from its single placeholder,
            # which is not news.
            if template == args.export and "velocity layers" in n:
                continue
            print(f"  {n}")

    if args.save:
        ml.save_state(state_path, entries, takes, p)
        print(f"wrote {state_path}")


if __name__ == "__main__":
    main()
