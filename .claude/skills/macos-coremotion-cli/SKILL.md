---
name: macos-coremotion-cli
description: Use when building a macOS command-line tool that calls TCC-gated APIs — CoreMotion (`CMHeadphoneMotionManager`, `CMMotionManager`), Bluetooth, Camera, Microphone, Photos, Contacts, Calendar, Reminders, Location, Speech, AppleEvents, Accessibility, ScreenCapture, FullDiskAccess. A bare `swiftc` binary will hit `Abort trap: 6` from TCC the moment it touches the gated API, no matter how the Info.plist is configured. This skill captures the exact crash signature, the four-step fix, and the launch dance needed to keep stderr visible.
---

# macOS CoreMotion (and other TCC-gated APIs) from a CLI tool

## The problem

You want a small one-off Swift program that reads CoreMotion data
(e.g. AirPods head tracking via `CMHeadphoneMotionManager`) and pipes
it somewhere — over serial, over a socket, into a file. You write a
single `.swift` file. You build it:

```bash
swiftc thing.swift -o thing
./thing /dev/cu.something
```

It immediately dies with:

```
Abort trap: 6
```

The Console log via `log show --predicate 'process == "thing"' --last 1m`
shows the actual cause:

```
(TCC) [com.apple.TCC:access] This app has crashed because it
attempted to access privacy-sensitive data without a usage
description.  The app's Info.plist must contain an
NSMotionUsageDescription key with a string value explaining to the
user how the app uses this data.
```

The message is misleading — adding `NSMotionUsageDescription` to an
external file or stuffing it into the `__TEXT,__info_plist` Mach-O
section **is not enough**. TCC, for the gated APIs, only consults
Info.plist when the executable is part of a proper `.app` bundle that
has been ad-hoc codesigned with entitlements *and* launched in a way
that gives the process a bundle-identity context.

## The four-step fix

Use this exact sequence. Each step solves a separate piece of the
puzzle and the program still crashes if any of them are skipped.

### 1. Write an `Info.plist`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>org.example.your_tool</string>
    <key>CFBundleName</key>
    <string>your_tool</string>
    <key>NSMotionUsageDescription</key>
    <string>Explanation shown in the consent prompt.</string>
    <!-- Add other Ns*UsageDescription keys for whichever
         additional TCC services your tool will touch.  -->
</dict>
</plist>
```

The string value must be non-empty and human-readable — TCC will
display it verbatim in the consent prompt.

### 2. Write an `entitlements.plist`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.device.bluetooth</key>
    <true/>
    <!-- Add com.apple.security.device.audio-input, .camera, etc.
         as needed for the services you'll touch. -->
</dict>
</plist>
```

For `CMHeadphoneMotionManager` specifically, the Bluetooth
entitlement is what unblocks the AirPods motion stream — the AirPods
deliver attitude via the L2CAP channel, not as a Motion service.

### 3. Build a proper `.app` bundle and codesign with the entitlements

```bash
mkdir -p your_tool.app/Contents/MacOS
cp Info.plist your_tool.app/Contents/Info.plist
swiftc your_tool.swift -o your_tool.app/Contents/MacOS/your_tool

codesign --force --sign - \
    --entitlements entitlements.plist \
    ./your_tool.app
```

Verify the Info.plist is sealed in:

```bash
$ codesign -dvv ./your_tool.app 2>&1 | grep Info.plist
Info.plist entries=4    # any number > 0 — "not bound" means broken
```

`--sign -` produces an ad-hoc signature. That's enough — TCC trusts
ad-hoc signatures provided the Info.plist is sealed into the bundle
and the entitlements file is bound.

### 4. Launch via `open`, NOT by invoking the inner Mach-O directly

```bash
open --stdout /tmp/your_tool.stdout \
     --stderr /tmp/your_tool.stderr \
     /absolute/path/to/your_tool.app \
     --args /dev/cu.something other-args
```

This is the step that catches people. Running
`your_tool.app/Contents/MacOS/your_tool` directly from the terminal
**still crashes** with the same `NSMotionUsageDescription` error,
because TCC walks the responsible-process chain up to the parent
shell and doesn't find a bundle identity. Going through `open`
routes the launch through LaunchServices, which sets the process's
responsible bundle to the `.app` itself, and TCC accepts the
Info.plist's usage descriptions.

The `--stdout` / `--stderr` flags redirect to files so you don't lose
output (the launched process has no controlling terminal). If you'd
rather see output live, `tail -F /tmp/your_tool.stderr` in another
window.

If `open` complains about "Unable to find application named …", run

```bash
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f /absolute/path/to/your_tool.app
```

once to register the bundle. Almost always unnecessary on a freshly
built bundle in a known path, but worth knowing about.

## Stdin is gone — drive runtime control with signals

A CLI tool launched via `open` has no controlling terminal, so you
can't read keystrokes from stdin to trigger runtime behavior (recenter,
toggle logging, etc.). Use signals instead:

```swift
let recenterSrc = DispatchSource.makeSignalSource(signal: SIGUSR1, queue: .main)
signal(SIGUSR1, SIG_IGN)
recenterSrc.setEventHandler {
    // recenter / toggle / whatever
}
recenterSrc.resume()
```

Then from any terminal:

```bash
pkill -USR1 -f your_tool
```

`SIGINT` (Ctrl-C) still works if you `pkill -INT` it. For a multi-key
UX, ship a one-liner shell script (`recenter.sh`, `toggle-log.sh`)
that calls `pkill -USR1` / `pkill -USR2`.

## Permission grant flow

The first time the bundle runs against a TCC-gated service, macOS
shows a consent prompt (center of screen or in Notification Center)
with the `NSMotionUsageDescription` string from Info.plist. The user
must click **Allow**. After that, the grant is persisted in TCC's
database keyed by the bundle identifier — subsequent runs proceed
silently.

If the user clicks **Deny** by mistake, re-grant via System Settings
→ Privacy & Security → Motion (or Bluetooth, Camera, etc.). The
bundle has to relaunch to pick up the new grant.

If you change `CFBundleIdentifier` between builds, TCC treats it as a
new app and re-prompts. Pin the identifier early.

## What "Abort trap: 6" looks like in practice

If you see this, run this:

```bash
$ ./your_tool /dev/cu.usbserial-XXXX
Abort trap: 6

$ log show --predicate 'process == "your_tool"' --last 1m 2>&1 \
    | grep -iE "TCC|denied|usage"
... (TCC) [com.apple.TCC:access] This app has crashed because it
    attempted to access privacy-sensitive data without a usage
    description.  The app's Info.plist must contain an
    NSMotionUsageDescription key with a string value explaining to
    the user how the app uses this data.
```

The error is the giveaway. If you see a different `TCC` message
(e.g., `Service is not allowed for an unentitled app`), you're missing
the entitlement on the codesign — go back to step 2.

If the Console log shows successful TCC requests but the program
still crashes, the abort is coming from somewhere else (a serial
`open()` failure, a Swift precondition trap, etc.). The TCC path is
the only one that produces `Abort trap: 6` with a `usage description`
error in Console.

## Why all four steps

| Step | What it fixes |
|---|---|
| 1. Info.plist | TCC reads it to show the consent prompt; missing → crash. |
| 2. entitlements | Codesign binds them; required for Bluetooth (and other) services. Without them: "Service is not allowed for an unentitled app". |
| 3. `.app` bundle + codesign | TCC won't honor an Info.plist outside a bundle, and won't honor an unsigned bundle. The `__info_plist` Mach-O section trick that works for some macOS APIs does NOT work for TCC-gated motion / Bluetooth. |
| 4. `open` instead of direct exec | LaunchServices sets the bundle-identity context. A direct `./app/Contents/MacOS/exe` invocation has no responsible bundle, so TCC can't find the Info.plist even though it's right there. |

Any one missing → `Abort trap: 6`. All four → first run shows the
consent prompt; click Allow; tool runs.

