// Capture WebSocket frames from a real OnSpeed device into NDJSON for
// replay by the dev server.
//
// Usage:
//   node tools/web/dev-server/capture.mjs ws://192.168.0.1:81 > replay/cruise.ndjson
//
// Each text frame becomes one NDJSON line:
//   {"tDelay": <ms since previous frame>, "frame": <parsed JSON>}
// First line's tDelay is 0.  Ctrl-C stops capture and flushes the
// remaining lines to stdout.
//
// Zero npm deps.  Implements just enough of RFC 6455 client framing to
// read text frames from an unmasked server (the firmware doesn't mask
// server-to-client frames, per the RFC).

import net from 'node:net';
import crypto from 'node:crypto';
import { URL } from 'node:url';

function usage() {
  console.error('Usage: node capture.mjs ws://<host>:<port>');
  process.exit(2);
}

const target = process.argv[2];
if (!target) usage();
let url;
try { url = new URL(target); } catch { usage(); }
if (url.protocol !== 'ws:') {
  console.error('Only ws:// is supported (no wss:// in this script)');
  process.exit(2);
}

const host = url.hostname;
const port = parseInt(url.port || '80', 10);

// RFC 6455 client handshake: send Upgrade, expect 101 + accept hash.
const key = crypto.randomBytes(16).toString('base64');
const handshake =
  `GET ${url.pathname || '/'} HTTP/1.1\r\n` +
  `Host: ${host}:${port}\r\n` +
  `Upgrade: websocket\r\n` +
  `Connection: Upgrade\r\n` +
  `Sec-WebSocket-Key: ${key}\r\n` +
  `Sec-WebSocket-Version: 13\r\n\r\n`;

const sock = net.connect(port, host, () => {
  sock.write(handshake);
});

let handshakeBuffer = Buffer.alloc(0);
let handshakeDone = false;
let frameBuffer = Buffer.alloc(0);
let lastFrameMs = null;

sock.on('data', (chunk) => {
  if (!handshakeDone) {
    handshakeBuffer = Buffer.concat([handshakeBuffer, chunk]);
    const idx = handshakeBuffer.indexOf('\r\n\r\n');
    if (idx === -1) return;
    const head = handshakeBuffer.slice(0, idx).toString('utf-8');
    const status = head.split('\r\n')[0];
    if (!status.startsWith('HTTP/1.1 101')) {
      console.error('Handshake failed:', status);
      process.exit(1);
    }
    handshakeDone = true;
    frameBuffer = handshakeBuffer.slice(idx + 4);
    handshakeBuffer = Buffer.alloc(0);
    console.error(`[capture] connected to ${target}`);
  } else {
    frameBuffer = Buffer.concat([frameBuffer, chunk]);
  }
  parseFrames();
});

sock.on('error', (err) => {
  console.error('[capture] socket error:', err.message);
  process.exit(1);
});

sock.on('close', () => {
  console.error('[capture] socket closed');
});

function parseFrames() {
  // Loop while at least one full frame is in frameBuffer.
  while (frameBuffer.length >= 2) {
    const b0 = frameBuffer[0];
    const b1 = frameBuffer[1];
    const opcode = b0 & 0x0f;
    const masked = (b1 & 0x80) !== 0;
    let payloadLen = b1 & 0x7f;
    let offset = 2;
    if (payloadLen === 126) {
      if (frameBuffer.length < 4) return;
      payloadLen = frameBuffer.readUInt16BE(2);
      offset = 4;
    } else if (payloadLen === 127) {
      if (frameBuffer.length < 10) return;
      // Skip high 32 bits — frames are always small.
      payloadLen = frameBuffer.readUInt32BE(6);
      offset = 10;
    }
    let maskKey = null;
    if (masked) {
      if (frameBuffer.length < offset + 4) return;
      maskKey = frameBuffer.slice(offset, offset + 4);
      offset += 4;
    }
    if (frameBuffer.length < offset + payloadLen) return;
    let payload = frameBuffer.slice(offset, offset + payloadLen);
    if (maskKey) {
      const out = Buffer.alloc(payload.length);
      for (let i = 0; i < payload.length; i++) out[i] = payload[i] ^ maskKey[i % 4];
      payload = out;
    }
    frameBuffer = frameBuffer.slice(offset + payloadLen);

    if (opcode === 0x1) {  // text
      handleTextFrame(payload.toString('utf-8'));
    } else if (opcode === 0x8) {  // close
      sock.end();
    } else if (opcode === 0x9) {  // ping
      // Send pong.  Header byte 0x8A (FIN + opcode 0xA), no mask, len=0.
      sock.write(Buffer.from([0x8a, 0x00]));
    }
    // Other opcodes (binary, continuation, pong) are ignored. The
    // OnSpeed feed is JSON text only; the non-text skip is defensive
    // against any future binary producer on this socket.
  }
}

function handleTextFrame(text) {
  let frame;
  try { frame = JSON.parse(text); }
  catch { return; }
  const now = Date.now();
  const tDelay = lastFrameMs === null ? 0 : (now - lastFrameMs);
  lastFrameMs = now;
  process.stdout.write(JSON.stringify({ tDelay, frame }) + '\n');
}

process.on('SIGINT', () => {
  console.error('[capture] Ctrl-C — flushing');
  sock.end();
  process.exit(0);
});
