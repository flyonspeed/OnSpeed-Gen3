#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
#     "websockets>=12",
# ]
# ///
"""
Stress test for OnSpeed Gen3 firmware web-handler reliability under
sustained 208 Hz logging load.

Goal: hammer every web endpoint that takes xWriteMutex and verify
none of them silent-fail or starve under load. Pairs with the
post-SD-integrity-merge bug class where the writer was starving
non-writer mutex takers.

Endpoints exercised:
  - GET /api/logs                  (file listing — must not 503)
  - GET /  (and /aoaconfig, etc.)  (main pages — must serve quickly)
  - GET /static/app-*.{js,css}     (bundle — PROGMEM, baseline)
  - POST /aoaconfigsave            (config save — must persist or banner)
  - GET /download?file=...         (download — paused but must succeed)
  - WebSocket /                    (live feed — must stay connected)

Run with `uv` (recommended — PEP 723 metadata above resolves deps):
  uv run ./stress_web_handlers.py

Or with pip (deps must match the PEP 723 block above):
  pip install "websockets>=12" "pyserial>=3.5"
  python3 stress_web_handlers.py

Options:
  --duration MIN     Test duration in minutes (default 10)
  --no-saves         Skip config-save POSTs (less invasive)
  --no-downloads     Skip the /download stress (no paused_drops)
  --aggressive       Tighter cadence — useful for catching races
  --host IP          OnSpeed AP IP (default 192.168.0.1)
  --probe            One-shot connectivity check, then exit

Hit Ctrl-C to stop early. Final summary prints success/failure counts
per endpoint plus the longest observed response time.
"""

import argparse
import asyncio
import glob
import json
import os
import random
import signal
import statistics
import sys
import threading
import time
import urllib.parse
import urllib.request
import urllib.error

DEFAULT_HOST = "192.168.0.1"
WS_PORT = 81

# Serial capture defaults. We try a few common port glob patterns
# because macOS occasionally renames the device number after a
# reconnect (cu.usbserial-310 vs cu.usbserial-510).
DEFAULT_BAUD = 921600
SERIAL_GLOBS = [
    "/dev/cu.usbserial-*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.usbmodem*",
]


def find_serial_port():
    """Return the first matching USB-serial device, or None."""
    for pattern in SERIAL_GLOBS:
        matches = glob.glob(pattern)
        if matches:
            return sorted(matches)[0]
    return None


class SerialCapture:
    """Background thread that reads serial lines and writes them to a
    file plus an in-memory counter. Robust to mid-run unplug-replug:
    if the serial read fails, we sleep briefly and try to reopen.
    """
    def __init__(self, port, baud, out_path):
        self.port = port
        self.baud = baud
        self.out_path = out_path
        self.line_count = 0
        self.error_count = 0
        self.stop_flag = False
        self.thread = None
        self.perf_count = 0
        self.imu_late_count = 0
        self.imu_late_max_us = 0
        self.drops_total = 0
        self.paused_drops_total = 0
        self.short_total = 0
        self.dbg_drops_total = 0
        self.write_max_peak = 0
        self.sync_max_peak = 0
        self.config_save_retry_lines = 0
        self.config_save_fail_lines = 0
        self.config_save_ok_lines = 0
        self.api_logs_mutex_timeout_lines = 0
        self.format_lines = []

    def start(self):
        try:
            import serial  # imported lazily so the script still runs without pyserial
        except ImportError:
            print(f"{C.YELLOW}serial capture: pyserial not installed; skipping{C.RESET}")
            return False
        self.thread = threading.Thread(target=self._run, args=(serial,), daemon=True)
        self.thread.start()
        return True

    def stop(self):
        self.stop_flag = True
        if self.thread:
            self.thread.join(timeout=2.0)

    def _run(self, serial_mod):
        import re
        last_open_attempt = 0
        s = None
        out_f = open(self.out_path, 'w', buffering=1)  # line-buffered
        out_f.write(f"# OnSpeed serial capture started at {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        try:
            while not self.stop_flag:
                if s is None:
                    now = time.time()
                    if now - last_open_attempt < 1.0:
                        time.sleep(0.5)
                        continue
                    last_open_attempt = now
                    try:
                        s = serial_mod.Serial(self.port, self.baud, timeout=1)
                    except Exception as e:
                        self.error_count += 1
                        time.sleep(2.0)
                        continue
                try:
                    line = s.readline()
                except Exception:
                    self.error_count += 1
                    try: s.close()
                    except: pass
                    s = None
                    continue
                if not line: continue
                try:
                    txt = line.decode('utf-8', errors='replace').rstrip()
                except: continue
                if not txt: continue
                self.line_count += 1
                out_f.write(f"{time.time():.3f} {txt}\n")
                # Counters
                if 'PERF' in txt:
                    self.perf_count += 1
                    m = re.search(r'write_max=(\d+)us', txt)
                    if m: self.write_max_peak = max(self.write_max_peak, int(m.group(1)))
                    m = re.search(r'sync_max=(\d+)us', txt)
                    if m: self.sync_max_peak = max(self.sync_max_peak, int(m.group(1)))
                    m = re.search(r'imu_lateMaxUs=(\d+)', txt)
                    if m: self.imu_late_max_us = max(self.imu_late_max_us, int(m.group(1)))
                    m = re.search(r'imu_late=(\d+)', txt)
                    if m: self.imu_late_count += int(m.group(1))
                    m = re.search(r' drops=(\d+)', txt)
                    if m: self.drops_total += int(m.group(1))
                    m = re.search(r'paused_drops=(\d+)', txt)
                    if m: self.paused_drops_total += int(m.group(1))
                    m = re.search(r' short=(\d+)', txt)
                    if m: self.short_total += int(m.group(1))
                    m = re.search(r'dbg_drops=(\d+)', txt)
                    if m: self.dbg_drops_total += int(m.group(1))
                if 'Config save: xWriteMutex busy' in txt:
                    self.config_save_retry_lines += 1
                if 'Saved config file to SD card' in txt:
                    self.config_save_ok_lines += 1
                if 'Could not save config file' in txt:
                    self.config_save_fail_lines += 1
                if '/api/logs: mutex timeout' in txt:
                    self.api_logs_mutex_timeout_lines += 1
                if 'Format:' in txt:
                    self.format_lines.append(txt)
        finally:
            out_f.write(f"# capture ended at {time.strftime('%Y-%m-%d %H:%M:%S')}, "
                        f"lines={self.line_count}, errors={self.error_count}\n")
            out_f.close()
            if s is not None:
                try: s.close()
                except: pass

    def summary(self):
        lines = [
            f"  lines captured: {self.line_count}  (errors: {self.error_count})",
            f"  PERF emits: {self.perf_count}",
            f"  drops: {self.drops_total}  paused_drops: {self.paused_drops_total}  short: {self.short_total}  dbg_drops: {self.dbg_drops_total}",
            f"  imu_late events: {self.imu_late_count}  imu_lateMaxUs peak: {self.imu_late_max_us}us",
            f"  write_max peak: {self.write_max_peak}us  sync_max peak: {self.sync_max_peak}us",
            f"  config save: ok={self.config_save_ok_lines} retries={self.config_save_retry_lines} failed={self.config_save_fail_lines}",
            f"  /api/logs mutex timeouts: {self.api_logs_mutex_timeout_lines}",
            f"  format lines: {len(self.format_lines)}",
        ]
        return "\n".join(lines)

class C:
    RESET = "\033[0m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    RED = "\033[31m"
    DIM = "\033[2m"
    BOLD = "\033[1m"
    CYAN = "\033[36m"

class EndpointStats:
    __slots__ = ('name', 'ok', 'errors', 'retries_503', 'durations_ms', 'last_error')
    def __init__(self, name):
        self.name = name
        self.ok = 0
        self.errors = 0
        self.retries_503 = 0
        self.durations_ms = []
        self.last_error = None
    def record_ok(self, ms):
        self.ok += 1
        self.durations_ms.append(ms)
    def record_err(self, ms, msg):
        self.errors += 1
        self.last_error = msg
        self.durations_ms.append(ms)
    def summary(self):
        n = len(self.durations_ms)
        if n == 0: return f"  {self.name}: (no requests)"
        d = self.durations_ms
        mean = sum(d) / n
        p95 = sorted(d)[int(n * 0.95)] if n > 1 else d[0]
        peak = max(d)
        color = C.GREEN if self.errors == 0 else (C.YELLOW if self.errors < self.ok else C.RED)
        s = (f"  {color}{self.name}{C.RESET}: ok={self.ok} err={self.errors} retries={self.retries_503} | "
             f"mean={mean:.0f}ms p95={p95:.0f}ms peak={peak:.0f}ms")
        if self.last_error: s += f" | last_err={self.last_error}"
        return s

stats = {
    'GET /api/logs':         EndpointStats('GET /api/logs'),
    'GET /aoaconfig':        EndpointStats('GET /aoaconfig'),
    'GET / (index)':         EndpointStats('GET / (index)'),
    'GET /static/*.js':      EndpointStats('GET /static/*.js'),
    'GET /download':         EndpointStats('GET /download'),
    'POST /aoaconfigsave':   EndpointStats('POST /aoaconfigsave'),
    'WebSocket':             EndpointStats('WebSocket'),
}
ws_frames = 0
ws_reconnects = 0
start_time = time.time()

stop = False
sigint_count = 0
def handle_sigint(signum, frame):
    global stop, sigint_count
    sigint_count += 1
    stop = True
    if sigint_count == 1:
        print(f"\n{C.YELLOW}Stopping... (press Ctrl-C again to force-exit){C.RESET}")
    else:
        # Hard exit. Workers may be stuck in urllib.urlopen() with a
        # long timeout; we don't want to wait.
        print(f"\n{C.RED}Force exit.{C.RESET}")
        os._exit(130)
signal.signal(signal.SIGINT, handle_sigint)


def sleep_stoppable(total_secs):
    """Sleep up to total_secs but wake up fast if stop is set.
    Used by worker threads so Ctrl-C is responsive."""
    end = time.time() + total_secs
    while not stop and time.time() < end:
        remaining = end - time.time()
        time.sleep(min(0.1, remaining))


def log(msg, color=None):
    elapsed = int(time.time() - start_time)
    m, s = divmod(elapsed, 60)
    prefix = f"{C.DIM}[{m:3d}:{s:02d}]{C.RESET} "
    if color: msg = f"{color}{msg}{C.RESET}"
    print(prefix + msg, flush=True)


def http_get(url, timeout=10, retry_503=True, max_retries=4):
    """GET with automatic retry on 503. Returns (status, body_len, ms, retry_count).

    Advertises `Accept-Encoding: gzip` so the firmware exercises its
    compression path the same way a real browser would. Body length is
    reported as the *decompressed* size so endpoint stats stay comparable
    across firmware versions with and without gzip enabled.
    """
    import gzip
    import io
    retries = 0
    t0 = time.time()
    while True:
        try:
            req = urllib.request.Request(url, headers={'Accept-Encoding': 'gzip'})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                raw = resp.read()
                # If the server compressed, decompress so body_len reflects
                # the logical (post-decompression) size. urllib does NOT
                # auto-decompress in Python 3.
                if resp.headers.get('Content-Encoding', '').lower() == 'gzip':
                    body = gzip.decompress(raw)
                else:
                    body = raw
                return (resp.status, len(body), int((time.time() - t0) * 1000), retries)
        except urllib.error.HTTPError as e:
            if e.code == 503 and retry_503 and retries < max_retries:
                # Honor Retry-After header.
                retry_after = e.headers.get('Retry-After', '1')
                try: wait = min(3.0, float(retry_after))
                except: wait = 1.0
                time.sleep(wait)
                retries += 1
                continue
            return (e.code, 0, int((time.time() - t0) * 1000), retries)
        except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
            return (0, 0, int((time.time() - t0) * 1000), retries)


def http_post_form(url, fields, timeout=15):
    """POST form-encoded body. Returns (status, body_len, ms)."""
    t0 = time.time()
    data = urllib.parse.urlencode(fields).encode('utf-8')
    try:
        req = urllib.request.Request(url, data=data, method='POST',
                                     headers={'Content-Type': 'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read()
            return (resp.status, len(body), int((time.time() - t0) * 1000))
    except urllib.error.HTTPError as e:
        return (e.code, 0, int((time.time() - t0) * 1000))
    except Exception:
        return (0, 0, int((time.time() - t0) * 1000))


def find_static_js_path(host):
    """Scrape the /aoaconfig HTML for the current static JS asset URL.
    Returns a path like /static/app-3a7f81b2.js, or None if not found."""
    import gzip
    try:
        req = urllib.request.Request(
            f"http://{host}/aoaconfig",
            headers={'Accept-Encoding': 'gzip'})
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status != 200: return None
            raw = resp.read()
            if resp.headers.get('Content-Encoding', '').lower() == 'gzip':
                raw = gzip.decompress(raw)
            body = raw.decode('utf-8', errors='replace')
    except Exception: return None
    import re
    m = re.search(r'"(/static/app-[a-f0-9]+\.js)"', body)
    return m.group(1) if m else None


# ------------------------------------------------------------------------
# Workers (each runs concurrently against the box)
# ------------------------------------------------------------------------

def worker_logs(host, end_time, cadence_range):
    """Hit /api/logs periodically."""
    while not stop and time.time() < end_time:
        sleep_stoppable(random.uniform(*cadence_range))
        if stop: break
        status, n, ms, retries = http_get(f"http://{host}/api/logs", timeout=15)
        s = stats['GET /api/logs']
        if retries > 0: s.retries_503 += retries
        if status == 200: s.record_ok(ms); log(f"GET /api/logs → 200 ({n}b, {ms}ms, retries={retries})", C.GREEN)
        else: s.record_err(ms, f"status={status}"); log(f"GET /api/logs → {status} ({ms}ms)", C.RED)


def worker_pages(host, end_time, cadence_range):
    """Hit main HTML pages periodically."""
    pages = [('GET /aoaconfig', '/aoaconfig'),
             ('GET / (index)', '/')]
    while not stop and time.time() < end_time:
        sleep_stoppable(random.uniform(*cadence_range))
        if stop: break
        name, path = random.choice(pages)
        status, n, ms, retries = http_get(f"http://{host}{path}", timeout=10, retry_503=False)
        s = stats[name]
        if status == 200: s.record_ok(ms); log(f"{name} → 200 ({n}b, {ms}ms)", C.GREEN)
        elif status == 503: s.record_err(ms, '503'); log(f"{name} → 503 ({ms}ms)", C.YELLOW)
        else: s.record_err(ms, f"status={status}"); log(f"{name} → {status} ({ms}ms)", C.RED)


def worker_static(host, end_time, cadence_range):
    """Hit /static/*.js (PROGMEM, no SD)."""
    js_path = find_static_js_path(host)
    if not js_path:
        log(f"static: could not find JS path; skipping", C.YELLOW); return
    log(f"static: discovered {js_path}", C.DIM)
    while not stop and time.time() < end_time:
        sleep_stoppable(random.uniform(*cadence_range))
        if stop: break
        status, n, ms, _ = http_get(f"http://{host}{js_path}", timeout=10, retry_503=False)
        s = stats['GET /static/*.js']
        if status in (200, 304): s.record_ok(ms); log(f"GET {js_path} → {status} ({n}b, {ms}ms)", C.GREEN)
        else: s.record_err(ms, f"status={status}"); log(f"GET {js_path} → {status} ({ms}ms)", C.RED)


def worker_download(host, end_time, cadence_range):
    """Periodic small-file download (intentionally causes paused_drops)."""
    while not stop and time.time() < end_time:
        sleep_stoppable(random.uniform(*cadence_range))
        if stop: break
        status, n, ms, _ = http_get(f"http://{host}/download?file=boot_log.txt", timeout=20, retry_503=False)
        s = stats['GET /download']
        if status == 200: s.record_ok(ms); log(f"GET /download → 200 ({n}b, {ms}ms)  EXPECTS paused_drops > 0", C.YELLOW)
        else: s.record_err(ms, f"status={status}"); log(f"GET /download → {status} ({ms}ms)", C.RED)


def snapshot_form_fields(host):
    """Fetch /aoaconfig and extract every <input>/<select>/<textarea> name
    and current value. Returns a dict suitable for re-POSTing to
    /aoaconfigsave without mutating config (the form values reflect
    the current in-memory state).

    Returns None on failure — caller should skip the save worker."""
    import re
    import gzip
    try:
        req = urllib.request.Request(
            f"http://{host}/aoaconfig",
            headers={'Accept-Encoding': 'gzip'})
        with urllib.request.urlopen(req, timeout=15) as resp:
            raw = resp.read()
            if resp.headers.get('Content-Encoding', '').lower() == 'gzip':
                raw = gzip.decompress(raw)
            html = raw.decode('utf-8', errors='replace')
    except Exception:
        return None
    fields = {}
    # <input name="X" ... value="Y"> — value attr might come before or after name.
    for m in re.finditer(r'<input\b([^>]*)>', html, re.IGNORECASE):
        attrs = m.group(1)
        name_m = re.search(r'\bname\s*=\s*["\']([^"\']+)["\']', attrs)
        if not name_m: continue
        name = name_m.group(1)
        value_m = re.search(r'\bvalue\s*=\s*["\']([^"\']*)["\']', attrs)
        type_m = re.search(r'\btype\s*=\s*["\']([^"\']+)["\']', attrs)
        itype = type_m.group(1).lower() if type_m else 'text'
        if itype in ('checkbox', 'radio'):
            # Only include checked ones (mirrors browser POST behavior).
            if re.search(r'\bchecked\b', attrs, re.IGNORECASE):
                fields[name] = value_m.group(1) if value_m else 'on'
        elif itype == 'submit':
            continue
        else:
            fields[name] = value_m.group(1) if value_m else ''
    # <select name="X">...<option ... selected>VAL</option>...</select>
    for m in re.finditer(r'<select\b([^>]*)>(.*?)</select>', html, re.IGNORECASE | re.DOTALL):
        attrs, body = m.group(1), m.group(2)
        name_m = re.search(r'\bname\s*=\s*["\']([^"\']+)["\']', attrs)
        if not name_m: continue
        name = name_m.group(1)
        sel_m = re.search(r'<option\b[^>]*\bselected\b[^>]*value\s*=\s*["\']([^"\']*)["\']', body, re.IGNORECASE)
        if not sel_m:
            sel_m = re.search(r'<option\b[^>]*value\s*=\s*["\']([^"\']*)["\'][^>]*\bselected\b', body, re.IGNORECASE)
        if sel_m: fields[name] = sel_m.group(1)
    return fields


def worker_save(host, end_time, cadence_range):
    """POST /aoaconfigsave with the form's current values, verbatim.

    Critical to use the full form snapshot rather than a partial body:
    the handler in HandleConfigSave() treats missing booleans as 'user
    cleared them' (the boolean-default-false pattern visible across
    ConfigWebServer.cpp's checkbox handlers), and missing flap fields
    would wipe the flap config. A round-trip snapshot/POST is the only
    safe way to stress the save endpoint without nuking the box.

    Refreshes the snapshot every N saves so it picks up any config
    drift mid-test.
    """
    fields = snapshot_form_fields(host)
    if fields is None:
        log(f"save: could not snapshot /aoaconfig form; skipping save worker", C.YELLOW)
        return
    log(f"save: snapshotted {len(fields)} form fields (incl. logRate={fields.get('logRate','?')})", C.CYAN)
    save_count = 0
    while not stop and time.time() < end_time:
        sleep_stoppable(random.uniform(*cadence_range))
        if stop: break
        status, n, ms = http_post_form(f"http://{host}/aoaconfigsave", fields, timeout=15)
        s = stats['POST /aoaconfigsave']
        if status == 200: s.record_ok(ms); log(f"POST /aoaconfigsave → 200 ({ms}ms)", C.GREEN)
        else: s.record_err(ms, f"status={status}"); log(f"POST /aoaconfigsave → {status} ({ms}ms)", C.RED)
        save_count += 1
        # Refresh the snapshot every 20 saves to catch any drift.
        if save_count % 20 == 0:
            fresh = snapshot_form_fields(host)
            if fresh: fields = fresh


async def worker_websocket(host, end_time, worker_id):
    """Hold one WebSocket connection open. Reconnects on drop until end_time.

    Multiple instances run concurrently to exercise the per-client
    buffer paths in arduinoWebSockets — the original #355 WebSocket
    double-cleanup panic surfaced when multiple clients churned. This
    test holds N steady clients while the chaos worker periodically
    kicks one of them, which forces the server to clean up the slot.
    """
    global ws_frames, ws_reconnects
    import websockets
    uri = f"ws://{host}:{WS_PORT}/"
    while not stop and time.time() < end_time:
        try:
            log(f"WS#{worker_id} connecting", C.DIM)
            async with websockets.connect(uri, ping_interval=20) as ws:
                # Do not increment stats['WebSocket'].ok here — that counter
                # would conflate "successful connects" with "successful
                # responses" (the meaning used by every other endpoint).
                # WS health is reported separately via ws_frames + reconnect
                # count in the summary.
                log(f"WS#{worker_id} connected", C.GREEN)
                while not stop and time.time() < end_time:
                    msg = await asyncio.wait_for(ws.recv(), timeout=15)
                    ws_frames += 1
                    if ws_frames % 500 == 0:
                        log(f"WS frames total: {ws_frames}", C.DIM)
        except asyncio.TimeoutError:
            log(f"WS#{worker_id} frame timeout, reconnecting", C.YELLOW)
            ws_reconnects += 1
            stats['WebSocket'].record_err(15000, 'timeout')
        except Exception as e:
            log(f"WS#{worker_id} error: {type(e).__name__}: {e}", C.YELLOW)
            ws_reconnects += 1
            stats['WebSocket'].record_err(0, f"{type(e).__name__}")
            await asyncio.sleep(0.5)


async def worker_websocket_chaos(host, end_time, cadence_range):
    """Periodically open a WebSocket, hold briefly, drop. Stresses the
    server's per-client cleanup path — the same code path PR #355 fixed
    for double-cleanup heap corruption. Each cycle: connect, receive a
    few frames, close abruptly.

    Runs slower than the held-connection workers so we can attribute
    any disconnect-storm panics to this worker specifically.
    """
    global ws_frames, ws_reconnects
    import websockets
    uri = f"ws://{host}:{WS_PORT}/"
    while not stop and time.time() < end_time:
        await asyncio.sleep(random.uniform(*cadence_range))
        if stop: break
        try:
            log(f"WS chaos: connecting", C.DIM)
            async with websockets.connect(uri, ping_interval=None) as ws:
                # Receive ~2-5 frames then close. The whole exchange
                # is 100ms-500ms — short enough that the server-side
                # cleanup runs on a fresh slot before the next
                # reconnect lands.
                target_frames = random.randint(2, 5)
                got = 0
                while got < target_frames and not stop:
                    msg = await asyncio.wait_for(ws.recv(), timeout=5)
                    ws_frames += 1
                    got += 1
                # Half the time, close cleanly. Other half, drop the
                # connection by exiting the context manager. Both
                # paths exercise the cleanup code.
                if random.random() < 0.5:
                    await ws.close()
                    log(f"WS chaos: closed cleanly after {got} frames", C.DIM)
                else:
                    log(f"WS chaos: dropping after {got} frames (no close)", C.DIM)
            ws_reconnects += 1
        except Exception as e:
            log(f"WS chaos error: {type(e).__name__}", C.YELLOW)
            ws_reconnects += 1


# ------------------------------------------------------------------------
# Main loop
# ------------------------------------------------------------------------

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--duration", type=float, default=10, help="Test duration in minutes")
    parser.add_argument("--no-saves", action="store_true", help="Skip /aoaconfigsave POSTs")
    parser.add_argument("--no-downloads", action="store_true", help="Skip /download")
    parser.add_argument("--no-ws", action="store_true", help="Skip WebSocket")
    parser.add_argument("--ws-clients", type=int, default=3,
                        help="Number of concurrent held WebSocket clients (default 3, "
                             "max is firmware WEBSOCKETS_SERVER_CLIENT_MAX-1=4)")
    parser.add_argument("--no-ws-chaos", action="store_true",
                        help="Skip the connect/drop chaos WebSocket worker")
    parser.add_argument("--aggressive", action="store_true",
                        help="Tighter cadence on /api/logs and pages; saves and downloads "
                             "stay at flight-realistic rates (one save per minute or less). "
                             "Use --aggressive --no-saves --no-downloads to truly torture-test "
                             "without inducing config-save cascades.")
    parser.add_argument("--realistic", action="store_true",
                        help="Realistic in-flight cadence: indexer + /api/logs only. Disables "
                             "saves and downloads entirely (pilots don't do either in flight). "
                             "Use this for the 'will the box drop samples during a normal "
                             "flight with the pilot looking at the page' test.")
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--serial-port", default=None,
                        help="USB-serial device to capture from (auto-detected if omitted; use 'none' to disable)")
    parser.add_argument("--serial-baud", type=int, default=DEFAULT_BAUD,
                        help=f"Serial baud rate (default {DEFAULT_BAUD})")
    parser.add_argument("--serial-out", default=None,
                        help="Where to write captured serial (default: ~/Downloads/stress_serial_YYYYMMDD_HHMMSS.log)")
    args = parser.parse_args()

    if args.probe:
        log(f"Probing {args.host}...", C.BOLD)
        status, n, ms, _ = http_get(f"http://{args.host}/api/logs")
        if status == 200: log(f"  /api/logs OK: {n}b in {ms}ms", C.GREEN); sys.exit(0)
        log(f"  /api/logs FAILED: status={status}", C.RED); sys.exit(1)

    # Cadence ranges (sec between requests per worker).
    #
    # The cadences below reflect realistic in-flight pilot behavior:
    # /api/logs every few seconds is plausible (pilot scrolling to
    # check the active log size), but config saves are a once-per-
    # flight thing at most, and downloads are explicitly forbidden in
    # flight (they pause the producer for the duration). The earlier
    # 3-6 sec save cadence and 15-30 sec download cadence was a "torture
    # test" — useful for surfacing the rare-but-real cascade where a
    # save under load starves the IMU, but NOT representative of any
    # real-flight workload.
    #
    # --realistic disables saves and downloads entirely (the actual
    # in-flight signal: indexer + /api/logs reload). --aggressive
    # keeps all knobs at moderate-but-not-pathological levels.
    if args.realistic:
        logs_cadence    = (2.0, 5.0)
        pages_cadence   = (4.0, 10.0)
        static_cadence  = (5.0, 12.0)
        # Realistic-flight mode actually disables the save / download
        # workers — pilots don't save config or download logs in flight.
        # Forcing the flags here keeps the banner ("workers: ...") in
        # sync with what's actually running, instead of advertising
        # workers that would tick once every two hours.
        args.no_saves     = True
        args.no_downloads = True
        save_cadence    = (3600.0, 7200.0)  # unused in this mode
        dl_cadence      = (3600.0, 7200.0)  # unused in this mode
    elif args.aggressive:
        logs_cadence    = (0.5, 1.5)
        pages_cadence   = (1.0, 2.5)
        static_cadence  = (1.5, 3.0)
        # Saves and downloads at flight-realistic rates even in
        # aggressive mode — they're known starvation triggers and
        # pilots don't do either at high cadence. The 60s save bumper
        # still exercises the retry path without producing pathological
        # cascades.
        save_cadence    = (60.0, 120.0)
        dl_cadence      = (60.0, 180.0)
    else:
        logs_cadence    = (2.0, 5.0)
        pages_cadence   = (3.0, 8.0)
        static_cadence  = (4.0, 10.0)
        save_cadence    = (120.0, 240.0)
        dl_cadence      = (120.0, 300.0)

    end_time = time.time() + args.duration * 60

    # Resolve serial capture target.
    serial_capture = None
    if args.serial_port != 'none':
        port = args.serial_port or find_serial_port()
        if port:
            if not args.serial_out:
                args.serial_out = os.path.expanduser(
                    f"~/Downloads/stress_serial_{time.strftime('%Y%m%d_%H%M%S')}.log")
            serial_capture = SerialCapture(port, args.serial_baud, args.serial_out)
        else:
            log(f"serial: no port found (looked under {SERIAL_GLOBS}); continuing without capture", C.YELLOW)

    if args.realistic:    mode_label = "realistic (indexer + /api/logs only)"
    elif args.aggressive: mode_label = "aggressive (tight cadence, flight-realistic save/dl)"
    else:                 mode_label = "default (moderate cadence)"

    log(f"OnSpeed web-handler stress test", C.BOLD)
    log(f"  host:         {args.host}", C.DIM)
    log(f"  duration:     {args.duration} min", C.DIM)
    log(f"  mode:         {mode_label}", C.DIM)
    log(f"  workers:      /api/logs, pages, /static, " +
        ("/download, " if not args.no_downloads else "") +
        ("/save (full-form), " if not args.no_saves else "") +
        ((f"{args.ws_clients}×WS held" + (" + chaos" if not args.no_ws_chaos else ""))
            if not args.no_ws else ""), C.DIM)
    if serial_capture:
        log(f"  serial:       {serial_capture.port} @ {serial_capture.baud} → {serial_capture.out_path}", C.DIM)
    else:
        log(f"  serial:       (disabled)", C.DIM)
    log(f"  Ctrl-C to stop early", C.DIM)
    log("", "")

    if serial_capture:
        serial_capture.start()

    # Run worker threads as daemons so Ctrl-C exits cleanly without
    # waiting for any blocked urllib call.
    worker_threads = [
        threading.Thread(target=worker_logs,   args=(args.host, end_time, logs_cadence),  daemon=True),
        threading.Thread(target=worker_pages,  args=(args.host, end_time, pages_cadence), daemon=True),
        threading.Thread(target=worker_static, args=(args.host, end_time, static_cadence),daemon=True),
    ]
    if not args.no_downloads:
        worker_threads.append(threading.Thread(target=worker_download, args=(args.host, end_time, dl_cadence), daemon=True))
    if not args.no_saves:
        worker_threads.append(threading.Thread(target=worker_save, args=(args.host, end_time, save_cadence), daemon=True))
    for t in worker_threads:
        t.start()

    async_tasks = []
    if not args.no_ws:
        # Held connections — exercise the steady-state broadcast path.
        for i in range(args.ws_clients):
            async_tasks.append(asyncio.create_task(
                worker_websocket(args.host, end_time, worker_id=i+1)))
        # Connect/drop chaos — exercise per-client cleanup (PR #355 fix).
        if not args.no_ws_chaos:
            chaos_cadence = (2.0, 5.0) if args.aggressive else (5.0, 15.0)
            async_tasks.append(asyncio.create_task(
                worker_websocket_chaos(args.host, end_time, chaos_cadence)))

    # Main coroutine polls the stop flag and end_time every 0.2 sec.
    # On Ctrl-C the signal handler sets stop=True; we exit the wait
    # loop and run the summary. Daemon worker threads die at exit.
    global stop
    try:
        while not stop and time.time() < end_time:
            await asyncio.sleep(0.2)
    finally:
        stop = True
        for t in async_tasks: t.cancel()
        # Brief grace so WS task can finish its cancellation.
        for t in async_tasks:
            try: await asyncio.wait_for(t, timeout=0.5)
            except (asyncio.CancelledError, asyncio.TimeoutError): pass

    # Stop serial capture first so its summary reflects the full window.
    if serial_capture:
        serial_capture.stop()

    elapsed_min = (time.time() - start_time) / 60
    log("", "")
    log("=" * 70, C.BOLD)
    log(f"Summary after {elapsed_min:.1f} min:", C.BOLD)
    for name, s in stats.items():
        log(s.summary(), None)
    log(f"  WebSocket frames received: {ws_frames}, reconnects: {ws_reconnects}", C.DIM)
    log("", "")
    if serial_capture:
        log("Serial capture summary:", C.BOLD)
        for line in serial_capture.summary().splitlines():
            log(line, None)
        log(f"  Full transcript: {serial_capture.out_path}", C.CYAN)
        log("", "")
    log("Verify post-flight:", C.BOLD)
    log("  - any 503 in summary?  →   check writer-yield path is actually running", C.DIM)
    log("  - paused_drops > 0     →   only acceptable during /download moments", C.DIM)
    log("  - drops > 0            →   ring overflowed; producer outran writer", C.DIM)
    log("  - imu_lateMaxUs > 1000 →   IMU schedule reset (timing disrupted; sample loss starts above one IMU period ~4807 us)", C.DIM)
    log("  - imu_lateMaxUs > 4807 →   IMU period exceeded (sample timing surrendered)", C.DIM)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
