# Vendored dependencies — replay tool

The OnSpeed docs-site replay tool exports MP4 clips with the M5 indicator
burned in. The encode pipeline uses the browser's `VideoEncoder`
(WebCodecs API); muxing the encoded frames into an MP4 container is the
job of `mp4-muxer`, vendored here.

The bundle is committed verbatim so the docs-site build has no Internet
dependency at runtime (Pilots load `/data-and-logs/replay/` from a
locally-served mkdocs build or the deployed GitHub Pages site — both
serve `lib/vendor/mp4-muxer.js` from the same origin).

## Files

| File | Source | Version | Size | License |
|---|---|---|---:|---|
| `mp4-muxer.js` | https://cdn.jsdelivr.net/npm/mp4-muxer@5.2.2/build/mp4-muxer.min.mjs | 5.2.2 | ~42 KB | MIT |

## What it provides

`mp4-muxer` consumes encoded video chunks from `VideoEncoder` (and
optionally audio chunks from `AudioEncoder`) and writes them into a
well-formed ISO BMFF (MP4) container.

API surface used by `replay/mp4Export.js`:

- `Muxer` — the encoder-output sink. Accepts encoded `Uint8Array`
  chunks via `addVideoChunkRaw(data, type, timestampUs, durationUs,
  meta)`. `meta.decoderConfig.description` carries the AVC config
  blob from the first `VideoEncoder.output` callback.
- `ArrayBufferTarget` — in-memory output. After `muxer.finalize()`,
  `target.buffer` holds the complete MP4 as an `ArrayBuffer` for the
  download.

## Why mp4-muxer

WebCodecs ships in Chrome/Edge and gives the browser an H.264 encoder.
But `VideoEncoder` emits raw H.264 NAL units, not an MP4. The browser
provides no container muxer of its own. Options considered:

- **mp4-muxer** (chosen): pure JS, no deps, ~42 KB minified, MIT,
  active. Built specifically for the WebCodecs → MP4 path. Used by
  Remotion's web renderer and several browser-side video editors.
- **mp4box.js**: ~600 KB. Full-featured, but mostly a *demuxer*; its
  muxer path is less polished and the dependency footprint is far
  heavier than we need for write-only.
- **mux.js** (HLS.js project): designed for fMP4 streaming over MSE,
  not file-on-disk; output is segmented in a way that confuses
  desktop players.

## Browser support

WebCodecs `VideoEncoder` is currently Chrome / Edge desktop only.
Safari has partial support that doesn't reach our codec configs
reliably. Firefox is missing the encoder API entirely. The replay
tool feature-detects `'VideoEncoder' in window && 'OffscreenCanvas'
in window && 'requestVideoFrameCallback' in HTMLVideoElement.prototype`
and grays out the Export MP4 button outside Chrome/Edge desktop with
a tooltip noting the limitation.

## Refresh procedure

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

## License

MIT-licensed. Reproduced below for the audit-able license trail.

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
