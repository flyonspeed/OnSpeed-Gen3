# Vendored dependencies — replay tool

The OnSpeed docs-site replay tool exports MP4 clips with the M5 indicator
burned in. Two third-party libraries are vendored here:

- `mp4-muxer.js` — writes the WebCodecs-encoded video + AAC audio into
  a well-formed MP4 (write side).
- `mp4box.js` — parses the source video's MP4 boxes to demux its AAC
  audio track without copying the whole file into memory (read side).

The bundles are committed verbatim so the docs-site build has no Internet
dependency at runtime (Pilots load `/data-and-logs/replay/` from a
locally-served mkdocs build or the deployed GitHub Pages site — both
serve the vendor files from the same origin).

## Files

| File | Source | Version | Size | License |
|---|---|---|---:|---|
| `mp4-muxer.js` | https://cdn.jsdelivr.net/npm/mp4-muxer@5.2.2/build/mp4-muxer.min.mjs | 5.2.2 | ~42 KB | MIT |
| `mp4box.js`    | https://cdn.jsdelivr.net/npm/mp4box@2.3.0/dist/mp4box.all.min.js | 2.3.0 | ~190 KB | BSD-3-Clause |

## What `mp4-muxer` provides

`mp4-muxer` consumes encoded video chunks from `VideoEncoder` (and
optionally audio chunks from `AudioEncoder`, or raw AAC samples
demuxed from a source file) and writes them into a well-formed
ISO BMFF (MP4) container.

API surface used by `replay/mp4Export.js`:

- `Muxer` — the encoder-output sink. Accepts encoded `Uint8Array`
  chunks via `addVideoChunkRaw(data, type, timestampUs, durationUs,
  meta)` and `addAudioChunkRaw(data, type, timestampUs, durationUs,
  meta)`. `meta.decoderConfig.description` carries the AVC config
  blob from the first `VideoEncoder.output` callback (video) and the
  AAC AudioSpecificConfig from the source's `esds` box (audio).
- `ArrayBufferTarget` — in-memory output. After `muxer.finalize()`,
  `target.buffer` holds the complete MP4 as an `ArrayBuffer` for the
  download.

## What `mp4box.js` provides

`mp4box.js` is the canonical JS MP4 box parser (GPAC project). The
replay export uses it as a **demuxer**, not a muxer. We feed only the
moov box (the first few MB of the source file) via `File.slice()` +
`mp4boxFile.appendBuffer(ab)`, where each appended ArrayBuffer's
`fileStart` property tells mp4box where its bytes sit in the original
file. Once `onReady` fires with parsed track info, we walk
`getTrackSamplesInfo(audioTrackId)` to learn every audio sample's
`{ offset, size, cts, duration }` and pull just those byte ranges via
`File.slice(offset, offset+size).arrayBuffer()` — never materializing
the whole file in memory.

API surface used by `replay/mp4Export.js`:

- `createFile()` — constructs an `ISOFile` parser.
- `isoFile.onReady = (info) => …` — fires after moov is parsed; info
  has `audioTracks[].id / .codec / .timescale / .audio.{sample_rate,
  channel_count}`.
- `isoFile.appendBuffer(ab)` — `ab.fileStart` set to its byte offset
  in the source File.
- `isoFile.getTrackSamplesInfo(track_id)` — per-sample
  `{ offset, size, cts, dts, duration }`.
- `isoFile.getTrackById(id).mdia.minf.stbl.stsd.entries[0].esds.esd`
  — drill-through path to the AAC DecoderSpecificInfo (AudioSpecific
  Config bytes) for `EncodedAudioChunk.decoderConfig.description`.

## Why this split (mp4-muxer for write, mp4box for read)

WebCodecs ships in Chrome/Edge and gives the browser an H.264 encoder.
But `VideoEncoder` emits raw H.264 NAL units, not an MP4. The browser
provides no container muxer of its own. Options considered:

- **mp4-muxer** (chosen for write): pure JS, no deps, ~42 KB minified,
  MIT, active. Built specifically for the WebCodecs → MP4 path. Used by
  Remotion's web renderer and several browser-side video editors.
- **mp4box.js as muxer**: ~190 KB minified. Full-featured but its muxer
  path is less polished than mp4-muxer's; we stick with mp4-muxer for
  the write side.
- **mux.js** (HLS.js project): designed for fMP4 streaming over MSE,
  not file-on-disk; output is segmented in a way that confuses
  desktop players.

For the **read** side (extracting source-file audio samples), mp4-muxer
isn't a demuxer at all, and the browser has no MP4 parser exposed to JS.
mp4box.js is the only mature option. It's a ~190 KB cost we accept to
avoid `File.arrayBuffer()` on multi-GB flight videos, which OOM-crashes
the export.

## Browser support

WebCodecs `VideoEncoder` is currently Chrome / Edge desktop only.
Safari has partial support that doesn't reach our codec configs
reliably. Firefox is missing the encoder API entirely. The replay
tool feature-detects `'VideoEncoder' in window && 'OffscreenCanvas'
in window && 'requestVideoFrameCallback' in HTMLVideoElement.prototype`
and grays out the Export MP4 button outside Chrome/Edge desktop with
a tooltip noting the limitation.

## Refresh procedure — mp4-muxer

```bash
python3 -c "import urllib.request; urllib.request.urlretrieve(
  'https://cdn.jsdelivr.net/npm/mp4-muxer@5.2.2/build/mp4-muxer.min.mjs',
  'docs/site/docs/data-and-logs/replay/lib/vendor/mp4-muxer.js')"

# Re-add the leading vendor banner block (preserves source URL + license).
# Bump the version in this file's table.
# Run tests:
cd docs/site/tests && npm test
```

The maintainer recently deprecated `mp4-muxer` in favor of `mediabunny`
(a successor with a broader scope). For our write-only WebCodecs → MP4
need, `mp4-muxer` 5.2.2 remains correct and small; migrating to
mediabunny would be a discretionary refresh, not a forced one.

## Refresh procedure — mp4box.js

```bash
python3 -c "import urllib.request; urllib.request.urlretrieve(
  'https://cdn.jsdelivr.net/npm/mp4box@2.3.0/dist/mp4box.all.min.js',
  'docs/site/docs/data-and-logs/replay/lib/vendor/mp4box.js')"

# Strip the trailing `//# sourceMappingURL=...` comment (points to a
# jsdelivr-host path that won't resolve from our origin).
# Re-add the leading vendor banner block.
# Bump the version in this file's table.
```

mp4box.js 2.x is the modern ESM rewrite; 0.5.x and 1.x are the older
UMD bundles. We're on 2.3.0 (ESM, named exports) so the `import { … }`
in `mp4Export.js` works with no shim.

## Licenses

### mp4-muxer — MIT

```
MIT License

Copyright (c) 2023 Vanilagy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### mp4box.js — BSD-3-Clause

```
Copyright (c) 2012. Telecom ParisTech/TSI/MM/GPAC Cyril Concolato
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
