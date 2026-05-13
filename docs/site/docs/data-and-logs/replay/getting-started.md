---
title: Getting Started — Video Replay
description: How to load files, sync, smooth, and annotate flights in the video replay tool
---

# Getting Started — Video Replay

The video replay tool plays back a flight by syncing your cockpit
video against the matching SD-card log, then overlaying the same M5
indicator your panel showed. Everything runs in the browser; no files
are uploaded.

## What you need

Three files per flight:

- **Log CSV** — the `log_NNN.csv` written to your SD card.
- **Flight video** — a single MP4 / MOV / WebM, or the GoPro chapters
  (`GOPR####.MP4`, `GP010314.MP4`, `GP02####.MP4`, …) for one
  recording session.
- **Config CFG** — the OnSpeed `onspeed2.cfg` (XML) that was running
  on the box at the time of the flight. The flap detents and
  per-flap setpoints come from here.

## Recommended folder layout

Keep one folder per flight on whatever cloud drive you use (Google
Drive, Dropbox, OneDrive — all expose normal file system handles
to the browser). Put the log, every GoPro chapter, and a copy of
the OnSpeed config from your box at the time of the flight all in
the same folder. Keep your master config in a separate `airframe/`
folder; copy it into each flight folder when you archive a flight.

```
RV-4 Data/
├── airframe/
│   └── onspeed2.cfg          # current/live config — what the box runs
├── 2026-05-11/
│   ├── log_007.csv
│   ├── GOPR0314.MP4
│   ├── GP010314.MP4
│   ├── GP020314.MP4
│   ├── GP030314.MP4
│   └── onspeed2.cfg          # snapshot at flight time
├── 2026-05-25/
│   ├── log_008.csv
│   ├── GOPR0315.MP4
│   └── onspeed2.cfg
└── ...
```

Conventions the tool relies on (loosely — nothing is enforced):

- **One folder per flight, named by date.** Self-evident
  chronological order in any file browser.
- **A copy of the OnSpeed config inside each flight folder.** This
  is the snapshot. When you re-load a flight months later, walking
  into the flight folder surfaces the config-as-flown, not whatever
  the master config has drifted to. Skip this step and the journal
  still works, but old flights will render under whatever config
  you pick at re-load time.
- **Master/current config in `airframe/`.** When you update setpoints,
  this is what you edit on disk and re-upload to the box. After each
  flight, copy `airframe/onspeed2.cfg` into the new flight folder.
- **GoPro chapters live alongside the log.** The folder-pick flow
  auto-detects every `GOPR####` + `GP0N####` sibling.

## Loading files

The toolbar has three buttons: **Open video…**, **Open log…**, and
**Open config…**. On Chrome / Edge desktop these use the File System
Access API — pick a single file, or pick the flight folder and the
tool will gather every GoPro chapter automatically. On other browsers
the buttons fall back to a plain file-picker (no folder pick, no
multi-chapter auto-detect).

When you re-pick the same log later, the takeoff anchor and any clips
you saved come back automatically — they're keyed off a hash of the
log's first 10 KB, so two flights with different content but the same
filename don't collide.

On Chrome / Edge desktop the page also remembers the file handles
themselves. Reload within the same browser session and the video, log,
and config re-open without a click — a "Resuming…" pill appears in the
toolbar while the files load. If the browser has expired the read
grant (typical after a browser restart, or after enough time has
passed), a Resume banner appears and one click re-grants permission
for all three files. Firefox and Safari fall back to the file-picker
each session.

## Sync

Pick a video and a log; the tool auto-detects takeoff in the log and
annotates the timeline. Click **Mark video anchor** while the video
is at the rotation moment, and the two clocks lock. From then on:

- The flight profile (IAS stripchart at the bottom) is clickable.
  **Click anywhere on it to seek the video to that log moment.**
- Shift-click to manually move the takeoff anchor (useful when the
  auto-detector picked the wrong climbout, e.g. on a pattern flight
  with multiple liftoffs).

If sync drifts mid-flight — say a video edit chopped a few seconds
out — click **Pause indexer for re-sync** at a recognizable moment,
scrub the video to the matching frame, and click **Attach here**. The
log clock re-anchors against the video at that point.

## Ball smoothing slider

Under the mode buttons there's a "Ball smoothing" slider, range 0 to 3
seconds. It only affects the slip ball — the lateral-G EMA time
constant for the rendered indicator, NOT the firmware's actual
behavior in flight.

- **0 s** — firmware-faithful, lively at 208 Hz.
- **0.25 s** — VN-300 territory.
- **0.75 s** — Dynon SkyView territory.

Dial it up until the ball looks like what you remember seeing on your
EFIS in flight. The other channels (AOA, pitch, IAS) are unchanged.

## Data marks vs clips

Two granularities of annotation on the same timeline:

- **Data mark (point).** The pilot's in-flight button press. The
  firmware writes a counter into the log's DataMark column; the tool
  detects each transition and surfaces it as a row in the Data marks
  panel. You can name a mark, write notes against it, jump the video
  to it, or kick off a 30 s / 60 s clip starting at it. Marks can't
  be created in the browser — the firmware owns the DataMark column.
- **Clip (range).** A `[startLogMs, endLogMs]` span you create in the
  browser. Annotate it, name it, export it as MP4. Clips are
  independent of marks; a clip that happens to contain marks shows
  them inline at read time.

Both are stored browser-side, keyed by the log's content hash.
Re-load the same log months later, your names and notes come back.

## Keyboard shortcuts

The page listens for these keys regardless of which element has focus
(typing in an `<input>` / `<textarea>` / `[contenteditable]` passes
through normally, so notes and labels are unaffected):

| Key                  | Action                       |
|----------------------|------------------------------|
| `Space`              | Play / pause                 |
| `←` or `,`           | Step back one frame          |
| `→` or `.`           | Step forward one frame       |
| `Shift + ←` / `→`    | Step ten frames              |

Frame size is read from the active video. When the rate can't be
detected the tool assumes 30 fps. Stepping while playing pauses
first — single frames don't survive an active play loop.
