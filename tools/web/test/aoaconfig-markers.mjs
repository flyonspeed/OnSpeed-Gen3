// Lockstep test for /aoaconfig template markers.
//
// The /aoaconfig page is rendered by substituting {{markerName}}
// placeholders in tools/web/legacy-pages/aoaconfig.html.  Two engines
// do that substitution today:
//
//   1. Firmware: HandleConfig() in
//      software/sketch_common/src/web_server/ConfigWebServer.cpp.
//   2. Dev-server: substituteAoaConfig() in
//      tools/web/dev-server/server.mjs.
//
// If a marker is added to the HTML and only one engine learns to
// substitute it, the page renders with literal {{marker}} text in the
// browser (firmware) or in the dev shell (dev-server).  This test
// fails CI when the three views drift.
//
// Approach: extract the set of marker names from each source, then
// assert all three sets are equal.  Uses a small registry helper so
// loop-generated markers (e.g. portsOrUpSel..portsOrAftSel) can be
// expanded without false negatives.
//
// Run with:  node tools/web/test/aoaconfig-markers.mjs
// Exit 0 = match, 1 = drift.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '..', '..', '..');

const HTML_PATH = path.join(ROOT, 'tools', 'web', 'legacy-pages', 'aoaconfig.html');
const CPP_PATH  = path.join(ROOT, 'software', 'sketch_common', 'src',
                            'web_server', 'ConfigWebServer.cpp');
const JS_PATH   = path.join(ROOT, 'tools', 'web', 'dev-server', 'server.mjs');

// ---------------------------------------------------------------------
// HTML markers: drop comment blocks, then scan for {{name}}.
// ---------------------------------------------------------------------
function extractHtmlMarkers(htmlSrc) {
  const stripped = htmlSrc.replace(/<!--[\s\S]*?-->/g, '');
  const set = new Set();
  for (const m of stripped.matchAll(/\{\{([A-Za-z0-9_.]+)\}\}/g)) {
    set.add(m[1]);
  }
  return set;
}

// ---------------------------------------------------------------------
// C++ markers: limit to the body of HandleConfig() to avoid pulling
// markers from unrelated handlers in the same translation unit.
// ---------------------------------------------------------------------
function stripCppComments(src) {
  // Strip /* ... */ blocks and // ... line comments.  Naive but
  // adequate: HandleConfig has no string literals containing '//'
  // followed by braces, so we don't need to track strings.
  return src
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/[^\n]*/g, '');
}

function extractCppMarkers(cppSrc) {
  const startRe = /^void\s+HandleConfig\(\)\s*$/m;
  const startMatch = startRe.exec(cppSrc);
  if (!startMatch) {
    throw new Error('Could not find void HandleConfig() in ConfigWebServer.cpp');
  }
  const startIdx = startMatch.index;
  // Walk forward to the next top-level '}' (matched braces from depth 0).
  let depth = 0;
  let i = startIdx;
  let sawOpen = false;
  for (; i < cppSrc.length; i++) {
    const c = cppSrc[i];
    if (c === '{') { depth++; sawOpen = true; }
    else if (c === '}') {
      depth--;
      if (sawOpen && depth === 0) { i++; break; }
    }
  }
  const body = stripCppComments(cppSrc.slice(startIdx, i));
  const set = new Set();
  for (const m of body.matchAll(/\{\{([A-Za-z0-9_.]+)\}\}/g)) {
    set.add(m[1]);
  }
  return set;
}

// ---------------------------------------------------------------------
// JS markers: limit to the body of substituteAoaConfig().
//
// Some markers are written as template literals, e.g.:
//
//     for (const o of ['Up', 'Down', ...]) {
//       body.replaceAll(`{{portsOr${o}Sel}}`, ...);
//     }
//
// To handle these, scan once for literal {{name}} placeholders and
// once for each `for (const X of [...])` array applied to the
// surrounding template literal.
// ---------------------------------------------------------------------
function extractJsMarkers(jsSrc) {
  const startRe = /function\s+substituteAoaConfig\s*\(/;
  const startMatch = startRe.exec(jsSrc);
  if (!startMatch) {
    throw new Error('Could not find function substituteAoaConfig() in server.mjs');
  }
  // Walk forward to first '{' (function body open), then match braces.
  let i = startMatch.index;
  while (i < jsSrc.length && jsSrc[i] !== '{') i++;
  const bodyStart = i;
  let depth = 0;
  for (; i < jsSrc.length; i++) {
    const c = jsSrc[i];
    if (c === '{') depth++;
    else if (c === '}') {
      depth--;
      if (depth === 0) { i++; break; }
    }
  }
  const body = stripCppComments(jsSrc.slice(bodyStart, i));

  const set = new Set();
  // 1. Plain {{name}} placeholders.
  for (const m of body.matchAll(/\{\{([A-Za-z0-9_.]+)\}\}/g)) {
    set.add(m[1]);
  }
  // 2. Template-literal placeholders: `{{prefix${var}suffix}}` paired
  //    with a nearby `for (const VAR of [array])` loop.
  const tplRe = /`\{\{([A-Za-z0-9_]*)\$\{([A-Za-z0-9_]+)\}([A-Za-z0-9_.]*)\}\}`/g;
  for (const m of body.matchAll(tplRe)) {
    const [, prefix, varName, suffix] = m;
    // Find the for-loop that binds varName.  Search backward from m.index
    // for `for (const VAR of [ ... ])`.
    const forRe = new RegExp(
      'for\\s*\\(\\s*const\\s+' + varName + '\\s+of\\s*(\\[[^\\]]*\\])',
      'g'
    );
    let bestArr = null;
    for (const f of body.slice(0, m.index).matchAll(forRe)) {
      bestArr = f[1];
    }
    if (!bestArr) continue;
    const elements = JSON.parse(bestArr.replace(/'/g, '"'));
    for (const e of elements) {
      set.add(prefix + e + suffix);
    }
  }
  return set;
}

// ---------------------------------------------------------------------
// Set utilities + assertion.
// ---------------------------------------------------------------------
function diff(a, b) {
  const out = [];
  for (const x of a) if (!b.has(x)) out.push(x);
  out.sort();
  return out;
}

function reportSet(label, set) {
  const arr = [...set].sort();
  console.log(`  ${label} (${arr.length}):`);
  for (const x of arr) console.log(`    ${x}`);
}

function main() {
  const html = fs.readFileSync(HTML_PATH, 'utf8');
  const cpp  = fs.readFileSync(CPP_PATH,  'utf8');
  const js   = fs.readFileSync(JS_PATH,   'utf8');

  const htmlSet = extractHtmlMarkers(html);
  const cppSet  = extractCppMarkers(cpp);
  const jsSet   = extractJsMarkers(js);

  let failed = 0;
  function check(label, missingFromBig, missingFromSmall) {
    if (missingFromBig.length === 0 && missingFromSmall.length === 0) {
      console.log(`PASS  ${label}`);
      return;
    }
    failed++;
    console.log(`FAIL  ${label}`);
    if (missingFromBig.length)   console.log('  in HTML, missing here: ' + missingFromBig.join(', '));
    if (missingFromSmall.length) console.log('  here but unused by HTML: ' + missingFromSmall.join(', '));
  }

  check('HTML markers == C++ HandleConfig substitutions',
        diff(htmlSet, cppSet), diff(cppSet, htmlSet));
  check('HTML markers == JS substituteAoaConfig substitutions',
        diff(htmlSet, jsSet), diff(jsSet, htmlSet));

  console.log('');
  console.log(`HTML:  ${htmlSet.size} markers`);
  console.log(`C++:   ${cppSet.size}`);
  console.log(`JS:    ${jsSet.size}`);

  if (failed) {
    console.log('');
    reportSet('HTML', htmlSet);
    reportSet('C++',  cppSet);
    reportSet('JS',   jsSet);
    process.exit(1);
  }
}

main();
