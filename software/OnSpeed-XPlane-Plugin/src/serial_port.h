// Cross-platform USB serial port wrapper.  Used to stream the OnSpeed
// display-serial wire frames the X-Plane plugin already builds out to
// a physically-connected M5Stack so pilots can use a real OnSpeed
// display in the sim.
//
// Design: thin RAII handle.  Open() returns true on success; Write()
// returns false on any error (caller can drop the data).  Close() is
// safe to call repeatedly.  Cross-platform via #ifdef on POSIX termios
// (macOS, Linux) vs Windows CreateFile + DCB.

#pragma once

#include <string>
#include <vector>

namespace onspeed_xplane::serial {

// Enumerate plausible USB-CDC serial ports the OS exposes right now.
// macOS:   /dev/cu.usbmodem* and /dev/cu.usbserial*
// Linux:   /dev/ttyACM* and /dev/ttyUSB*
// Windows: COM3..COM256 that respond to CreateFile (no enumeration API
//          short of WMI, which is heavy; we probe by open).
std::vector<std::string> ListPorts();

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    SerialPort(const SerialPort&)            = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Open the port at 115200 8N1 (matches OnSpeed display-serial
    // wire format).  Returns true on success.  If a port was already
    // open, it's closed first.
    bool Open(const std::string& portPath);

    // Close any currently-open port.  Safe to call when not open.
    void Close();

    // True iff a port is currently open.
    bool IsOpen() const;

    // Write `len` bytes to the port.  Returns true on success, false
    // on any error (broken pipe, device unplugged, partial write).
    // On error, the port stays open — caller can decide whether to
    // close + reopen or just drop the next frame.
    bool Write(const void* data, std::size_t len);

private:
#ifdef _WIN32
    void* m_handle = nullptr;       // HANDLE
#else
    int   m_fd     = -1;
#endif
    std::string m_path;
};

}   // namespace onspeed_xplane::serial
