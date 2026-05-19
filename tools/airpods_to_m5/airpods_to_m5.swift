// airpods_to_m5.swift
//
// For-fun: read AirPods Pro head-tracking pitch/roll via CoreMotion's
// CMHeadphoneMotionManager and emit OnSpeed `#1` display-serial frames
// (v4.23 wire format, 77 bytes) at 20 Hz to a USB serial port. Pair with
// the M5/huVVer display in attitude mode (mode 1) and tilt your head.
//
// Wire format reference: onspeed_core/src/proto/DisplaySerial.h
//
// Usage:
//   swift airpods_to_m5.swift /dev/cu.usbserial-XXXXXXXX
//
// Requires:
//   - macOS 14+ (CMHeadphoneMotionManager works on the Mac since 14.0)
//   - AirPods Pro / 3 / Max / 4 paired and worn
//   - Bluetooth/Motion permission (granted on first launch)

import CoreMotion
import Darwin
import Foundation

// ---------------------------------------------------------------------------
// Frame builder — mirrors onspeed::proto::BuildDisplayFrame.
// We only fill pitch/roll. iasValid=false so the M5 hides IAS / percentLift.

let kDisplayFrameSizeBytes = 77
let kDisplayFrameChecksumLen = 73
let kIasInvalidWireSentinel = 9999

func clampInt(_ x: Int, _ lo: Int, _ hi: Int) -> Int {
    return max(lo, min(hi, x))
}

func buildFrame(pitchDeg: Double, rollDeg: Double) -> [UInt8] {
    let pitch10 = clampInt(Int((pitchDeg * 10).rounded()), -999, 999)
    let roll10  = clampInt(Int((rollDeg  * 10).rounded()), -9999, 9999)

    // Build the 73-byte ASCII payload. The %+0Nd / %0Nu specifiers match
    // exactly what DisplaySerial.cpp emits via snprintf.
    let payload = String(format:
        "#1" +                  //  0  2   magic
        "%+04d" +               //  2  4   pitch×10
        "%+05d" +               //  6  5   roll×10
        "%04u"  +               // 11  4   ias×10        (9999 sentinel)
        "%+06d" +               // 15  6   palt
        "%+05d" +               // 21  5   turnRate×10
        "%+03d" +               // 26  3   lateralG×100
        "%+03d" +               // 29  3   verticalG×10
        "%03u"  +               // 32  3   percentLift×10 (0 with invalid IAS)
        "%+04d" +               // 35  4   vsi/10
        "%+03d" +               // 39  3   oat
        "%+04d" +               // 42  4   flightPath×10
        "%+03d" +               // 46  3   flaps
        "%02u"  +               // 49  2   tonesOnPctLift
        "%02u"  +               // 51  2   onSpeedFastPctLift
        "%02u"  +               // 53  2   onSpeedSlowPctLift
        "%02u"  +               // 55  2   stallWarnPctLift
        "%+03d" +               // 57  3   flapsMinDeg
        "%+03d" +               // 60  3   flapsMaxDeg
        "%+04d" +               // 63  4   gOnsetRate×100
        "%+02d" +               // 67  2   spinRecoveryCue
        "%02u"  +               // 69  2   dataMark
        "%02u",                 // 71  2   pipPctLift
        pitch10,
        roll10,
        UInt32(kIasInvalidWireSentinel),  // iasKt sentinel
        0,        // palt
        0,        // turnRate
        0,        // lateralG
        0,        // verticalG
        UInt32(0),// percentLift
        0,        // vsi/10
        0,        // oat
        0,        // flightPath
        0,        // flaps
        UInt32(0), UInt32(0), UInt32(0), UInt32(0),  // band-edge percents
        0, 0,     // flaps min/max
        0,        // gOnsetRate
        0,        // spinRecoveryCue
        UInt32(0),// dataMark
        UInt32(0) // pipPctLift
    )

    let payloadBytes = Array(payload.utf8)
    guard payloadBytes.count == kDisplayFrameChecksumLen else {
        FileHandle.standardError.write(
            Data("payload was \(payloadBytes.count) bytes, expected \(kDisplayFrameChecksumLen)\n".utf8))
        return []
    }

    // Two-byte ASCII-hex checksum over the 73-byte payload.
    var sum: UInt16 = 0
    for b in payloadBytes { sum = sum &+ UInt16(b) }
    let crc = UInt8(sum & 0xFF)
    let crcStr = String(format: "%02X", crc)

    var frame = payloadBytes
    frame.append(contentsOf: crcStr.utf8)
    frame.append(0x0D)
    frame.append(0x0A)
    return frame
}

// ---------------------------------------------------------------------------
// Serial port — raw POSIX so we don't drag in a dependency.

func openSerial(path: String, baud: speed_t) -> Int32 {
    let fd = open(path, O_WRONLY | O_NOCTTY | O_NDELAY)
    if fd < 0 {
        perror("open(\(path))")
        exit(1)
    }
    // Clear O_NDELAY so writes block on the queue draining.
    _ = fcntl(fd, F_SETFL, 0)

    var tio = termios()
    if tcgetattr(fd, &tio) != 0 {
        perror("tcgetattr"); exit(1)
    }
    cfmakeraw(&tio)
    cfsetspeed(&tio, baud)
    tio.c_cflag |= UInt(CLOCAL | CREAD)
    tio.c_cflag &= ~UInt(PARENB)
    tio.c_cflag &= ~UInt(CSTOPB)
    tio.c_cflag &= ~UInt(CSIZE)
    tio.c_cflag |= UInt(CS8)
    if tcsetattr(fd, TCSANOW, &tio) != 0 {
        perror("tcsetattr"); exit(1)
    }
    return fd
}

// ---------------------------------------------------------------------------
// Main.

guard CommandLine.arguments.count == 2 else {
    print("usage: swift airpods_to_m5.swift /dev/cu.usbserial-XXXX")
    exit(2)
}
let devicePath = CommandLine.arguments[1]

guard CMHeadphoneMotionManager.authorizationStatus() != .denied else {
    print("Motion authorization denied. Re-enable in System Settings → Privacy & Security → Motion.")
    exit(1)
}

let mgr = CMHeadphoneMotionManager()
guard mgr.isDeviceMotionAvailable else {
    print("CMHeadphoneMotionManager reports no compatible AirPods. Pair AirPods Pro / 3 / Max / 4 and try again.")
    exit(1)
}

let fd = openSerial(path: devicePath, baud: speed_t(115200))
print("Opened \(devicePath) at 115200 8N1. Waiting for AirPods motion data...")

// Latest sample held by the motion callback. Bias captured on the
// first sample so head-forward = horizon level.
let lock = NSLock()
var pitchDeg: Double = 0
var rollDeg:  Double = 0
var pitchBias: Double? = nil
var rollBias:  Double? = nil
var haveData = false

let radToDeg = 180.0 / .pi

mgr.startDeviceMotionUpdates(to: .main) { motion, error in
    if let error = error {
        FileHandle.standardError.write(Data("motion error: \(error)\n".utf8))
        return
    }
    guard let m = motion else { return }
    let p = m.attitude.pitch * radToDeg
    let r = m.attitude.roll  * radToDeg

    lock.lock()
    if pitchBias == nil { pitchBias = p; rollBias = r }
    pitchDeg = p - (pitchBias ?? 0)
    rollDeg  = r - (rollBias  ?? 0)
    haveData = true
    lock.unlock()
}

// 20 Hz frame pacing — matches the firmware's display-serial cadence.
let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .userInteractive))
timer.schedule(deadline: .now(), repeating: .milliseconds(50))
var framesSent: UInt64 = 0
var nextLog = Date().addingTimeInterval(2)

timer.setEventHandler {
    lock.lock()
    let p = pitchDeg
    let r = rollDeg
    let ok = haveData
    lock.unlock()

    let frame = buildFrame(pitchDeg: ok ? p : 0,
                           rollDeg:  ok ? r : 0)
    guard !frame.isEmpty else { return }

    frame.withUnsafeBufferPointer { buf in
        let n = write(fd, buf.baseAddress, buf.count)
        if n != buf.count {
            FileHandle.standardError.write(
                Data("short write \(n)/\(buf.count): \(String(cString: strerror(errno)))\n".utf8))
        }
    }
    framesSent &+= 1

    let now = Date()
    if now >= nextLog {
        FileHandle.standardError.write(
            Data(String(format: "%6llu frames | pitch %+6.1f° roll %+6.1f° %@\n",
                        framesSent, p, r,
                        ok ? "" : "(waiting for AirPods)").utf8))
        nextLog = now.addingTimeInterval(2)
    }
}
timer.resume()

// Tidy up on Ctrl-C.
let sigsrc = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
signal(SIGINT, SIG_IGN)
sigsrc.setEventHandler {
    print("\nstopping")
    mgr.stopDeviceMotionUpdates()
    close(fd)
    exit(0)
}
sigsrc.resume()

// SIGUSR1 → re-zero the horizon to whatever the AirPods are reading
// right now. `kill -USR1 <pid>` from another terminal, or via the
// recenter.sh helper.
let recenterSrc = DispatchSource.makeSignalSource(signal: SIGUSR1, queue: .main)
signal(SIGUSR1, SIG_IGN)
recenterSrc.setEventHandler {
    lock.lock()
    pitchBias = nil
    rollBias  = nil
    haveData  = false
    lock.unlock()
    FileHandle.standardError.write(Data("recenter requested — next sample becomes new zero\n".utf8))
}
recenterSrc.resume()

RunLoop.main.run()
