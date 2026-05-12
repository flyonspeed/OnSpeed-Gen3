# Vendored dependencies — replay tool

The OnSpeed docs-site replay tool exports MP4 clips with the M5 indicator
burned in. One third-party library is vendored here:

- `mediabunny.min.mjs` — reads (demuxes) the source MP4 to pull out the
  AAC audio packets and H.264/HEVC video packets, then writes (muxes)
  the WebCodecs-encoded composited video and pass-through audio into a
  new MP4. Both demux and mux in a single library by the same author
  who wrote the (now-deprecated) `mp4-muxer` library.

The bundle is committed verbatim so the docs-site build has no Internet
dependency at runtime (Pilots load `/data-and-logs/replay/` from a
locally-served mkdocs build or the deployed GitHub Pages site — both
serve the vendor files from the same origin).

## Files

| File | Source | Version | Size | License |
|---|---|---|---:|---|
| `mediabunny.min.mjs` | https://cdn.jsdelivr.net/npm/mediabunny@1.44.2/dist/bundles/mediabunny.min.mjs | 1.44.2 | ~592 KB | MPL-2.0 |

## What `mediabunny` provides

Mediabunny is a one-library replacement for the previous mp4box.js
(demux) + mp4-muxer (mux) split. It supports streaming reads natively,
which removes the bounded head/tail probe budget the mp4box.js path
needed for moov discovery — moov-in-the-middle files now work
correctly.

API surface used by `replay/mp4Export.js`:

**Reading (demux):**

- `Input` + `BlobSource(file)` — open a `File` / `Blob` for streaming
  reads. No multi-GB ArrayBuffer materialization.
- `input.getPrimaryVideoTrack()` / `getPrimaryAudioTrack()` — pick out
  the tracks.
- `track.getCodec()`, `getCodecParameterString()`, `getCodedWidth()`,
  `getCodedHeight()`, `getRotation()`, `computePacketStats()`,
  `getDecoderConfig()` — track metadata.
- `EncodedPacketSink(track)` + `sink.packets()` async iterator — walk
  encoded packets, each with `.data: Uint8Array`, `.type: 'key' |
  'delta'`, `.timestamp` (seconds), `.duration` (seconds).

**Writing (mux):**

- `Output({ format: new Mp4OutputFormat({ fastStart: 'in-memory' }),
  target: new BufferTarget() })` — in-memory MP4 writer.
- `EncodedVideoPacketSource('avc' | 'hevc')` — sink for encoded
  video packets (from `VideoEncoder.output` callbacks, wrapped as
  `EncodedPacket`). The first `.add(packet, meta)` call carries
  `meta.decoderConfig.{codec, codedWidth, codedHeight, description}`.
- `EncodedAudioPacketSource('aac')` — sink for AAC packets pulled
  through unchanged from the source's encoded packets. First
  `.add(packet, meta)` call carries `meta.decoderConfig.{codec,
  numberOfChannels, sampleRate, description}` (the
  AudioSpecificConfig from the source).
- `output.addVideoTrack(source, { rotation, frameRate })` /
  `addAudioTrack(source)` — add tracks to the output.
- `output.start()` / `output.finalize()` — lifecycle.
- `output.target.buffer: ArrayBuffer` after finalize — wrap in a Blob.

## Why mediabunny (single library for read + write)

Mediabunny is written by the same author as `mp4-muxer`; its
in-repo deprecation banner points at mediabunny as the successor.
It also covers the demux side, replacing `mp4box.js` and removing
the head/tail moov probe budget that the previous split required
to keep heap usage bounded — non-fast-start MP4s with moov in the
middle are read correctly via streaming. One vendor file (~592 KB
minified) replaces the previous two (~232 KB combined) with a single
library and a fix for the moov-in-the-middle case.

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
  'https://cdn.jsdelivr.net/npm/mediabunny@1.44.2/dist/bundles/mediabunny.min.mjs',
  'docs/site/docs/data-and-logs/replay/lib/vendor/mediabunny.min.mjs')"

# Bump the version in this file's table.
# Run tests:
cd docs/site && node tests/replay/mp4Export-smoke.mjs
```

## License

### Mediabunny — MPL-2.0

```
Mozilla Public License Version 2.0
==================================

1. Definitions
--------------

1.1. "Contributor"
    means each individual or legal entity that creates, contributes to
    the creation of, or owns Covered Software.

1.2. "Contributor Version"
    means the combination of the Contributions of others (if any) used
    by a Contributor and that particular Contributor's Contribution.

1.3. "Contribution"
    means Covered Software of a particular Contributor.

1.4. "Covered Software"
    means Source Code Form to which the initial Contributor has attached
    the notice in Exhibit A, the Executable Form of such Source Code
    Form, and Modifications of such Source Code Form, in each case
    including portions thereof.

1.5. "Incompatible With Secondary Licenses"
    means

    (a) that the initial Contributor has attached the notice described
        in Exhibit B to the Covered Software; or

    (b) that the Covered Software was made available under the terms of
        version 1.1 or earlier of the License, but not also under the
        terms of a Secondary License.

1.6. "Executable Form"
    means any form of the work other than Source Code Form.

1.7. "Larger Work"
    means a work that combines Covered Software with other material, in
    a separate file or files, that is not Covered Software.

1.8. "License"
    means this document.

1.9. "Licensable"
    means having the right to grant, to the maximum extent possible,
    whether at the time of the initial grant or subsequently, any and
    all of the rights conveyed by this License.

1.10. "Modifications"
    means any of the following:

    (a) any file in Source Code Form that results from an addition to,
        deletion from, or modification of the contents of Covered
        Software; or

    (b) any new file in Source Code Form that contains any Covered
        Software.

1.11. "Patent Claims" of a Contributor
    means any patent claim(s), including without limitation, method,
    process, and apparatus claims, in any patent Licensable by such
    Contributor that would be infringed, but for the grant of the
    License, by the making, using, selling, offering for sale, having
    made, import, or transfer of either its Contributions or its
    Contributor Version.

1.12. "Secondary License"
    means either the GNU General Public License, Version 2.0, the GNU
    Lesser General Public License, Version 2.1, the GNU Affero General
    Public License, Version 3.0, or any later versions of those
    licenses.

1.13. "Source Code Form"
    means the form of the work preferred for making modifications.

1.14. "You" (or "Your")
    means an individual or a legal entity exercising rights under this
    License. For legal entities, "You" includes any entity that
    controls, is controlled by, or is under common control with You. For
    purposes of this definition, "control" means (a) the power, direct
    or indirect, to cause the direction or management of such entity,
    whether by contract or otherwise, or (b) ownership of more than
    fifty percent (50%) of the outstanding shares or beneficial
    ownership of such entity.

Full license text: https://www.mozilla.org/en-US/MPL/2.0/

Copyright (c) 2026-present, Vanilagy and contributors
```

The full MPL-2.0 license header is preserved at the top of
`mediabunny.min.mjs`.
