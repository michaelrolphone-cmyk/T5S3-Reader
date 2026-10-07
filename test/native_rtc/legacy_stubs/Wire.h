#pragma once
#include <cstddef>
#include <cstdint>
struct TestWire {
    uint8_t registers[32]{}, tx[16]{}, cursor = 0;
    size_t length = 0, remaining = 0;
    unsigned writes = 0;
    bool ok = true;
    void beginTransmission(uint8_t address);
    size_t write(uint8_t byte);
    size_t write(const uint8_t* data, size_t n);
    uint8_t endTransmission(bool stop = true);
    uint8_t requestFrom(uint8_t address, uint8_t count);
    int available();
    int read();
};
extern TestWire Wire;
