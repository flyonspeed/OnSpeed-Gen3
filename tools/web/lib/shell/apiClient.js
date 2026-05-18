// API client for OnSpeed firmware HTTP endpoints.
//
// Base URL resolution mirrors the wsClient pattern:
//
// - Firmware build:  no `<meta name="onspeed-mode">` tag → base = "".
//   `getJson('/api/foo')` becomes a same-origin fetch.
// - Dev-server proxy: dev-server inserts `<meta name="onspeed-mode"
//   content="proxy http://192.168.0.1">`.  Base = the device URL.
// - Dev-server mock:  dev-server inserts `<meta name="onspeed-mode"
//   content="mock">`.  Base = "" — the dev-server itself answers
//   /api/* from its mocks/ directory.
//
// Throws an `ApiError` on non-2xx responses with `{status, message,
// errors?}` so callers can render path-keyed validation errors.

function readMode() {
  if (typeof document === 'undefined') return { mode: 'mock', base: '' };
  const meta = document.querySelector('meta[name="onspeed-mode"]');
  if (!meta || !meta.content) return { mode: 'firmware', base: '' };
  const content = meta.content.trim();
  if (content === 'mock') return { mode: 'mock', base: '' };
  if (content.startsWith('proxy ')) {
    return { mode: 'proxy', base: content.slice('proxy '.length).trim() };
  }
  return { mode: 'firmware', base: '' };
}

export class ApiError extends Error {
  constructor(status, message, errors) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.errors = errors || null;
  }
}

function buildUrl(path) {
  const { base } = readMode();
  if (!base) return path;
  if (path.startsWith('http://') || path.startsWith('https://')) return path;
  return base.replace(/\/$/, '') + path;
}

async function parseError(response) {
  let body = null;
  try { body = await response.json(); }
  catch { body = { message: response.statusText }; }
  // Firmware SendError() shape is {"ok":false,"errors":[{"path":"...","message":"..."}]}
  // — surface the first error's message when no top-level message is set,
  // so pages get a useful string instead of "Service Unavailable".
  const message = body?.message
    || body?.errors?.[0]?.message
    || response.statusText
    || `HTTP ${response.status}`;
  throw new ApiError(response.status, message, body?.errors);
}

async function getJsonImpl(path, { signal } = {}) {
  const response = await fetch(buildUrl(path), {
    method: 'GET',
    headers: { 'Accept': 'application/json' },
    signal,
  });
  if (!response.ok) await parseError(response);
  return response.json();
}

// GET with retry on 503. The firmware emits 503 from /api/logs when the
// SD writer is holding xWriteMutex (sidecar refresh, card stall). The
// 503 carries a Retry-After header indicating seconds; the call retries
// up to `maxAttempts` total times, honoring Retry-After if present
// (capped to 3s per attempt to keep UX responsive).
//
// `onAttempt(attempt, lastError)` is an optional callback so the page
// can show "retrying… (attempt 2/4)" feedback while the call waits.
async function getJsonWithRetryImpl(path, {
  signal, maxAttempts = 4, onAttempt = null,
} = {}) {
  let lastError = null;
  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    if (onAttempt) onAttempt(attempt, lastError);
    try {
      const response = await fetch(buildUrl(path), {
        method: 'GET',
        headers: { 'Accept': 'application/json' },
        signal,
      });
      if (response.ok) return response.json();
      // Only retry on 503 (writer mutex busy). Other errors bubble up
      // immediately — they aren't going to fix themselves.
      if (response.status !== 503 || attempt === maxAttempts) {
        await parseError(response);
      }
      const retryAfterHeader = response.headers.get('Retry-After');
      const retryAfterSec = retryAfterHeader ? Math.min(3, parseFloat(retryAfterHeader)) : 1;
      lastError = new ApiError(503, 'SD busy, retrying…', null);
      await new Promise((resolve) => setTimeout(resolve, retryAfterSec * 1000));
    } catch (e) {
      // Non-503 errors propagated by parseError, or fetch aborts.
      // Don't retry on abort.
      if (e?.name === 'AbortError') throw e;
      throw e;
    }
  }
  throw lastError || new ApiError(503, 'SD busy after retries', null);
}

async function postJsonImpl(path, body, { signal } = {}) {
  const response = await fetch(buildUrl(path), {
    method: 'POST',
    headers: {
      'Accept': 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
    signal,
  });
  if (!response.ok) await parseError(response);
  return response.json();
}

async function postFormImpl(path, formData, { signal } = {}) {
  const response = await fetch(buildUrl(path), {
    method: 'POST',
    body: formData,
    signal,
  });
  if (!response.ok) await parseError(response);
  // Form posts may return non-JSON (legacy endpoints); return text in
  // that case.
  const ct = response.headers.get('Content-Type') || '';
  return ct.includes('application/json') ? response.json() : response.text();
}

export const getJson = getJsonImpl;
export const getJsonWithRetry = getJsonWithRetryImpl;
export const postJson = postJsonImpl;
export const postForm = postFormImpl;

export function currentMode() {
  return readMode();
}
