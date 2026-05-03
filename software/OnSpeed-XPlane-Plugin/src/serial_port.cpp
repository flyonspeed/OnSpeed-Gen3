// SerialPort.cpp — see serial_port.h.
//
// POSIX path uses raw termios at 115200 8N1, no flow control.
// Windows path uses CreateFile + DCB (similar settings).

#include "serial_port.h"

#include <cstring>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
#endif

#ifdef __APPLE__
    #include <IOKit/serial/ioss.h>
#endif

namespace onspeed_xplane::serial {

// ----------------------------------------------------------------
// Port enumeration
// ----------------------------------------------------------------

#ifndef _WIN32
namespace {

// List entries in /dev whose name starts with any of the given
// prefixes.  Returns full paths.
std::vector<std::string> ScanDevByPrefix(
    std::initializer_list<const char*> prefixes)
{
    std::vector<std::string> out;
    DIR* d = opendir("/dev");
    if (!d) return out;
    while (auto* e = readdir(d)) {
        const char* n = e->d_name;
        for (const char* p : prefixes) {
            const std::size_t plen = std::strlen(p);
            if (std::strncmp(n, p, plen) == 0) {
                out.push_back(std::string("/dev/") + n);
                break;
            }
        }
    }
    closedir(d);
    return out;
}

}   // namespace
#endif

std::vector<std::string> ListPorts()
{
#ifdef _WIN32
    // Windows has no clean enumeration API short of SetupDi; for the
    // common case of "user wants to pick their M5", probe COM3..COM32
    // with CreateFile + close.
    std::vector<std::string> out;
    for (int i = 1; i <= 32; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "\\\\.\\COM%d", i);
        HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            char display[8];
            std::snprintf(display, sizeof(display), "COM%d", i);
            out.emplace_back(display);
        }
    }
    return out;
#elif defined(__APPLE__)
    return ScanDevByPrefix({"cu.usbmodem", "cu.usbserial",
                             "cu.SLAB_USBtoUART", "cu.wchusbserial"});
#else
    return ScanDevByPrefix({"ttyACM", "ttyUSB"});
#endif
}

// ----------------------------------------------------------------
// SerialPort
// ----------------------------------------------------------------

SerialPort::SerialPort() = default;

SerialPort::~SerialPort()
{
    Close();
}

bool SerialPort::IsOpen() const
{
#ifdef _WIN32
    return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
#else
    return m_fd >= 0;
#endif
}

void SerialPort::Close()
{
#ifdef _WIN32
    if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
#else
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    m_path.clear();
}

#ifdef _WIN32

bool SerialPort::Open(const std::string& portPath)
{
    Close();
    // Accept "COM5" or "\\.\COM5" — CreateFile requires the latter
    // for COM10+.
    std::string fullPath = portPath;
    if (fullPath.size() < 4 || fullPath[0] != '\\') {
        fullPath = std::string("\\\\.\\") + portPath;
    }

    HANDLE h = CreateFileA(fullPath.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return false;
    }
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return false;
    }

    COMMTIMEOUTS to = {};
    to.WriteTotalTimeoutConstant = 100;     // 100 ms cap on a single Write
    SetCommTimeouts(h, &to);

    m_handle = h;
    m_path   = portPath;
    return true;
}

bool SerialPort::Write(const void* data, std::size_t len)
{
    if (!IsOpen()) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(static_cast<HANDLE>(m_handle), data,
                        static_cast<DWORD>(len), &wrote, nullptr);
    return ok && wrote == len;
}

#else   // POSIX

bool SerialPort::Open(const std::string& portPath)
{
    Close();
    int fd = ::open(portPath.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;

    // Keep O_NONBLOCK.  A blocking write() on a stalled USB-CDC device
    // (M5 unplugged mid-session, USB driver retrying) can hang for
    // several seconds — and Write() runs on X-Plane's flight-loop
    // thread.  X-Plane will kill the plugin if a flight-loop callback
    // doesn't return promptly.  Non-blocking IO + drop-the-frame-on-
    // EAGAIN is the right contract for a 20 Hz display stream.

    struct termios tio = {};
    if (tcgetattr(fd, &tio) != 0) {
        ::close(fd);
        return false;
    }
    cfmakeraw(&tio);                         // no canonical, no echo, etc.
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;                  // 8N1
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;                 // no HW flow control
    tio.c_iflag &= ~(IXON | IXOFF | IXANY); // no SW flow control
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    // Set 115200 portably.  POSIX cfsetospeed accepts B115200 on
    // both Linux and macOS.  On macOS we'd use IOSSIOSPEED for
    // non-standard rates, but 115200 is standard.
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        ::close(fd);
        return false;
    }

    m_fd   = fd;
    m_path = portPath;
    return true;
}

bool SerialPort::Write(const void* data, std::size_t len)
{
    if (m_fd < 0) return false;
    // O_NONBLOCK is set on the fd.  A single non-blocking write either
    // accepts the whole frame (typical case — kernel TX buffer is
    // ~4 KB, our frames are 76 bytes at 20 Hz = 1520 B/s) or fails
    // with EAGAIN/EWOULDBLOCK because the device is stalled.  Treat
    // partial-write or EAGAIN as "drop this frame and report error" —
    // the caller (Tick) closes the port and surfaces it to the user
    // rather than freezing the sim by retrying inside the flight
    // loop.
    ssize_t n = ::write(m_fd, data, len);
    return n == static_cast<ssize_t>(len);
}

#endif

}   // namespace onspeed_xplane::serial
