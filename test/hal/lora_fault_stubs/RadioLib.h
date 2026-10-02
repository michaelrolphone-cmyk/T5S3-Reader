#pragma once
#include <cstddef>
#include <cstdint>
#include <SPI.h>
#define RADIOLIB_ERR_NONE 0
extern unsigned hardwareCalls;
struct Module { Module(int,int,int,int,int&,SPISettings) {} };
struct SX1262 {
 explicit SX1262(Module*) {}
 template<class... T> int16_t begin(T...) { ++hardwareCalls; return 0; }
 int16_t setCurrentLimit(float) { ++hardwareCalls; return 0; }
 int16_t setCRC(bool) { ++hardwareCalls; return 0; }
 int16_t setDio2AsRfSwitch() { ++hardwareCalls; return 0; }
 void setPacketReceivedAction(void (*)()) { ++hardwareCalls; }
 void clearPacketReceivedAction() { ++hardwareCalls; }
 int16_t startReceive() { ++hardwareCalls; return 0; }
 int16_t sleep() { ++hardwareCalls; return 0; }
 int16_t standby() { ++hardwareCalls; return 0; }
 size_t getPacketLength() { ++hardwareCalls; return 1; }
 int16_t readData(uint8_t*,size_t) { ++hardwareCalls; return 0; }
 float getRSSI() { ++hardwareCalls; return 0; }
 float getSNR() { ++hardwareCalls; return 0; }
 float getFrequencyError() { ++hardwareCalls; return 0; }
 int16_t transmit(const uint8_t*,size_t) { ++hardwareCalls; return 0; }
};
