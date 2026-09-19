/* Temporary physical I2C0 owner for the installable i2c.bus provider.
 * One global Board I2C lock spans the entire write/repeated-START/read cycle,
 * including the resident touch, power-management and GPIO-expander clients.
 * Never export this function through the ordinary application ELF ABI.
 */
#ifdef BOARD_T5S3_PRO
#include <BoardT5S3.h>
#include <Wire.h>
#include <RiscFirmwareI2cCompatV1.h>

extern "C" bool risc_fw_i2c_transact_v1(
    uint8_t address, const uint8_t *write_bytes, size_t write_length,
    uint8_t *read_bytes, size_t read_length, uint32_t timeout_ms) {
  if (address < 0x08u || address > 0x77u ||
      (!write_length && !read_length) ||
      write_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
      read_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
      (write_length && !write_bytes) || (read_length && !read_bytes) ||
      !timeout_ms || timeout_ms > RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS)
    return false;

  BoardT5S3::ScopedI2CLock lock;
  // Firmware's beginI2C() initializes Wire once at boot. An installable ELF
  // must never call Wire.begin(), reconfigure pins or install another driver.
  Wire.setTimeOut(timeout_ms);
  bool ok = true;
  if (write_length) {
    Wire.beginTransmission(address);
    ok = Wire.write(write_bytes, write_length) == write_length;
    // STOP only for write-only transactions. A combined transaction needs
    // a repeated START, not separate unlocked operations.
    if (ok) ok = Wire.endTransmission(read_length == 0) == 0;
    else (void)Wire.endTransmission(true);
  }
  if (ok && read_length) {
    const uint8_t requested = static_cast<uint8_t>(read_length);
    ok = Wire.requestFrom(address, requested) == requested;
    if (ok) {
      for (size_t i = 0; i < read_length; ++i) {
        const int value = Wire.read();
        if (value < 0) { ok = false; break; }
        read_bytes[i] = static_cast<uint8_t>(value);
      }
    }
    while (Wire.available()) (void)Wire.read();
  }
  // Restore the board's established 50ms timeout before releasing the lock.
  Wire.setTimeOut(50);
  return ok;
}
#endif
