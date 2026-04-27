---
name: framer-mcp
description: Use when editing Framer sites via the unframer/Framer MCP server. Covers gotchas in updateXmlForNode, auto-injected defaults, breakpoint variants, asset import, button/CTA construction, and when to fall back to the visual editor.
---

# Framer MCP — what to watch for

Framer's MCP (via unframer.co or similar) lets you edit a Framer Studio project's XML tree, CMS, code files, and styles. The tool surface is rich but has sharp edges that cost real time when you don't know about them. This skill captures everything learned the hard way during the FlyONSPEED v2 build.

## Tools you'll use most

- `getProjectXml` — call FIRST in every session to see structure + IDs + attribute documentation. Always shows project-wide style and color paths.
- `getNodeXml` — read a specific node and its descendants by ID.
- `updateXmlForNode` — the workhorse. Edit existing nodes or add new ones via XML.
- `deleteNode` — also deletes color styles, text styles, code files (path or ID).
- `manageColorStyle`, `manageTextStyle` — create design tokens. Reference via `inlineTextStyle="/Path"` and `color="/Path"`.
- `searchFonts` — Google Fonts and Framer's own font library. Returns `selector` strings like `"GF;Source Serif 4-600"`. Required for `font` attribute.
- `getProjectWebsiteUrl` — staging/production URLs.
- `createPage` — new web or design pages. Web page paths must start with `/`.

## Things that bit us hard

### 1. updateXmlForNode does NOT replace contents — it appends or mutates

If you pass an XML tree to `updateXmlForNode` for an existing node, Framer **does not** replace the node's children. It updates the target node's attributes and adds new nodes from your XML *as additional siblings or children*, leaving the originals in place. This causes duplicate content.

**Fix:** before sending a big rewrite, `deleteNode` the obsolete children, then send the new XML. Or send small targeted edits instead of full rewrites.

This is the most expensive single gotcha. The first home-page rewrite of FlyONSPEED v2 ended up with both the original Wireframer copy AND the new Vac-voice copy stacked one after the other, because the rewrite XML appended instead of replacing.

### 2. Framer auto-injects unwanted attribute defaults

Every `updateXmlForNode` call returns the actual saved XML. Look at it. Framer adds attributes you didn't specify:

- New Stacks default to `width="100px"`, `height="100px"`, `backgroundColor="rgba(255,255,255,1)"` (white). If you wanted "fill the parent" you have to write it explicitly.
- Grids get `gridRows="2"`, `gridRowHeight="200"`, `gridColumnWidthType="minmax"`, `gridColumnWidth="200"`, `gridColumnMinWidth="50"` even if you only specified `gridColumns="2"`. Result: phantom second rows of empty space.
- Frames get auto-converted to Stacks if you give them layout attributes.

**Fix:** specify everything explicitly. For containers that should fit children:
```
width="fit-content"   # or "1fr" inside a horizontal stack
height="fit-content"
backgroundColor="rgba(0,0,0,0)"   # transparent — important
```

### 3. Layout="grid" with auto-injected gridRows is a trap

The `gridRows="2"` + `gridRowHeight="200"` defaults create real visible empty space, even when you have only 2 children in a `gridColumns="2"` grid (which should naturally collapse to one row).

**Reliable workaround: use `layout="stack"` with `stackDirection="horizontal"` for 2-column / 3-column layouts instead of `layout="grid"`.** Stacks size to content. No phantom rows. This worked uniformly across FlyONSPEED's home page.

When you genuinely need a CSS grid (say, an asymmetric span or auto-wrapping items), explicitly set `gridRows="1"` and `gridRowHeightType="auto"` and verify the saved XML stripped the auto-defaults.

### 4. Children inside horizontal stacks need width="1fr"

To get equal-width columns in a horizontal stack:
```xml
<Stack layout="stack" stackDirection="horizontal" gap="80px">
  <Column width="1fr" ... />
  <Column width="1fr" ... />
</Stack>
```
`width="100%"` or `width="fit-content"` will make the column collapse to its content's natural width and leave the rest of the row empty.

### 5. Text inside a styled button/pill is finicky

Framer's XML system seems reluctant to nest a *new* text node inside an *existing* container via update. Symptom: you call `updateXmlForNode` on a button frame with new text inside, and instead of nesting the text, Framer adds it as a sibling to the button (so the button stays empty and the text floats next to it).

**Workaround that worked:** delete the existing button container entirely, then update the *parent* with new XML that contains the full button-with-text-child structure. This way the button is created as a new node (not an update target) and Framer correctly nests the text inside.

### 5a. RELIABLE REORDER PATTERN

To move an existing child to a different position within its parent, send a `updateXmlForNode` on the **parent** with a minimal child list in the desired order. Each child only needs its `nodeId`. Example to move `pLmqDamww` to position 0 (before `A8XNYgqc4`):

```xml
<Desktop nodeId="cgyW_kRqW">
  <Stack nodeId="pLmqDamww" />
  <Stack nodeId="A8XNYgqc4" />
</Desktop>
```

Framer responds with `Reordered node pLmqDamww within parent cgyW_kRqW to index 0`. The other children of the parent are NOT deleted — they keep their positions after the explicitly-listed ones. **This is the canonical fix for the "new node appended at the end" gotcha below.**

### 5b. New children are appended to the END of the parent

When you call `updateXmlForNode` with a new XML element under an existing parent, Framer **appends it as the last child**, not the first. If you want the new element at the top of the page (e.g., a nav header), you have two choices:

1. **Use `getProjectXml` to find the parent's first child, then `updateXmlForNode` on the WHOLE PARENT** with the desired children in the right order. This is heavy but correct.
2. **Use Framer's reordering** — there is no direct `reorderNode` tool, but `updateXmlForNode` will reorder if you re-pass the parent's full child list. The output diff will say "Reordered node X within parent Y to index 0" when this works.

Symptom: you add a `<Nav>` element to a page expecting it to render at the top, and instead it appears at the bottom of the page below the footer. This is the cause.

For a multi-page site where you need the same Nav at the top of every page, the right pattern is: build the Nav as a **Component** (not just an XML stack), then insert a `ComponentInstance` at the start of each page. Components auto-update across all pages when you edit them. **Do this from the start; retrofitting nav into 5 separate pages is expensive.**

### 6. Node IDs are sticky; XML output may show duplicates

When `updateXmlForNode` rewrites a parent, it sometimes shows the rewritten node twice in the diff output. This is a diff-rendering artifact, not a real duplicate — verifying with `getNodeXml` confirms the structure is correct. Don't panic at the diff.

### 7. Color attribute on text only works through text styles

`<Text color="rgb(220,213,200)">` is silently ignored. To color text:
- Use `inlineTextStyle="/Some Style"` where the style has the color.
- For dark/light variants of the same hierarchy, create paired styles: `/H1` and `/H1 Light`, `/Body` and `/Body Light`, `/Eyebrow` and `/Eyebrow Light`. The "Light" variants apply over dark backgrounds.

This pairing pattern works well and keeps the design system clean.

### 7e. Internal links require the target page to exist

`updateXmlForNode` will silently reject a `link="/some-path"` if `/some-path` isn't a real page in the project, returning `No changes were made!`. Create the target page via `createPage` BEFORE wiring up internal links to it.

This is doubly painful because the error message ("Make sure you are not using made up attributes, follow the outlined attributes only") points you at the wrong cause — you'll spend time debugging the XML rather than realizing the linked page is missing. When `link=` updates fail, check `getProjectXml` for the page list before assuming it's an attribute issue.

### 7d. User-uploaded assets via Framer's file uploader land on the canvas

When the user uses Framer's "upload file" UI in the editor, the file gets dropped as a top-level image node on whichever page is currently focused — NOT just into the asset library. Symptom: the user reports an image showing up "way at the bottom of the page." The fix:

1. Find the orphan image's nodeId via `getNodeXml` on the page Desktop (it'll be a tag like `<3C039A...Mv2 nodeId="...">` with `backgroundImage` pointing at framerusercontent.com).
2. Read its `backgroundImage` URL (Framer's CDN URL).
3. Apply that URL as `backgroundImage` on the intended placeholder frame.
4. `deleteNode` the orphan.

**Better workflow for the user**: drag the file from Downloads onto the specific empty placeholder rectangle in the editor (drag-onto-canvas, not drag-into-the-asset-panel). Framer will replace the placeholder's backgroundImage in place. No orphan, no cleanup.

### 7c. Wix CDN can hotlink-block specific images

When importing images from `static.wixstatic.com` URLs, **some images return 403** while others succeed, with no obvious pattern (image age? privacy bit on the source site?). Symptom:

```
Failed to upload background image from URL "https://static.wixstatic.com/media/...": 
Assets upload from URL ... failed. Could not get asset, response code 403
```

When this happens, neither the master URL nor the URL with transform parameters works. **Workaround**: download via curl from the user's browser session (which has cookies), upload to a public URL (catbox.moe, S3, GitHub raw), then `backgroundImage=` from there. Or: tell the user to upload the image directly via Framer's editor.

For the FlyONSPEED build, ~10% of Wix images returned 403. Don't waste time retrying with different transforms — same outcome.

### 8. backgroundImage URLs auto-upload

`<Frame backgroundImage="https://external.example.com/foo.jpg" />` will trigger Framer to download and re-host the image at framerusercontent.com. Works for any publicly accessible URL (Wix CDN URLs from `static.wixstatic.com` worked fine).

For local files, the docs suggest `curl -F "fileToUpload=@image.png" https://catbox.moe/user/api.php` first to get a public URL — but this requires shell access and user permission for curl.

### 8b. New children get position="absolute" auto-injected

Closely related to gotcha #5b: when Framer auto-creates new nodes from an XML update, the **new outer Stack often gets `position="absolute"` added** even when you didn't ask for it. This causes the element to layer on top of other content instead of stacking in the parent flow.

**Fix:** check `getNodeXml` after the update. If the new outer container has `position="absolute"`, send a follow-up update setting `position="relative"` (the default for stacked children).

This bites every time you add a new top-level child to a page. Build it into your workflow: every "add new section" update should be followed by a "verify position" check.

### 8c. Plugin disconnects on long updates

The unframer MCP plugin can disconnect mid-update on very large XML payloads (tens of KB), with `code 1001`. The update may have partially succeeded — verify with `getNodeXml` before retrying. The fix is to ask the user to reopen the MCP plugin in Framer (⌘K → MCP → open).

To reduce risk: prefer many smaller targeted updates over one giant page rewrite. The skill is to know when you're past the safe size — roughly, an update with more than ~80 created nodes is at risk.

### 9. There is no "publish" tool in the MCP

Changes appear in the editor immediately. The staging URL only updates when the user clicks Publish in the Framer editor, OR when the user uses the Preview eye icon to view unpublished changes. Tell the user explicitly: "click the Preview eye icon to see changes."

### 10. Wireframer-generated layouts have AI-generic copy

If you start the project with Wireframer (Framer's AI site generator), the output has:
- Inter sans throughout, no serif headlines
- Marketing-speak copy ("brings flight-test roots to the hangar")
- Empty Tablet and Phone breakpoint variants
- Often only the requested pages, partially

Plan to **delete most of the Wireframer copy** in your first edits. Use Wireframer for *structure* only (nav, sections, footer slots) and replace text wholesale.

### 11. Tablet and Phone variants are almost always broken after AI generation

Wireframer leaves them empty placeholders, not auto-mirroring desktop. Two ways forward:
- Build them out via MCP (laborious — every node has to be created inside the variant container).
- Tell the user this is a manual editor task. Visual editor handles responsive in seconds; MCP takes hours.

When in doubt: ship the desktop layout first, surface the responsive issue to the user, and hand off mobile to the visual editor.

## Recommended workflow

1. **`getProjectXml` first.** Always. Memorize the page IDs and style paths. The output also documents every legal attribute — re-read it if you're unsure.
2. **Set up design tokens early.** Color styles + text styles before writing any layout. Then use `inlineTextStyle` and `backgroundColor` references everywhere. Fixing one style updates every place it's used. This is the single biggest leverage point.
3. **Pair Light variants for dark backgrounds.** `/H1`, `/H1 Light`, `/Body`, `/Body Light`, `/Eyebrow`, `/Eyebrow Light`, `/Mono Label`, `/Mono Label Light`. You will need them.
4. **Prefer `layout="stack"` over `layout="grid"`** unless you genuinely need grid. Auto-defaults make grids unreliable.
5. **For containers, always specify** `width`, `height`, `backgroundColor` (use `rgba(0,0,0,0)` for transparent), and the layout attributes. Don't trust defaults.
6. **For columns, use `width="1fr"`** inside horizontal stacks.
7. **Targeted edits beat full rewrites.** A 30-line update to a single Stack is more reliable than a 500-line page rewrite. Iterate small.
8. **Verify with `getNodeXml`** after suspicious updates. The MCP's diff output sometimes lies (looks like duplicates that aren't real); the actual XML is the truth.
9. **Don't try to fight the MCP on visual layout.** If you're 5+ updates deep on a single section and it's still wrong, tell the user "this part is faster in the editor" and move on. Save MCP energy for content, copy, and global styles where it shines.

## Things the MCP is GREAT at

- Bulk content/copy edits across many text nodes
- Setting up the type system and color system once at the start
- CMS schema + items (team grid, FAQ, video library — anything list-shaped)
- Reading the current state of any page and explaining it
- Surgical edits to deeply nested attributes
- Migrating images via URL (Wix CDN → Framer CDN, etc.)

## Things the MCP is BAD at

- Visual layout fine-tuning (spacing, alignment, breakpoint tweaks)
- Tablet/Phone breakpoint propagation
- Anything where you'd be faster with a mouse and the editor's preview
- Forms (use Framer's built-in form widget via the editor)
- Complex component composition (use Framer's pre-built section components when possible — they live at `https://framer.com/m/sections-Hero-2xJX.js?detached=true` etc., shown in `getProjectXml` output)

## Pre-built section components

`getProjectXml` returns a list of pre-built section components in its instructions. To use one:
```xml
<ComponentInstance insertUrl="https://framer.com/m/sections-Hero-2xJX.js?detached=true" position="relative" width="100%" />
```
The `?detached=true` makes it editable. Then call `getNodeXml` on the page to see what nodes the section actually created, and customize from there. This is faster than building hero/footer sections from scratch.

## When to call back to the user

- The Framer plugin has disconnected (error: "Framer plugin not connected"). User needs to open Framer, ⌘K, search MCP, open the plugin window. The plugin tab must stay open in their browser for the MCP to work.
- A layout is going wrong on multiple iterations. Tell the user — don't burn cycles. Ask if they want you to keep going via MCP or hand off to the editor.
- Mobile/tablet variants need attention. Default position: ship desktop first, surface this as known follow-up.

## Specific patterns that worked for FlyONSPEED v2 heritage aesthetic

For reference. Adjust to whatever the current project needs.

**Color palette (mid-century military / heritage):**
- `/Brand/Ink` — `rgb(20, 24, 28)` near-black for body text
- `/Brand/Paper` — `rgb(248, 245, 238)` warm off-white background
- `/Brand/Accent` — `rgb(196, 86, 36)` signal orange (USAF technical-manual-style)
- `/Brand/Muted` — `rgb(106, 106, 100)` subdued gray-green
- `/Brand/Rule` — `rgb(220, 213, 200)` for hairline borders

**Type pairing:**
- Headlines: `GF;Source Serif 4-600` — restrained authoritative serif
- Body: `GF;IBM Plex Sans-regular` — technical-manual feel, way less generic than Inter
- Eyebrows / mono labels: `FS;JetBrains Mono-medium` (uppercase, letter-spaced)

These specific choices were chosen to fight the AI-generic-Wix-Inter-everywhere look. They worked.

## Reminders / housekeeping

- The unframer MCP URL contains an `id` and `secret`. Treat as a credential. Rotate after sensitive sessions.
- Plugin must stay open in a browser tab for the MCP to send commands.
- The user will be the one clicking Publish. The MCP cannot publish.
