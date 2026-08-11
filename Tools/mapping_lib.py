"""
Shared library for building a FxmeSampler mapping.xml from multi-hit take files.

The four ideas it is built on:

  * Onsets come from spectral flux (the frame-to-frame increase in log
    magnitude) rather than from an absolute level gate. Flux reacts to
    increases in spectral energy, so a decaying cymbal contributes nothing and
    a stroke landing on a ringing tail is still detected. A gate has to wait
    for the signal to fall below a threshold and stay quiet, which cymbals,
    hi-hats and toms never do in time.

  * The filename already states how many hits there are, so the N strongest
    flux peaks are taken directly. There is no threshold to sweep.

  * The onset is then refined to the attack foot at sample resolution, by
    walking back from the attack peak along the analytic-signal envelope. The
    generation of tooling this replaced smeared the envelope with a centred
    window and subtracted a further millisecond, placing every start a
    systematic 101 samples (2.3 ms) early.

  * Files recorded from the same take through different microphone sets (the
    Ambix and Prox pairs) are linked, so a velocity layer means the same
    physical stroke in every one of them.

No UI and no XML writing lives here. See mapping_build.py, and README.md for
the workflow and the reasoning.

    Author: Olivier Doare, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
"""

import os
import re
import glob
import json
import math
import wave
from dataclasses import dataclass, field, asdict

import numpy as np

try:
    import scipy.signal
    import scipy.ndimage
except ImportError:  # pragma: no cover
    raise SystemExit("scipy is required: pip install scipy")

try:
    from scipy.io import wavfile as _scipy_wav
    HAS_SCIPY_IO = True
except ImportError:
    HAS_SCIPY_IO = False


# Bump this only for a change a v1 reader would get wrong. Adding Hit fields
# with defaults is not one: a state written before loop points existed loads
# with them unset, which is exactly what it means.
STATE_VERSION = 1


class UncutMapping(Exception):
    """Raised for a mapping whose samples are already cut one per file, so
    there are no take files to slice."""


# ─── Parameters ────────────────────────────────────────────────────────────

@dataclass
class DetectParams:
    """Everything the detector can be tuned by. Saved into the state file so a
    result can always be reproduced, and so the editor can show what produced
    a given hit list."""

    # Spectral flux analysis.
    n_fft: int = 1024
    hop: int = 128

    # Running-median baseline subtracted from the flux, in seconds. Long
    # compared with a stroke, short compared with the gap between strokes.
    baseline_s: float = 0.5

    # Two strokes closer than this are never both accepted.
    min_gap_ms: float = 120.0

    # A take usually begins inside its first stroke (sample 0 is already in the
    # attack), which no onset detector can see. The head is compared with the
    # file's own noise floor rather than its peak, because the first stroke of
    # a crescendo can be 40 dB below the last.
    head_over_floor: float = 8.0
    # Used as a floor; the window actually examined is at least one FFT frame
    # long. See begins_inside_a_hit for why that length and not a shorter one.
    head_ms: float = 20.0

    # Onset refinement: where to look for the attack peak around the coarse
    # flux position, and how far back to walk to find the foot of the attack.
    peak_back_ms: float = 15.0
    peak_fwd_ms: float = 40.0
    foot_back_ms: float = 30.0
    smooth_ms: float = 0.5

    # The attack foot is the last sample below this level, taken as the larger
    # of a fraction of the stroke's own peak and a multiple of the file's noise
    # floor. The first term handles an isolated stroke, the second keeps a
    # noisy floor from counting as signal.
    foot_rel_peak: float = 0.02
    foot_rel_floor: float = 3.0

    # Subtracted from the refined onset. 0 puts the start exactly on the attack
    # foot; raise it if you would rather have a hair of silence in front.
    pre_roll_ms: float = 0.0

    # Window used to compare strokes against each other for velocity ordering.
    # Fixed length so a stroke is not rated by how long its segment happens to
    # be (the old code summed to the next onset, so the last stroke of a file
    # was measured over whatever was left and sorted wrongly).
    energy_ms: float = 200.0

    # Put every member of a verified take on the exact samples its reference
    # uses, instead of on each file's own attack foot. The files come from one
    # multitrack, so equal starts reproduce the recording's natural arrival
    # delay between a close mic and a distant array at trigger time; per-file
    # feet make both attacks land together and discard it.
    align_members: bool = False

    # How far one stroke may depart from a take's constant offset before the
    # link is rejected. The offset itself is flight time between fixed points,
    # so it does not vary; what varies is onset-detection error, which on this
    # material sits under 4 ms even at its worst.
    link_tolerance_ms: float = 8.0


# ─── Data model ────────────────────────────────────────────────────────────

@dataclass
class Hit:
    start: int
    end: int
    rms: float = 0.0
    peak: float = 0.0
    locked: bool = False      # set by the editor; re-detection must not move it
    rank: int = 0             # position in the velocity ladder, 0 = softest

    # Playback regions, as absolute sample indices like start and end. -1 means
    # unset, which is what a one-shot kit uses and what the engine reads as
    # "loopStart = sampleStart, loopEnd = sampleEnd, releaseStart = loopEnd".
    # Left out of the mapping entirely when unset, so a drum kit's Sound lines
    # stay byte for byte what they were.
    loop_start: int = -1
    loop_end: int = -1
    release_start: int = -1

    @property
    def length(self):
        return self.end - self.start

    @property
    def has_loop(self):
        return self.loop_start >= 0 and self.loop_end > self.loop_start

    @property
    def loop_length(self):
        return self.loop_end - self.loop_start if self.has_loop else 0


@dataclass
class FileEntry:
    filename: str
    expected: int
    group: str
    family: str
    mute_group: int
    base_pitch: int
    note_low: int
    note_high: int
    n_channels: int = 0
    sample_rate: int = 0
    n_frames: int = 0
    starts_at_zero: bool = False
    hits: list = field(default_factory=list)
    warnings: list = field(default_factory=list)


@dataclass
class Take:
    """A set of files recorded simultaneously through different mic sets."""
    name: str
    reference: str
    members: list = field(default_factory=list)
    locked: bool = False
    status: str = ""


# ─── Filename convention ───────────────────────────────────────────────────

def parse_filename(filename):
    """NumHits Group Family MuteGroup BasePitch NoteLow NoteHigh.wav

    Returns a FileEntry with no audio fields filled in, or None if the name
    does not follow the convention."""
    stem = os.path.splitext(os.path.basename(filename))[0]
    parts = stem.split(" ")
    if len(parts) < 7:
        return None
    try:
        return FileEntry(
            filename=os.path.basename(filename),
            expected=int(parts[0]),
            group="_".join(parts[1:-5]),
            family=parts[-5],
            mute_group=int(parts[-4]),
            base_pitch=int(parts[-3]),
            note_low=int(parts[-2]),
            note_high=int(parts[-1]),
        )
    except ValueError:
        return None


# ─── Audio input ───────────────────────────────────────────────────────────

def read_wav(filepath):
    """Returns (data, n_channels, sample_rate) with data as float32 in -1..1,
    shaped (n,) for mono or (n, channels) otherwise. Handles the 24-bit files
    this kit is recorded in."""
    if HAS_SCIPY_IO:
        try:
            import warnings
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                rate, data = _scipy_wav.read(filepath)
            n_ch = 1 if data.ndim == 1 else data.shape[1]
            if data.dtype == np.int16:
                data = data.astype(np.float32) / 32768.0
            elif data.dtype == np.int32:
                data = data.astype(np.float32) / 2147483648.0
            elif data.dtype == np.uint8:
                data = (data.astype(np.float32) - 128.0) / 128.0
            elif np.issubdtype(data.dtype, np.integer):
                data = data.astype(np.float32) / 2147483648.0
            else:
                data = data.astype(np.float32)
            return data, n_ch, int(rate)
        except Exception:
            pass

    with wave.open(filepath, "rb") as wf:
        n_ch = wf.getnchannels()
        width = wf.getsampwidth()
        rate = wf.getframerate()
        raw = wf.readframes(wf.getnframes())

    if width == 2:
        data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    elif width == 1:
        data = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
    elif width == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        b = np.zeros((a.shape[0], 4), dtype=np.uint8)
        b[:, 1:] = a                       # 24-bit value shifted into an int32
        data = b.view(np.int32).astype(np.float32).ravel() / 2147483648.0
    elif width == 4:
        data = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
    else:
        raise ValueError(f"unsupported sample width {width} in {filepath}")

    if n_ch > 1:
        data = data.reshape(-1, n_ch)
    return data, n_ch, int(rate)


def detection_channel(data):
    """Channel 0. For the Ambix files that is W, the omnidirectional component,
    which is the right one to detect on. For the mono close mics it is the only
    one."""
    return data[:, 0] if data.ndim > 1 else data


# ─── Onset detection ───────────────────────────────────────────────────────

def flux_curve(mono, sample_rate, p):
    """Half-wave-rectified sum of the frame-to-frame increase in log magnitude.

    The log is taken on the raw magnitude (not a compressed proxy), so the
    curve measures relative change and a soft stroke on a quiet passage counts
    as much as a loud one. That is what makes a single detector work across the
    40 dB span of a crescendo take."""
    _, _, spec = scipy.signal.stft(mono, fs=sample_rate,
                                   nperseg=p.n_fft, noverlap=p.n_fft - p.hop,
                                   window="hann", boundary=None, padded=False)
    log_mag = np.log10(np.abs(spec) + 1e-7)
    return np.sum(np.maximum(np.diff(log_mag, axis=1), 0.0), axis=0)


def noise_floor(mono, sample_rate, block_ms=20.0):
    """10th percentile of the per-block peak level: the level the file sits at
    between strokes."""
    blk = max(1, int(block_ms * sample_rate / 1000.0))
    n = len(mono) // blk
    if n < 4:
        return 0.0
    levels = np.abs(mono[:n * blk]).reshape(n, blk).max(axis=1)
    positive = levels[levels > 0]
    return float(np.percentile(positive, 10)) if positive.size else 0.0


def begins_inside_a_hit(mono, sample_rate, p):
    """True when the take's first stroke has no detectable onset, so it has to
    be placed at sample 0 by hand.

    The window examined is one FFT frame, not some shorter "is sample 0 loud"
    span, because that is exactly the reach of the limitation. Spectral flux
    compares each frame with the one before it, so an onset falling inside the
    very first frame has nothing to be contrasted against and produces almost
    no flux at all. A take whose first stroke arrives within one window of the
    start is therefore indistinguishable from one that begins mid-stroke, and
    both are handled the same way.

    The cost is that up to one window (23 ms at 44.1 kHz with n_fft 1024) of
    lead-in gets swallowed into the first sample range. That is a negligible
    amount of room tone, and it fails safe: the alternative is missing the
    stroke entirely and silently promoting some other peak in its place.

    The level test is against the file's own noise floor rather than its peak,
    since the first stroke of a crescendo take can be 40 dB below the last."""
    span = max(1, int(p.head_ms * sample_rate / 1000.0), p.n_fft)
    head = np.abs(mono[:span])
    if head.size == 0:
        return False
    return float(head.max()) > p.head_over_floor * max(noise_floor(mono, sample_rate), 1e-9)


def detect_onsets(mono, sample_rate, expected, p):
    """Returns (refined_starts, diagnostics). Exactly `expected` starts unless
    the file simply does not contain that many candidate peaks."""
    flux = flux_curve(mono, sample_rate, p)

    kernel = max(3, int(p.baseline_s * sample_rate / p.hop) | 1)
    # Pad the running median with the flux's own median rather than with the
    # edge value. Edge replication copies flux[0] across half the window, so a
    # stroke close to the start of the file becomes its own baseline and
    # subtracts to zero: the first stroke of a take with a short lead-in
    # disappears, and since N peaks are taken regardless, a spurious one
    # elsewhere silently takes its place.
    baseline = scipy.ndimage.median_filter(flux, size=kernel, mode="constant",
                                           cval=float(np.median(flux)))
    detection = np.maximum(flux - baseline, 0.0)

    at_zero = begins_inside_a_hit(mono, sample_rate, p)
    n_search = expected - 1 if at_zero else expected

    search = detection.copy()
    if at_zero:
        # Skip the decay of the stroke sitting at sample 0.
        search[:int(0.20 * sample_rate / p.hop)] = 0.0

    gap_frames = max(1, int(p.min_gap_ms * sample_rate / 1000.0 / p.hop))
    # find_peaks wants a strictly lower neighbour on both sides, so it can never
    # return the first or last frame. A take with a short lead-in puts its first
    # stroke in frame 0, where it would be structurally invisible. Padding makes
    # both ends ordinary interior points.
    peaks, _ = scipy.signal.find_peaks(np.concatenate(([0.0], search, [0.0])),
                                       distance=gap_frames)
    peaks -= 1
    if peaks.size and n_search > 0:
        strongest = peaks[np.argsort(search[peaks])[::-1][:n_search]]
        chosen = np.sort(strongest)
    else:
        chosen = np.array([], dtype=int)

    # Frame i of the difference corresponds to STFT frame i+1.
    coarse = [int((c + 1) * p.hop) for c in chosen]
    if at_zero:
        coarse = [0] + coarse

    floor = noise_floor(mono, sample_rate)
    starts = []
    for c in coarse:
        s = refine_onset(mono, sample_rate, c, p, floor)
        if starts and s <= starts[-1]:
            s = starts[-1] + 1          # keep them strictly increasing
        starts.append(s)

    diag = {
        "at_zero": at_zero,
        "detection": detection,
        "flux": flux,
        "floor": floor,
        "n_peaks_available": int(peaks.size),
    }
    return starts, diag


def refine_onset(mono, sample_rate, coarse, p, floor=0.0):
    """Move a coarse flux position (hop resolution, and slightly late) to the
    foot of the attack at sample resolution.

    Walks back from the attack peak to the last sample still at the pre-stroke
    level. Two details matter:

      * The envelope is the analytic-signal magnitude, not the rectified
        waveform. A rectified low tom passes through zero twice per cycle, and
        those dips are deep enough to be mistaken for the foot.

      * The search is a backward one for the last quiet sample, not a minimum.
        Ahead of a clean stroke the envelope is flat digital silence, and the
        minimum of a flat run is its first sample, which would drag the onset
        to the far edge of the search window.

    When the stroke lands on a tail that never reaches the floor (a second
    crash over a ringing first), no sample qualifies and the envelope minimum
    is used instead, which is the right answer in that regime."""
    if coarse <= 0:
        return 0

    ms = sample_rate / 1000.0
    w0 = max(0, coarse - int(p.peak_back_ms * ms))
    w1 = min(len(mono), coarse + int(p.peak_fwd_ms * ms))
    if w1 - w0 < 2:
        return coarse

    window = np.abs(mono[w0:w1])
    peak = float(window.max())
    if peak <= 0.0:
        return coarse
    peak_idx = w0 + int(np.argmax(window))

    lo = max(0, peak_idx - int(p.foot_back_ms * ms))
    if peak_idx - lo < 8:
        return lo

    env = np.abs(scipy.signal.hilbert(mono[lo:peak_idx]))
    smooth = max(3, int(p.smooth_ms * ms) | 1)
    env = scipy.ndimage.uniform_filter1d(env, size=smooth, mode="nearest")

    threshold = max(p.foot_rel_peak * peak, p.foot_rel_floor * floor)
    quiet = np.flatnonzero(env < threshold)
    foot = lo + int(quiet[-1]) if quiet.size else lo + int(np.argmin(env))

    return max(0, foot - int(p.pre_roll_ms * ms))


# ─── Loudness ──────────────────────────────────────────────────────────────

def measure(mono, sample_rate, start, end, p):
    """RMS and peak over a fixed window from the onset, clipped to the segment.

    Fixed length is the point: it is what makes strokes comparable, and what
    stops the last stroke of a file from being rated over a different span from
    all the others."""
    span = min(end, start + int(p.energy_ms * sample_rate / 1000.0), len(mono))
    seg = mono[start:span]
    if seg.size == 0:
        return 0.0, 0.0
    return float(np.sqrt(np.mean(seg.astype(np.float64) ** 2))), float(np.abs(seg).max())


# ─── Loop points ───────────────────────────────────────────────────────────
#
# The engine is the authority here, not this file. doc/architecture.md states
# the region ordering, the crossfade formula and both gain laws, and render_loop
# below is a transcription of it. If the preview and the plugin ever disagree,
# that document decides which one is wrong.

MIN_RELEASE_FADE_MS = 5.0     # Voice::minReleaseFadeMs


def zero_crossings(mono, lo, hi, rising=None):
    """Indices i in [lo, hi) where the signal changes sign between i and i+1.

    rising=True keeps only negative-to-positive crossings, False only the other
    way, None keeps both."""
    lo = max(0, int(lo))
    hi = min(len(mono) - 1, int(hi))
    if hi <= lo:
        return np.empty(0, dtype=int)

    seg = mono[lo:hi + 1]
    neg = seg < 0.0
    cross = np.nonzero(neg[:-1] != neg[1:])[0]
    if rising is True:
        cross = cross[neg[cross]]
    elif rising is False:
        cross = cross[~neg[cross]]
    return cross + lo


def snap_to_zero_crossing(mono, sample, window, rising=True, bounds=None):
    """Nearest zero crossing to `sample` within +/- window samples.

    Both loop ends default to rising crossings, which is what makes a seam
    continuous for anything periodic: two points at the same phase of the cycle
    join without a step. Returns the sample unchanged when there is no crossing
    to snap to, which is the honest answer for a decayed tail.

    bounds restricts the search to a legal range. Callers with a range must pass
    it rather than clamping the result: a crossing found outside the range and
    then clamped lands on the boundary, which is not a crossing at all, so the
    snap would appear to have happened and quietly not have."""
    sample = int(sample)
    lo, hi = sample - window, sample + window
    if bounds is not None:
        lo, hi = max(lo, bounds[0]), min(hi, bounds[1])

    found = zero_crossings(mono, lo, hi, rising)
    if found.size == 0:
        return sample
    return int(found[np.argmin(np.abs(found - sample))])


def sounding_end(mono, hit, floor_db=-45.0):
    """The last sample of a stroke that is actually making a sound.

    A hit's end is the next stroke's start, which for sustained material is well
    past the end of the note: a two-second organ note in a two-and-a-half second
    slice has half a second of digital silence on the end. Anything that treats
    the slice as the sound puts loop points in that silence, where there is
    nothing to loop and no zero crossing to snap to either."""
    lo, hi = int(hit.start), int(min(hit.end, len(mono)))
    if hi <= lo:
        return hi

    seg = np.abs(mono[lo:hi])
    peak = float(seg.max())
    if peak <= 0.0:
        return hi

    above = np.nonzero(seg > peak * (10.0 ** (floor_db / 20.0)))[0]
    return lo + int(above[-1]) + 1 if above.size else hi


def default_loop_points(mono, hit, sample_rate, snap_ms=5.0):
    """A first guess at a loop for a stroke that has none.

    Deliberately unambitious: the second half of the sounding part of the
    stroke, ending short of its tail, with both ends snapped to rising zero
    crossings. It is a starting point to drag, not a proposal to trust; the real
    loop finder is a separate job and needs f0 to constrain the length to whole
    periods."""
    end = sounding_end(mono, hit)
    span = end - hit.start
    if span < 4:
        return hit.start, hit.end, hit.end

    loop_start = hit.start + int(span * 0.50)
    loop_end = hit.start + int(span * 0.90)

    window = max(1, int(snap_ms * sample_rate / 1000.0))
    loop_start = snap_to_zero_crossing(mono, loop_start, window)
    loop_end = snap_to_zero_crossing(mono, loop_end, window)

    # Snapping can cross the two over on very short strokes.
    if loop_end <= loop_start:
        loop_start = hit.start + int(span * 0.50)
        loop_end = hit.start + int(span * 0.90)

    return loop_start, loop_end, loop_end


def loop_warnings(hit):
    """The engine's ordering constraint, checked where it can still be fixed.

    The plugin clamps points back into order on load and logs it, so a mapping
    that trips any of these still plays; it just does not play what was
    authored."""
    out = []
    if not hit.has_loop:
        if hit.loop_start >= 0 or hit.loop_end >= 0:
            out.append(f"loopStart {hit.loop_start} / loopEnd {hit.loop_end} "
                       "is not a usable loop (need loopStart < loopEnd)")
        return out

    if hit.loop_start < hit.start:
        out.append(f"loopStart {hit.loop_start} is before sampleStart {hit.start}")
    if hit.loop_end > hit.end:
        out.append(f"loopEnd {hit.loop_end} is past sampleEnd {hit.end}")

    if hit.release_start >= 0:
        if hit.release_start < hit.loop_end:
            out.append(f"releaseStart {hit.release_start} is before loopEnd {hit.loop_end}")
        if hit.release_start > hit.end:
            out.append(f"releaseStart {hit.release_start} is past sampleEnd {hit.end}")

    return out


def max_crossfade_samples(hit):
    """What a crossfade can actually be, in samples: shorter than the loop, and
    no further back from loopStart than sampleStart. Voice::maxCrossfadeSamples."""
    if not hit.has_loop:
        return 0.0
    loop_len = float(hit.loop_end - hit.loop_start)
    if loop_len <= 1.0:
        return 0.0
    return max(0.0, min(loop_len - 1.0, float(hit.loop_start - hit.start)))


def _fade_gains(g, shape):
    """gainOut falls 1 to 0, gainIn rises 0 to 1. Voice::fadeGains."""
    if shape == "linear":
        return 1.0 - g, g
    angle = g * math.pi / 2.0
    return math.cos(angle), math.sin(angle)


def _read_interpolated(buf, position, wrap_at_loop, loop_start, loop_end):
    """Voice::readInterpolated. Silence outside the buffer; with wrap_at_loop the
    sample after loopEnd - 1 is loopStart, so the seam interpolates continuously."""
    pos = int(position)
    if pos < 0 or pos >= len(buf):
        return 0.0

    s0 = buf[pos]
    s1 = 0.0
    if wrap_at_loop and pos + 1 >= loop_end:
        s1 = buf[loop_start]
    elif pos + 1 < len(buf):
        s1 = buf[pos + 1]

    return s0 + (position - pos) * (s1 - s0)


def render_loop(mono, sample_rate, hit, crossfade_ms=0.0, shape="equalPower",
                hold_s=1.5, release_mode="loop", release_s=0.3, increment=1.0):
    """Play a hit the way the plugin would, for listening and for drawing.

    A transcription of Voice::renderNextBlock, not an approximation of it: the
    two heads, the seam crossfade, the wrap, the jump into the release region
    and its fade all behave as in Source/Sampler.cpp.

    What it leaves out is the envelope. Attack, decay and sustain are not
    applied, because the question this answers is whether the loop is clean, and
    an envelope on top only hides the seam. The ADSR release is applied in
    release_mode="loop", since there it is the whole mechanism.

    Returns float32, one channel."""
    start = int(hit.start)
    end = int(hit.end)
    loop_start = int(hit.loop_start) if hit.has_loop else start
    loop_end = int(hit.loop_end) if hit.has_loop else end
    release_start = int(hit.release_start) if hit.release_start >= 0 else loop_end

    looping = hit.has_loop
    loop_len = float(loop_end - loop_start)

    crossfade = 0.0
    if looping and crossfade_ms > 0.0:
        crossfade = min(crossfade_ms * 0.001 * sample_rate, max_crossfade_samples(hit))

    hold_n = int(hold_s * sample_rate)
    tail_n = int((max(release_s, (end - release_start) / sample_rate) + 0.05) * sample_rate)
    out = np.zeros(hold_n + tail_n, dtype=np.float32)

    position = float(start)
    loop_position = 0.0

    # Voice::Region, plus the envelope release that Loop mode uses instead.
    in_release_region = False
    env_releasing = False

    release_fade = 0.0
    envelope = 1.0
    env_step = 1.0 / max(1.0, release_s * sample_rate)

    written = 0
    for i in range(len(out)):
        # Note off.
        if i == hold_n and looping and not (in_release_region or env_releasing):
            if release_mode == "region" and release_start < end:
                loop_position = position
                position = float(release_start)
                in_release_region = True
                tail = float(end - release_start)
                asked = max(MIN_RELEASE_FADE_MS, crossfade_ms) * 0.001 * sample_rate
                release_fade = min(max(asked, 0.0), tail)
            else:
                env_releasing = True

        main_loops = looping and not in_release_region
        if not main_loops and int(position) >= end:
            break

        # Release jump fade, main head against the loop it left.
        gain_release, gain_loop, leaving = 1.0, 0.0, False
        if in_release_region and release_fade > 0.0:
            travelled = position - release_start
            if travelled < release_fade:
                gain_loop, gain_release = _fade_gains(
                    min(1.0, max(0.0, travelled / release_fade)), shape)
                leaving = True

        # The body head: the main one until the release region takes it over.
        body_playing = (not in_release_region) or leaving
        body_position = loop_position if in_release_region else position

        body_fading = False
        body_main, body_next = 1.0, 0.0
        if body_playing and looping and crossfade > 0.0:
            fade_from = loop_end - crossfade
            if body_position >= fade_from:
                body_main, body_next = _fade_gains(
                    min(1.0, max(0.0, (body_position - fade_from) / crossfade)), shape)
                body_fading = True

        def read_body():
            main = _read_interpolated(mono, body_position, looping and not body_fading,
                                      loop_start, loop_end)
            if not body_fading:
                return main
            behind = _read_interpolated(mono, body_position - loop_len, False,
                                        loop_start, loop_end)
            return body_main * main + body_next * behind

        if in_release_region:
            value = _read_interpolated(mono, position, False, loop_start, loop_end)
            if leaving:
                value = gain_release * value + gain_loop * read_body()
        else:
            value = read_body()

        if env_releasing:
            envelope -= env_step
            if envelope <= 0.0:
                break
            value *= envelope

        out[i] = value
        written = i + 1

        # advance
        position += increment
        if main_loops and position >= loop_end:
            over = position - loop_end
            position = (loop_start + math.fmod(over, loop_len)) if loop_len > 0 else loop_start
        if leaving:
            loop_position += increment
            if loop_position >= loop_end:
                over = loop_position - loop_end
                loop_position = ((loop_start + math.fmod(over, loop_len))
                                 if loop_len > 0 else loop_start)

    return out[:written]


def seam_view(mono, hit, sample_rate, crossfade_ms=0.0, shape="equalPower", span=400):
    """What the loop seam looks and sounds like, as three curves over the same
    x axis of samples either side of the wrap:

        heard     the rendered output, crossfade included
        naive     the plain concatenation, i.e. the seam with no crossfade
        ahead     what would have followed loopEnd had the loop not wrapped

    naive and ahead diverging at x=0 is the discontinuity; heard staying with
    ahead through the seam is the crossfade removing it."""
    if not hit.has_loop:
        return None

    x = np.arange(-span, span)
    naive = np.zeros(len(x), dtype=np.float32)
    ahead = np.zeros(len(x), dtype=np.float32)
    for i, dx in enumerate(x):
        naive[i] = _read_interpolated(mono, (hit.loop_end + dx) if dx < 0
                                      else (hit.loop_start + dx), False, 0, 0)
        ahead[i] = _read_interpolated(mono, hit.loop_end + dx, False, 0, 0)

    # The heard curve is position-driven, exactly as the engine's gains are, so
    # it needs no run-up: every sample is a function of where the head is.
    heard = np.zeros(len(x), dtype=np.float32)
    loop_len = float(hit.loop_end - hit.loop_start)
    crossfade = (min(crossfade_ms * 0.001 * sample_rate, max_crossfade_samples(hit))
                 if crossfade_ms > 0.0 else 0.0)
    for i, dx in enumerate(x):
        p = (hit.loop_end + dx) if dx < 0 else (hit.loop_start + dx)
        fading = crossfade > 0.0 and p >= hit.loop_end - crossfade and dx < 0
        value = _read_interpolated(mono, p, crossfade <= 0.0, hit.loop_start, hit.loop_end)
        if fading:
            g = min(1.0, max(0.0, (p - (hit.loop_end - crossfade)) / crossfade))
            a, b = _fade_gains(g, shape)
            value = a * value + b * _read_interpolated(mono, p - loop_len, False,
                                                       hit.loop_start, hit.loop_end)
        heard[i] = value

    return x, heard, naive, ahead


# ─── Finding a loop ────────────────────────────────────────────────────────
#
# The constraint that does the work here is pitch. A loop whose length is not a
# whole number of periods restarts the waveform at the wrong phase, and no
# crossfade hides that: the fade blends two stretches that disagree, so it
# cancels rather than joins, and the loop sounds thin and beats at the wrap
# rate. Constraining the length to k periods and then choosing k by how well the
# two ends actually match is the whole algorithm.


@dataclass
class LoopSuggestion:
    loop_start: int
    loop_end: int
    f0: float = 0.0            # Hz, 0 when pitch could not be established
    confidence: float = 0.0    # 0 to 1, from the YIN aperiodicity
    periods: float = 0.0       # loop length in periods of f0
    cost: float = 1.0          # 0 identical ends, 1 as different as the signal
    crossfade_ms: float = 0.0  # the window the ends were matched over
    seam_ratio: float = 0.0    # the step at the wrap, over the material's own
    note: str = ""


def estimate_f0(mono, sample_rate, start, end, fmin=30.0, fmax=1500.0,
                threshold=0.15, octave_margin=1.15):
    """Fundamental frequency by YIN, as (f0_hz, confidence).

    Returns (0.0, 0.0) when there is not enough material or nothing periodic.
    YIN rather than plain autocorrelation because autocorrelation's bias towards
    tau=0 makes it report octaves too high on material with strong harmonics,
    which for loop finding is the expensive kind of wrong: a loop one octave
    short is a loop half the length it should be."""
    lo_tau = max(2, int(sample_rate / fmax))
    hi_tau = int(sample_rate / fmin)

    seg = np.asarray(mono[int(start):int(end)], dtype=np.float64)
    # Two windows of the longest period, plus the window itself: less than this
    # and the longest lags are estimated from almost no data.
    n = len(seg) - hi_tau
    if n < hi_tau or n < 64:
        return 0.0, 0.0
    if not np.any(seg):
        return 0.0, 0.0

    window = seg[:n]
    target_energy = float(np.dot(window, window))
    if target_energy <= 0.0:
        return 0.0, 0.0

    # d(tau) = sum_j (x[j] - x[j+tau])^2, expanded so one correlation does it.
    squares = np.concatenate(([0.0], np.cumsum(seg * seg)))
    lagged_energy = squares[n + np.arange(hi_tau + 1)] - squares[np.arange(hi_tau + 1)]
    dot = np.correlate(seg[:n + hi_tau], window, mode="valid")[:hi_tau + 1]
    d = target_energy + lagged_energy - 2.0 * dot

    # Cumulative mean normalisation: this is what stops tau=0 winning.
    taus = np.arange(1, hi_tau + 1)
    running = np.cumsum(d[1:]) / taus
    dn = np.ones(hi_tau + 1)
    good = running > 0.0
    dn[1:][good] = d[1:][good] / running[good]

    search = dn[lo_tau:hi_tau + 1]
    if search.size == 0:
        return 0.0, 0.0

    # First dip below the threshold, not the deepest: the deepest is often an
    # octave down, and the first acceptable one is the true period.
    below = np.nonzero(search < threshold)[0]
    if below.size:
        tau = int(below[0]) + lo_tau
    else:
        # Nothing clean enough to cross the threshold, which is normal on a
        # short or decaying note. Falling back to the deepest dip is what makes
        # a detector report an octave too low: a sub-octave lag lines up whole
        # cycles as well as the true period does and often scores a shade
        # better. So take the shortest lag that scores nearly as well, not the
        # best one.
        best = float(np.min(search))
        near = np.nonzero(search <= best * octave_margin)[0]
        tau = int(near[0] if near.size else np.argmin(search)) + lo_tau

    # Settle into the bottom of whichever dip was chosen.
    while tau + 1 <= hi_tau and dn[tau + 1] < dn[tau]:
        tau += 1

    # Parabolic interpolation for a sub-sample period.
    if 1 <= tau < hi_tau:
        a, b, c = d[tau - 1], d[tau], d[tau + 1]
        denom = a - 2.0 * b + c
        if denom != 0.0:
            tau = tau + 0.5 * (a - c) / denom

    if tau <= 0:
        return 0.0, 0.0

    confidence = float(np.clip(1.0 - dn[int(round(tau))], 0.0, 1.0))
    return float(sample_rate) / float(tau), confidence


def sustain_region(mono, hit, sample_rate, attack_skip_ms=30.0):
    """The part of a stroke worth looping: past the attack, before the sound has
    gone. Looping across the attack would repeat the transient."""
    end = sounding_end(mono, hit)
    seg = np.abs(mono[int(hit.start):int(end)])
    if seg.size == 0:
        return int(hit.start), int(end)

    peak_at = int(hit.start) + int(np.argmax(seg))
    lo = peak_at + int(attack_skip_ms * sample_rate / 1000.0)
    return min(lo, max(int(hit.start), int(end) - 1)), int(end)


def _match_costs(mono, ends, target_start, width):
    """Normalised RMS difference between the `width` samples ending at each
    position in `ends` and the `width` samples ending at target_start + width.

    This is the quantity that decides both questions at once. At width 1 it is
    the height of the step at the seam; at the crossfade width it is how well
    the two stretches the crossfade blends agree, and a crossfade between
    stretches that disagree cancels instead of joining."""
    x = np.asarray(mono, dtype=np.float64)
    target = x[target_start:target_start + width]
    energy = float(np.dot(target, target))
    if energy <= 0.0:
        return np.ones(len(ends))

    lo, hi = int(ends[0]), int(ends[-1])
    region = x[lo - width:hi]
    if len(region) < width:
        return np.ones(len(ends))

    dot = np.correlate(region, target, mode="valid")
    squares = np.concatenate(([0.0], np.cumsum(x * x)))
    window_energy = squares[ends] - squares[ends - width]

    diff = window_energy - 2.0 * dot[:len(ends)] + energy
    return np.sqrt(np.maximum(diff, 0.0) / energy)


def find_loop(mono, sample_rate, hit, min_periods=3, min_loop_ms=20.0,
              period_tolerance=0.12, longer_is_better=1.25, min_confidence=0.3,
              good_enough=0.05):
    """Propose a loop for one stroke, or explain why it cannot.

    The length is constrained to a whole number of periods of the estimated f0,
    then chosen among those by how well the two ends match. Among candidates
    that match nearly as well as the best, the longest wins: a three-period loop
    that matches perfectly still sounds static and machine-like, and the extra
    length costs nothing.

    With no reliable f0 the search still runs, unconstrained, and says so. That
    is the honest answer for a cymbal: there is no period to be a multiple of."""
    lo, hi = sustain_region(mono, hit, sample_rate)
    f0, confidence = estimate_f0(mono, sample_rate, lo, hi)

    # YIN always returns its best lag, even for noise, where that lag means
    # nothing. Constraining loop lengths to multiples of a meaningless period is
    # worse than not constraining them at all, so unpitched material has to be
    # recognised and let through unconstrained rather than quietly obeyed.
    #
    # The gate sits where it does from measurement, not taste. A real repeat
    # period on a short decaying bass note (55 ms of sustain, six cycles) scores
    # 0.38; band-limited noise scores 0.08. A gate of 0.5 rejects the first
    # along with the second.
    period = sample_rate / f0 if (f0 > 0.0 and confidence >= min_confidence) else 0.0

    # The window the two ends are compared over, which is also the crossfade
    # that will work: one period if there is one, 20 ms otherwise.
    width = int(period) if period >= 16.0 else int(0.020 * sample_rate)
    width = max(8, min(width, (hi - lo) // 4))

    # Three periods is a musical minimum rather than a technical one: shorter
    # loops start to sound like a pitch of their own. But refusing outright is
    # worse than offering two, so a short note gets what fits and is told.
    room = hi - lo
    shortened = 0
    periods_wanted = min_periods
    while periods_wanted > 1:
        needed = max(int(min_loop_ms * sample_rate / 1000.0),
                     int(periods_wanted * period) if period else 0) + 2 * width
        if needed <= room:
            break
        periods_wanted -= 1
        shortened += 1

    min_length = max(int(min_loop_ms * sample_rate / 1000.0),
                     int(periods_wanted * period) if period else 0)

    # loopEnd is searched over the last part of the sustain, so a bad final
    # moment is not forced on the loop. loopStart needs a whole comparison
    # window of sustain in front of it.
    end_lo = max(lo + width + min_length, hi - max(width, int(period) * 2 or width))
    ends = np.arange(end_lo, hi + 1)
    if ends.size == 0 or room < min_length + 2 * width:
        return None, (f"not enough sustain to loop: {room} samples after the "
                      f"attack, need {min_length + 2 * width}")

    # Every (loopStart, loopEnd) worth considering, scored, then one selection
    # rule applied to the lot. Choosing per loopEnd and then choosing among the
    # winners applies the rule twice and can pick a candidate that is neither
    # the best match nor the longest.
    all_costs, all_starts, all_ends = [], [], []
    for end in ends[::max(1, len(ends) // 12)]:
        starts_lo = lo + width
        starts_hi = end - min_length
        if starts_hi <= starts_lo:
            continue

        starts = np.arange(starts_lo, starts_hi + 1)
        costs = _match_costs(mono, starts, end - width, width)
        lengths = end - starts

        if period > 0.0:
            # The constraint: keep only lengths within a fraction of a period of
            # a whole number of them.
            phase = np.abs(((lengths / period + 0.5) % 1.0) - 0.5)
            costs = np.where(phase <= period_tolerance, costs, np.inf)

        keep = np.isfinite(costs)
        if not np.any(keep):
            continue

        all_costs.append(costs[keep])
        all_starts.append(starts[keep])
        all_ends.append(np.full(int(keep.sum()), end))

    if not all_costs:
        return None, "no loop length matched: nothing periodic enough to wrap on"

    costs = np.concatenate(all_costs)
    starts = np.concatenate(all_starts)
    ends_of = np.concatenate(all_ends)
    lengths = ends_of - starts

    # Two ways to be acceptable, and the absolute one matters more than it
    # looks. On material that loops well every candidate scores near zero, and a
    # purely relative band then rejects a half-second loop for being 0.004 worse
    # than a 27 ms one. The difference between those two matches is inaudible
    # after a crossfade; the difference between their lengths is the difference
    # between an instrument and a machine.
    floor = float(np.min(costs))
    acceptable = np.nonzero(costs <= max(good_enough, floor * longer_is_better))[0]
    pick = acceptable[np.argmax(lengths[acceptable])]

    cost = float(costs[pick])
    loop_start, loop_end = int(starts[pick]), int(ends_of[pick])
    length = int(lengths[pick])

    # Two different questions, and they need two different measurements.
    #
    # cost is over a whole comparison window, which is right for *choosing*
    # among candidates: it prefers ends that agree in phase and in timbre. It is
    # wrong for judging the result, because on a decaying note the amplitude
    # drift across a window dominates it, and drift is what a crossfade is for.
    # A loop whose seam is inaudible can score a cost of 0.33.
    #
    # What is audible at the wrap is the step, and the yardstick for a step is
    # the steepest step the material takes by itself. A seam no steeper than
    # that cannot be heard as a click, because the waveform does it anyway.
    step = abs(float(mono[loop_start]) - float(mono[loop_end - 1]))
    natural = float(np.max(np.abs(np.diff(np.asarray(mono[lo:hi], dtype=np.float64)))))
    seam_ratio = step / natural if natural > 0.0 else 0.0

    if seam_ratio <= 1.0:
        verdict = "clean"
    elif seam_ratio <= 3.0:
        verdict = "usable, crossfade it"
    else:
        verdict = "POOR, this may not loop"

    suggestion = LoopSuggestion(
        # f0 is the pitch this loop was built on, so it is 0 when none was
        # used. Reporting the measurement that was rejected would read as a
        # pitch the caller could rely on; the confidence says why it was not.
        loop_start=loop_start, loop_end=loop_end,
        f0=f0 if period else 0.0, confidence=confidence,
        periods=(length / period) if period else 0.0, cost=cost,
        crossfade_ms=width / sample_rate * 1000.0, seam_ratio=seam_ratio)

    if period:
        suggestion.note = (f"f0 {f0:.1f} Hz (conf {confidence:.2f}), "
                           f"{length / period:.2f} periods, "
                           f"{length / sample_rate * 1000.0:.0f} ms, "
                           f"seam {seam_ratio:.2f}x natural ({verdict}), "
                           f"blend {cost:.2f}")
        if shortened:
            suggestion.note += (f"; only {periods_wanted} periods fit in this "
                                f"note, wanted {min_periods}")
    else:
        # Name the rejected estimate rather than dropping it: "0.08 confidence
        # at 500 Hz" is what tells you this is genuinely unpitched material and
        # not a detector that gave up.
        suggestion.note = (f"no reliable pitch (best guess {f0:.0f} Hz at "
                           f"confidence {confidence:.2f}), length unconstrained; "
                           f"{length / sample_rate * 1000.0:.0f} ms, "
                           f"seam {seam_ratio:.2f}x natural ({verdict}), "
                           f"blend {cost:.2f}")

    return suggestion, suggestion.note


# ─── Linking ───────────────────────────────────────────────────────────────

def _mic_stripped(name, prefixes):
    for pre in prefixes:
        if name.startswith(pre):
            return name[len(pre):]
    return name


def propose_links(entries, mic_prefixes=("Ambix", "Prox")):
    """Group files that are the same take through different mic sets.

    Keyed on hit count, note range and frame count. Two mic sets cut from one
    multitrack are frame-identical, which makes the key decisive. Stripping a
    known mic prefix from the family name is used only as a second vote, never
    on its own, so the tool does not depend on a prefix vocabulary."""
    buckets = {}
    for e in entries:
        buckets.setdefault((e.expected, e.note_low, e.note_high, e.n_frames), []).append(e)

    takes = []
    for key, members in sorted(buckets.items()):
        if len(members) < 2:
            continue
        members.sort(key=lambda e: e.filename)

        stripped = {_mic_stripped(e.family, mic_prefixes) for e in members}
        name = sorted(stripped)[0] if len(stripped) == 1 else members[0].family

        takes.append(Take(name=name,
                          reference=choose_reference(members),
                          members=[e.filename for e in members],
                          status="proposed"))
    return takes


def choose_reference(members):
    """The close mic: fewest channels wins (mono close mic over a 4-channel
    ambisonic array), then the shortest name for stability."""
    return sorted(members, key=lambda e: (e.n_channels, len(e.filename), e.filename))[0].filename


def recompute_hits(entry, mono, p):
    """Segment ends and loudnesses for a hit list whose starts have changed.
    Ends are always the next start, so moving one stroke resizes the segment
    either side of it."""
    entry.hits.sort(key=lambda h: h.start)
    for i, h in enumerate(entry.hits):
        h.end = entry.hits[i + 1].start if i + 1 < len(entry.hits) else entry.n_frames
        h.rms, h.peak = measure(mono, entry.sample_rate, h.start, h.end, p)


def align_members(entries, takes, folder, p):
    """Move every verified take's members onto the reference's exact samples.

    Hand-locked strokes are left alone: an explicit placement outranks a
    configuration flag. Unlike the editor's M key this does not lock what it
    moves, because the positions are a reproducible consequence of
    align_members rather than a decision someone made about one stroke."""
    by_name = {e.filename: e for e in entries}
    notes = []

    for take in takes:
        if not take.status.startswith("linked"):
            notes.append(f"{take.name}: link not verified, members left on their own feet")
            continue
        ref = by_name.get(take.reference)
        if ref is None:
            continue
        ref_starts = [h.start for h in sorted(ref.hits, key=lambda h: h.start)]

        for member in take.members:
            if member == take.reference:
                continue
            entry = by_name.get(member)
            if entry is None:
                continue
            mine = sorted(entry.hits, key=lambda h: h.start)
            if len(mine) != len(ref_starts):
                notes.append(f"{member}: {len(mine)} strokes against {len(ref_starts)} "
                             "in the reference, not aligned")
                continue

            shifts = [s - h.start for h, s in zip(mine, ref_starts) if not h.locked]
            moved = sum(1 for h, s in zip(mine, ref_starts) if not h.locked and h.start != s)
            skipped = sum(1 for h in mine if h.locked)
            for h, s in zip(mine, ref_starts):
                if not h.locked:
                    h.start = s
            if moved:
                data, _, _ = read_wav(os.path.join(folder, member))
                recompute_hits(entry, detection_channel(data), p)
                median_shift = float(np.median(shifts)) if shifts else 0.0
                notes.append(f"{member}: {moved} stroke(s) aligned to "
                             f"{take.reference} (median shift {median_shift:+.0f} samples, "
                             f"{median_shift / entry.sample_rate * 1000:+.2f} ms)"
                             + (f", {skipped} locked stroke(s) left alone" if skipped else ""))

    return notes


def refresh_link_status(entries, takes, p):
    """Re-check every link against the hits as they currently stand. Cheap (it
    only compares start positions), so the editor can call it after each edit
    and say straight away whether a take still hangs together."""
    by_name = {e.filename: e for e in entries}
    for take in takes:
        if any(m not in by_name for m in take.members):
            take.status = "REJECTED: a member file is missing"
            continue
        onsets = {m: sorted(h.start for h in by_name[m].hits) for m in take.members}
        ok, msg = verify_link(take, onsets, by_name[take.reference].sample_rate, p)
        take.status = ("linked: " if ok else "REJECTED: ") + msg
    return takes


def take_offsets(member_train, ref_train):
    """Per-stroke offsets between a member and its reference, with the strokes
    both files place at sample 0 dropped.

    The recording convention forces the first stroke to sample 0 in every file
    of a take, so that pair's offset is 0 by construction and carries no timing
    information. Leaving it in drags the estimate towards zero and inflates any
    measure of spread by the whole propagation delay."""
    member_train = np.asarray(member_train)
    ref_train = np.asarray(ref_train)
    informative = ~((member_train == 0) & (ref_train == 0))
    return (member_train - ref_train)[informative]


def verify_link(take, onsets_by_file, sample_rate, p):
    """A real link shows a *constant* per-stroke offset between its members:
    the drum and the mics do not move, so the flight time between them is the
    same for every stroke. What is checked is therefore how far the worst
    stroke departs from the take's median offset, not the raw spread.

    Returns (ok, message)."""
    trains = [np.asarray(onsets_by_file[f]) for f in take.members]
    counts = {len(t) for t in trains}
    if len(counts) != 1:
        return False, f"hit counts differ: {[len(t) for t in trains]}"

    ref = np.asarray(onsets_by_file[take.reference])
    tol = p.link_tolerance_ms * sample_rate / 1000.0
    summary = []

    for f in take.members:
        if f == take.reference:
            continue
        d = take_offsets(onsets_by_file[f], ref)
        if d.size < 2:
            continue
        offset = float(np.median(d))
        deviation = np.abs(d - offset)
        worst = float(deviation.max())
        if worst > tol:
            stroke = int(np.argmax(deviation))
            return False, (f"{f}: stroke {stroke + 1} sits {worst:.0f} samples "
                           f"({worst / sample_rate * 1000:.1f} ms) off the take's "
                           f"constant offset of {offset:.0f}")
        summary.append(f"offset {offset:.0f} samples "
                       f"({offset / sample_rate * 1000:.2f} ms), "
                       f"worst stroke {worst:.0f} off")

    return True, "; ".join(summary) if summary else "single file"


# ─── Analysis pipeline ─────────────────────────────────────────────────────

def analyse_folder(folder, p=None, progress=None, link=True, previous=None):
    """Reads every conventionally named wav in `folder`, detects its strokes,
    proposes and verifies links, and shares the velocity ordering across each
    verified take. Returns (entries, takes, params, notes)."""
    p = p or DetectParams()
    previous_by_name = {e.filename: e for e in (previous or [])}

    paths = sorted(glob.glob(os.path.join(folder, "*.wav")))
    entries, onsets_by_file, rate_by_file = [], {}, {}

    for path in paths:
        entry = parse_filename(path)
        if entry is None:
            continue

        data, n_ch, rate = read_wav(path)
        mono = detection_channel(data)
        entry.n_channels = n_ch
        entry.sample_rate = rate
        entry.n_frames = int(mono.shape[0])

        starts, diag = detect_onsets(mono, rate, entry.expected, p)
        entry.starts_at_zero = bool(diag["at_zero"])

        # Hand-edited starts win over detected ones. This has to happen before
        # the segment ends and loudnesses are worked out, because inserting or
        # moving a start changes the segment either side of it.
        old = previous_by_name.get(entry.filename)
        locked = sorted(h.start for h in old.hits if h.locked) if old else []
        if locked:
            starts = merge_locked_starts(starts, locked, rate, p)
            entry.warnings.append(f"{len(locked)} locked start(s) kept from the state file")

        if len(starts) != entry.expected:
            entry.warnings.append(f"expected {entry.expected} strokes, found {len(starts)}")

        locked_set = set(locked)
        for i, s in enumerate(starts):
            end = starts[i + 1] if i + 1 < len(starts) else entry.n_frames
            rms, peak = measure(mono, rate, s, end, p)
            entry.hits.append(Hit(start=int(s), end=int(end), rms=rms, peak=peak,
                                  locked=s in locked_set))

        entries.append(entry)
        onsets_by_file[entry.filename] = starts
        rate_by_file[entry.filename] = rate
        if progress:
            progress(entry)

    takes = propose_links(entries) if link else []
    for take in takes:
        rate = rate_by_file[take.reference]
        ok, msg = verify_link(take, onsets_by_file, rate, p)
        take.status = ("linked: " if ok else "REJECTED: ") + msg

    notes = []
    if takes and p.align_members:
        # Before ranking: moving a start resizes its segment, which changes the
        # loudness that decides the velocity ladder.
        notes += align_members(entries, takes, folder, p)
        refresh_link_status(entries, takes, p)

    # Ranking is the last step, so it always sees the final hit list: detected,
    # merged with anything locked, aligned if asked, ends and loudnesses settled.
    for entry in entries:
        assign_ranks(entry.hits)

    if takes:
        notes += apply_take_ordering(entries, takes)
    return entries, takes, p, notes


# ─── Velocity ladder ───────────────────────────────────────────────────────

def assign_ranks(hits):
    """Softest stroke gets rank 0. Ranking on the fixed-window RMS is what puts
    a stroke in a velocity layer, so it is deliberately separate from the time
    order the hits are stored in."""
    for rank, hit in enumerate(sorted(hits, key=lambda h: h.rms)):
        hit.rank = rank
    return hits


def apply_take_ordering(entries, takes):
    """Give every member of a verified take the reference file's velocity
    ordering, matched stroke by stroke in time.

    Without this each file ranks its own strokes by its own RMS, and the two
    mic sets of one take disagree about which stroke is layer 12. They sound
    together on the same note, so the disagreement is audible: the close mic of
    one stroke against the room mic of another. Ranking is a property of the
    performance, not of the microphone, so it is decided once on the reference
    (the close mic, best signal to noise) and copied.

    Only verified links are used. A rejected one falls back to per-file
    ranking, because copying an ordering across files whose strokes do not
    correspond would be worse than the disagreement it replaces."""
    by_name = {e.filename: e for e in entries}
    notes = []

    for take in takes:
        if not take.status.startswith("linked"):
            notes.append(f"{take.name}: link not verified, each file ranked on its own")
            continue

        ref = by_name.get(take.reference)
        if ref is None:
            notes.append(f"{take.name}: reference {take.reference} not among the files")
            continue
        ref_hits = sorted(ref.hits, key=lambda h: h.start)

        for member in take.members:
            if member == take.reference:
                continue
            entry = by_name.get(member)
            if entry is None:
                continue
            hits = sorted(entry.hits, key=lambda h: h.start)
            if len(hits) != len(ref_hits):
                notes.append(f"{member}: {len(hits)} strokes against "
                             f"{len(ref_hits)} in the reference, left ranked on its own")
                continue
            changed = sum(1 for h, r in zip(hits, ref_hits) if h.rank != r.rank)
            for h, r in zip(hits, ref_hits):
                h.rank = r.rank
            if changed:
                notes.append(f"{member}: {changed} of {len(hits)} strokes re-ranked "
                             f"to match {take.reference}")
                entry.warnings.append(f"{changed} stroke(s) re-ranked from take reference")

    return notes


def velocity_ranges(count):
    """Split 0..127 into `count` layers. Reproduces the split the existing
    mapping.xml files were written with, so re-exporting an untouched mapping
    changes nothing."""
    if count <= 0:
        return []
    step = 128.0 / count
    return [(int(i * step), 127 if i == count - 1 else int((i + 1) * step - 1))
            for i in range(count)]


# ─── mapping.xml ───────────────────────────────────────────────────────────
#
# The mapping files are hand-maintained: multi-line <Bus> elements, chosen
# blank lines, a stray trailing space or two. Re-serialising a parsed DOM would
# quietly reformat all of it and make any real change impossible to see in a
# diff. So the writer works on the text and replaces only the runs of <Sound>
# lines, using an existing line from each run as the template. Attribute order,
# indentation and any attribute the tool does not know about survive untouched.

_SOUND_LINE = re.compile(r"^\s*<Sound\b")


def _attr(line, name):
    m = re.search(r'(?<=[\s])' + re.escape(name) + r'="([^"]*)"', line)
    return m.group(1) if m else None


def _set_attr(line, name, value):
    pattern = r'(?<=[\s])(' + re.escape(name) + r'=")[^"]*(")'
    new, n = re.subn(pattern, lambda m: m.group(1) + str(value) + m.group(2), line, count=1)
    if n == 0:
        raise ValueError(f"no {name} attribute to replace in: {line.strip()[:80]}")
    return new


def _upsert_attr(line, name, value):
    """Replace the attribute, or add it before the closing bracket when the
    template has none. Adding rather than requiring it is what lets a mapping
    written before loop points existed gain them without being rewritten."""
    try:
        return _set_attr(line, name, value)
    except ValueError:
        pass

    m = re.search(r"\s*/?>\s*$", line)
    if m is None:
        raise ValueError(f"no closing bracket to insert {name} into: {line.strip()[:80]}")
    return line[:m.start()] + f' {name}="{value}"' + line[m.start():]


def _drop_attr(line, name):
    """Remove the attribute if present. A stroke with no loop points must emit
    no loop attributes at all, or clearing a loop in the editor would leave a
    stale loopStart behind in the mapping."""
    return re.sub(r'\s+' + re.escape(name) + r'="[^"]*"', "", line, count=1)


def _read_text(path):
    """Returns (lines, terminator, had_final_newline) so the file can be put
    back together exactly as it was."""
    raw = open(path, "rb").read()
    term = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("utf-8")
    final = text.endswith(term)
    if final:
        text = text[: -len(term)]
    return text.split(term), term, final


def _write_text(path, lines, term, final_newline):
    body = term.join(lines) + (term if final_newline else "")
    with open(path, "wb") as f:
        f.write(body.encode("utf-8"))


def sound_blocks(lines):
    """Runs of consecutive <Sound> lines sharing one resource, as
    (first_index, last_index, group, resource). Indices are 0-based and
    inclusive."""
    blocks, run = [], None
    for i, line in enumerate(lines):
        if _SOUND_LINE.match(line):
            key = (_attr(line, "group"), _attr(line, "resource"))
            if run and run[0] == key and run[2] == i - 1:
                run = (key, run[1], i)
            else:
                if run:
                    blocks.append((run[1], run[2], run[0][0], run[0][1]))
                run = (key, i, i)
        elif run:
            blocks.append((run[1], run[2], run[0][0], run[0][1]))
            run = None
    if run:
        blocks.append((run[1], run[2], run[0][0], run[0][1]))
    return blocks


def read_mapping_xml(path):
    """resource -> list of hits in velocity order, taken from the <Sound>
    elements. Used to seed the state from a mapping you have already tuned."""
    lines, _, _ = _read_text(path)
    out = {}
    for first, last, group, resource in sound_blocks(lines):
        hits = []
        for rank, line in enumerate(lines[first:last + 1]):
            start, end = _attr(line, "sampleStart"), _attr(line, "sampleEnd")
            if start is None or end is None:
                # A kit whose samples are already cut, one file per stroke, is a
                # perfectly good mapping; it just has nothing for these tools to
                # slice. Say so rather than failing halfway through.
                raise UncutMapping(
                    f"{os.path.basename(path)} has <Sound> elements without "
                    f"sampleStart/sampleEnd (first: {resource}).\n"
                    "These tools slice multi-hit take files. A kit whose samples "
                    "are already cut needs no slicing and is not their subject.")
            def optional(name):
                raw = _attr(line, name)
                return int(raw) if raw is not None else -1

            hits.append(Hit(start=int(start), end=int(end), rank=rank,
                            loop_start=optional("loopStart"),
                            loop_end=optional("loopEnd"),
                            release_start=optional("releaseStart")))
        out[resource] = hits
    return out


def seed_entries_from_mapping(entries, path):
    """Replaces detected hits with the ones in mapping.xml, keeping each
    stroke's velocity position. Returns the list of resources the mapping had
    no block for."""
    blocks = read_mapping_xml(path)
    missing = []
    for e in entries:
        hits = blocks.get(e.filename)
        if hits is None:
            missing.append(e.filename)
            continue
        e.hits = sorted((Hit(start=h.start, end=h.end, rank=h.rank,
                             loop_start=h.loop_start, loop_end=h.loop_end,
                             release_start=h.release_start) for h in hits),
                        key=lambda h: h.start)
    return missing


STRIP_TYPE_BY_CHANNELS = {1: "mono", 2: "stereo", 4: "ambisonic"}

SCAFFOLD_COLOURS = ["255,100,100", "255,120,0", "255,100,255", "255,255,100",
                    "100,255,100", "100,200,255", "200,150,255", "255,180,120"]


def create_scaffold(entries, path):
    """Write a starter mapping.xml for a kit that has none.

    Only the parts that follow from the wav files are filled in: one sample
    group per group name, sequential channel assignments, and a strip per
    group. Everything describing routing and presentation (artwork, colours,
    which strips share a bus, ambisonic versus mono handling, reverb sends) is
    a placeholder marked TODO, because nothing in a file name implies it.

    The Sound elements are left as a single placeholder per resource. Running
    the normal writer over the result expands each one into its velocity
    layers, so there is one code path that emits Sound lines."""
    groups = {}
    for e in sorted(entries, key=lambda x: (x.group, x.family)):
        g = groups.setdefault(e.group, {"channels": e.n_channels,
                                        "mute": e.mute_group,
                                        "files": []})
        g["files"].append(e)
        if g["channels"] != e.n_channels:
            g["channels"] = max(g["channels"], e.n_channels)

    total_channels = sum(g["channels"] for g in groups.values())

    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           "<!-- Generated scaffold. Every TODO below needs a real value: the wav",
           "     files say nothing about artwork, colours or routing. -->",
           "<Mapping>",
           '  <WelcomeTab text="TODO kit name" img="TODO_welcome.png" />',
           f'  <Master channels="{total_channels}" img="logo686.png" color="0,220,255"/>',
           "",
           "  <Mixer>"]

    for i, (name, g) in enumerate(groups.items()):
        strip = STRIP_TYPE_BY_CHANNELS.get(g["channels"], "mono")
        colour = SCAFFOLD_COLOURS[i % len(SCAFFOLD_COLOURS)]
        out.append(f'    <Strip type="{strip}" name="{name}" '
                   f'img="TODO_{name}.jpg" color="{colour}"/>')

    out += ['    <Bus type="stereo" name="Parallel bus" img="tube.png" color="100,255,100"/>',
            "  </Mixer>",
            ""]

    offset = 0
    for name, g in groups.items():
        n_ch = g["channels"]
        channels = ", ".join(f"{i}:{offset + i}" for i in range(n_ch))
        out.append(f'  <SampleGroup name="{name}" channels="{channels}" '
                   f'muteGroup="{g["mute"]}" oneShot="true" attack="0.001" '
                   f'decay="0.0" sustain="1.0" release="0.1"/>')
        for e in g["files"]:
            out.append(f'  <Sound name="{e.family}" group="{name}" '
                       f'resource="{e.filename}" basePitch="{e.base_pitch}" '
                       f'noteLow="{e.note_low}" noteHigh="{e.note_high}" '
                       f'velLow="0" velHigh="127" sampleStart="0" sampleEnd="0"/>')
        out.append("")
        offset += n_ch

    out.append("</Mapping>")
    _write_text(path, out, "\n", True)
    return sorted(groups)


def write_mapping_xml(src_path, entries, dst_path=None):
    """Rewrites only the <Sound> runs of src_path from `entries`. Every other
    byte is carried across unchanged. Returns a list of human-readable notes."""
    dst_path = dst_path or src_path
    lines, term, final = _read_text(src_path)
    by_resource = {e.filename: e for e in entries}
    notes = []
    seen = set()

    # Last block first, so earlier indices stay valid as runs change length.
    for first, last, group, resource in reversed(sound_blocks(lines)):
        seen.add(resource)
        entry = by_resource.get(resource)
        if entry is None:
            notes.append(f"{resource}: no state for this resource, block left untouched")
            continue
        if not entry.hits:
            notes.append(f"{resource}: state has no hits, block left untouched")
            continue

        template = lines[first]
        ordered = sorted(entry.hits, key=lambda h: h.rank)
        ranges = velocity_ranges(len(ordered))

        new_lines = []
        for (lo, hi), hit in zip(ranges, ordered):
            line = _set_attr(template, "velLow", lo)
            line = _set_attr(line, "velHigh", hi)
            line = _set_attr(line, "sampleStart", hit.start)
            line = _set_attr(line, "sampleEnd", hit.end)

            # Loop attributes appear only on strokes that have them. A kit with
            # no loops therefore produces exactly the line it produced before
            # loop points existed, which is what keeps the round trip byte for
            # byte and its diffs worth reading.
            for name, value in (("loopStart", hit.loop_start),
                                ("loopEnd", hit.loop_end),
                                ("releaseStart", hit.release_start)):
                if hit.has_loop and value >= 0:
                    line = _upsert_attr(line, name, value)
                else:
                    line = _drop_attr(line, name)

            new_lines.append(line)

            for w in loop_warnings(hit):
                notes.append(f"{resource} layer {hit.rank + 1}: {w}")

        old_count = last - first + 1
        if old_count != len(new_lines):
            notes.append(f"{resource}: {old_count} -> {len(new_lines)} velocity layers")
        lines[first:last + 1] = new_lines

    for e in entries:
        if e.filename not in seen:
            notes.append(f"{e.filename}: no <Sound> block in {os.path.basename(src_path)}, "
                         "not written (add a SampleGroup for it by hand)")

    _write_text(dst_path, lines, term, final)
    return notes


# ─── State file ────────────────────────────────────────────────────────────

def save_state(path, entries, takes, p):
    """The authoring state: hit lists, lock flags and the parameters that
    produced them. Kept beside mapping.xml and meant to be committed."""
    doc = {
        "version": STATE_VERSION,
        "params": asdict(p),
        "takes": [asdict(t) for t in takes],
        "files": [asdict(e) for e in entries],
    }
    with open(path, "w", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def load_state(path):
    with open(path) as f:
        doc = json.load(f)
    if doc.get("version") != STATE_VERSION:
        raise ValueError(f"{path}: state version {doc.get('version')}, expected {STATE_VERSION}")

    p = DetectParams(**doc["params"])
    takes = [Take(**t) for t in doc["takes"]]
    entries = []
    for d in doc["files"]:
        hits = [Hit(**h) for h in d.pop("hits", [])]
        e = FileEntry(**d)
        e.hits = hits
        entries.append(e)
    return entries, takes, p


def merge_locked_starts(detected, locked, sample_rate, p):
    """Fold hand-edited starts into a fresh detection.

    Each locked start consumes the detector's nearest version of the same
    stroke, so moving a marker moves it rather than leaving a duplicate behind.
    The association window is the minimum gap: two strokes closer than that
    were never separable anyway. A locked start with nothing within that window
    is a stroke the user added, and it is simply inserted."""
    window = p.min_gap_ms * sample_rate / 1000.0
    remaining = sorted(detected)

    for l in sorted(locked):
        if not remaining:
            break
        nearest = min(range(len(remaining)), key=lambda i: abs(remaining[i] - l))
        if abs(remaining[nearest] - l) <= window:
            remaining.pop(nearest)

    return sorted(set(remaining) | set(locked))
