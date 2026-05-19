// Smoke test: build a frame at pitch=+5.3°, roll=-12.1° and confirm
// length / magic / checksum / CRLF.

import Foundation

let kDisplayFrameSizeBytes = 77
let kDisplayFrameChecksumLen = 73
let kIasInvalidWireSentinel = 9999

func clampInt(_ x: Int, _ lo: Int, _ hi: Int) -> Int {
    return max(lo, min(hi, x))
}

func buildFrame(pitchDeg: Double, rollDeg: Double) -> [UInt8] {
    let pitch10 = clampInt(Int((pitchDeg * 10).rounded()), -999, 999)
    let roll10  = clampInt(Int((rollDeg  * 10).rounded()), -9999, 9999)
    let payload = String(format:
        "#1%+04d%+05d%04u%+06d%+05d%+03d%+03d%03u%+04d%+03d%+04d%+03d%02u%02u%02u%02u%+03d%+03d%+04d%+02d%02u%02u",
        pitch10, roll10, UInt32(kIasInvalidWireSentinel),
        0, 0, 0, 0, UInt32(0), 0, 0, 0, 0,
        UInt32(0), UInt32(0), UInt32(0), UInt32(0),
        0, 0, 0, 0, UInt32(0), UInt32(0))
    let payloadBytes = Array(payload.utf8)
    var sum: UInt16 = 0
    for b in payloadBytes { sum = sum &+ UInt16(b) }
    let crc = UInt8(sum & 0xFF)
    var frame = payloadBytes
    frame.append(contentsOf: String(format: "%02X", crc).utf8)
    frame.append(0x0D); frame.append(0x0A)
    return frame
}

let f = buildFrame(pitchDeg: 5.3, rollDeg: -12.1)
print("len=\(f.count) (want \(kDisplayFrameSizeBytes))")
print("payload[0..2]=\(String(bytes: Array(f[0..<2]), encoding: .ascii)!)")
print("CR=\(f[75]==0x0D) LF=\(f[76]==0x0A)")
print("frame: \(String(bytes: Array(f[0..<75]), encoding: .ascii)!)<\(String(format:"%02X",f[73]))\(String(format:"%02X",f[74]))>CRLF")

// Verify checksum self-consistently.
var s: UInt16 = 0
for b in f[0..<73] { s = s &+ UInt16(b) }
let want = UInt8(s & 0xFF)
let got = (UInt8(f[73]) >= 0x41 ? UInt8(f[73]) - 0x41 + 10 : UInt8(f[73]) - 0x30) * 16
        + (UInt8(f[74]) >= 0x41 ? UInt8(f[74]) - 0x41 + 10 : UInt8(f[74]) - 0x30)
print("checksum want=\(String(format:"%02X",want)) got=\(String(format:"%02X",got)) ok=\(want==got)")
