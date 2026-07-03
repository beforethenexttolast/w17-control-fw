#pragma once

namespace hal {

// Abstract character I/O for the bench console (UART0 in the esp32 impl, a
// scripted buffer in tests). Non-blocking read so the console never stalls
// the control loop.
class ICharIO {
public:
    virtual ~ICharIO() = default;

    // Returns the next input byte, or -1 if none is available right now.
    virtual int read() = 0;

    // Writes a NUL-terminated string.
    virtual void write(const char* text) = 0;
};

} // namespace hal
