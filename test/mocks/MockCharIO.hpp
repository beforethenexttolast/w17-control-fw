#pragma once

#include <cstring>

#include "hal/ICharIO.hpp"

namespace test_mocks {

// Scripted char IO: feed() queues input bytes; read() drains them
// (returning -1 when empty); write() captures output for assertions.
class MockCharIO : public hal::ICharIO {
public:
    void feed(const char* s) {
        for (const char* p = s; *p; ++p) {
            if (inLen < sizeof(in)) in[inLen++] = *p;
        }
    }

    int read() override { return inPos < inLen ? static_cast<unsigned char>(in[inPos++]) : -1; }

    void write(const char* text) override {
        const size_t n = std::strlen(text);
        for (size_t i = 0; i < n && outLen < sizeof(out) - 1; ++i) out[outLen++] = text[i];
        out[outLen] = '\0';
    }

    bool outputContains(const char* needle) const { return std::strstr(out, needle) != nullptr; }
    void clearOutput() { outLen = 0; out[0] = '\0'; }

    char in[512] = {0};
    size_t inLen = 0;
    size_t inPos = 0;
    char out[2048] = {0};
    size_t outLen = 0;
};

} // namespace test_mocks
