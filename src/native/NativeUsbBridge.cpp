#include <T5AppApi.h>
#include <T5UsbApi.h>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstring>

#ifdef BOARD_T5S3_PRO
#include <BoardT5S3.h>
#include <Wire.h>
#include <bq25896_reg.h>
#include <usb/usb_host.h>
#endif

namespace {
constexpr size_t kRxRingSize = 8192;
constexpr size_t kBulkBufferSize = 512;
constexpr size_t kControlBufferSize = 32;
constexpr uint32_t kUsbShutdownTimeoutMs = 7000;

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
t5_usb_serial_state_t state = {};
uint8_t rxRing[kRxRingSize];
size_t rxHead = 0;
size_t rxTail = 0;
volatile bool running = false;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void resetState(uint8_t status) {
  portENTER_CRITICAL(&stateMux);
  std::memset(&state, 0, sizeof(state));
  state.status = status;
  state.line_coding.baud_rate = 115200;
  state.line_coding.data_bits = 8;
  state.line_coding.parity = T5_USB_PARITY_NONE;
  state.line_coding.stop_bits = 1;
  rxHead = rxTail = 0;
  portEXIT_CRITICAL(&stateMux);
}

#ifdef BOARD_T5S3_PRO
usb_host_client_handle_t client = nullptr;
usb_device_handle_t device = nullptr;
usb_transfer_t* rxTransfer = nullptr;
usb_transfer_t* txTransfer = nullptr;
usb_transfer_t* controlTransfer = nullptr;
TaskHandle_t hostTaskHandle = nullptr;
SemaphoreHandle_t txMutex = nullptr;
SemaphoreHandle_t hostStopped = nullptr;
volatile bool stopRequested = false;
volatile bool teardownFailed = false;
volatile uint8_t pendingAddress = 0;
volatile bool deviceGone = false;
volatile bool controlDone = false;
volatile usb_transfer_status_t controlStatus = USB_TRANSFER_STATUS_ERROR;
uint8_t controlInterface = 0xff;
uint8_t dataInterface = 0xff;
uint8_t dataAlt = 0;
uint8_t epIn = 0;
uint8_t epOut = 0;
uint16_t epInMps = 64;
uint16_t epOutMps = 64;
bool controlClaimed = false;
bool dataClaimed = false;
bool txInFlight = false;
bool rxActive = false;
t5_usb_line_coding_t requestedCoding = {115200, 8, T5_USB_PARITY_NONE, 1, 0};
bool debugUsbSerialSuspended = false;

enum SerialDriverKind : uint8_t {
  SERIAL_DRIVER_NONE = 0,
  SERIAL_DRIVER_CDC,
  SERIAL_DRIVER_WCH_CDC,
  SERIAL_DRIVER_CP210X,
  SERIAL_DRIVER_CH34X,
};

enum ControlStep : int {
  CTRL_NONE = 0,
  CTRL_CDC_LINE = 1,
  CTRL_CDC_LINES = 2,
  CTRL_CP210X_ENABLE = 10,
  CTRL_CP210X_BAUD = 11,
  CTRL_CP210X_LINE = 12,
  CTRL_CP210X_LINES = 13,
  CTRL_CH34X_VERSION = 20,
  CTRL_CH34X_INIT = 21,
  CTRL_CH34X_BAUD = 22,
  CTRL_CH34X_LCR = 23,
  CTRL_CH34X_LINES = 24,
};

SerialDriverKind driverKind = SERIAL_DRIVER_NONE;
int controlStep = CTRL_NONE;
uint8_t ch34xVersion = 0;

void suspendDebugUsbSerial() {
#if defined(ENABLE_SERIAL_LOG) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  if (!debugUsbSerialSuspended) {
    // ESP32-S3 USB Serial/JTAG and USB OTG share the internal PHY. The firmware
    // starts HW CDC for logging at boot, so release it before the host driver
    // claims that PHY. Otherwise usb_host_install() fails immediately and the
    // normal failure cleanup removes OTG VBUS before the peripheral can boot.
    Serial.end();
    delay(20);
    debugUsbSerialSuspended = true;
  }
#endif
}

void restoreDebugUsbSerial() {
#if defined(ENABLE_SERIAL_LOG) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  if (debugUsbSerialSuspended) {
    delay(20);
    Serial.begin(115200);
    debugUsbSerialSuspended = false;
  }
#endif
}

bool bqRead(uint8_t reg, uint8_t* value) {
  if (!value) return false;
  BoardT5S3::ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_BQ25896_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)T5S3_BQ25896_ADDR, (uint8_t)1) != 1) return false;
  *value = (uint8_t)Wire.read();
  return true;
}

bool bqWrite(uint8_t reg, uint8_t value) {
  BoardT5S3::ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_BQ25896_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool setOtgPower(bool enable) {
  if (!BoardT5S3::beginBatteryManagement()) return false;
  if (enable && BoardT5S3::isUsbConnected()) return false; // Never back-drive an upstream host/charger.

  uint8_t reg03 = 0;
  if (!bqRead(BQ25896_REG_03, &reg03)) return false;
  if (enable) {
    uint8_t reg0a = 0;
    if (!bqRead(BQ25896_REG_0A, &reg0a)) return false;
    // 5.126 V (BOOSTV=9) and 1.2 A limit (BOOST_LIM=2).
    reg0a = (uint8_t)((reg0a & ~(BQ25896_REG0A_BOOSTV_MASK | BQ25896_REG0A_BOOST_LIM_MASK)) |
                     (9u << BQ25896_REG0A_BOOSTV_SHIFT) | 2u);
    if (!bqWrite(BQ25896_REG_0A, reg0a)) return false;
    reg03 &= (uint8_t)~BQ25896_REG03_CHG_CONFIG_MASK;
    reg03 |= BQ25896_REG03_OTG_CONFIG_MASK;
  } else {
    reg03 &= (uint8_t)~BQ25896_REG03_OTG_CONFIG_MASK;
    reg03 |= BQ25896_REG03_CHG_CONFIG_MASK;
  }
  if (!bqWrite(BQ25896_REG_03, reg03)) return false;
  delay(enable ? 80 : 20);
  return true;
}

void setError(int32_t error) {
  portENTER_CRITICAL(&stateMux);
  state.status = T5_USB_STATUS_ERROR;
  state.last_error = error;
  state.connected = device ? 1 : 0;
  portEXIT_CRITICAL(&stateMux);
}

void copyProduct(const usb_str_desc_t* desc) {
  portENTER_CRITICAL(&stateMux);
  state.product[0] = 0;
  if (desc && desc->bLength >= 2) {
    size_t chars = (desc->bLength - 2u) / 2u;
    if (chars >= T5_USB_PRODUCT_MAX) chars = T5_USB_PRODUCT_MAX - 1u;
    for (size_t i = 0; i < chars; ++i) {
      uint16_t wc = desc->wData[i];
      state.product[i] = (wc >= 32u && wc <= 126u) ? (char)wc : '?';
    }
    state.product[chars] = 0;
  }
  portEXIT_CRITICAL(&stateMux);
}

void setProductFallback(const char* name) {
  if (!name) return;
  portENTER_CRITICAL(&stateMux);
  if (!state.product[0]) {
    size_t i = 0;
    while (name[i] && i + 1u < T5_USB_PRODUCT_MAX) {
      state.product[i] = name[i];
      ++i;
    }
    state.product[i] = 0;
  }
  portEXIT_CRITICAL(&stateMux);
}

const char* driverName() {
  switch (driverKind) {
    case SERIAL_DRIVER_WCH_CDC: return "WCH CH343/CH9102";
    case SERIAL_DRIVER_CP210X: return "CP210x USB-UART";
    case SERIAL_DRIVER_CH34X: return "CH34x USB-UART";
    case SERIAL_DRIVER_CDC: return "USB CDC-ACM";
    default: return "USB serial";
  }
}

void pushRx(const uint8_t* data, size_t length) {
  portENTER_CRITICAL(&stateMux);
  for (size_t i = 0; i < length; ++i) {
    size_t next = (rxHead + 1u) % kRxRingSize;
    if (next == rxTail) {
      state.dropped_rx_bytes++;
      continue;
    }
    rxRing[rxHead] = data[i];
    rxHead = next;
    state.rx_bytes++;
  }
  portEXIT_CRITICAL(&stateMux);
}

void rxCallback(usb_transfer_t* transfer) {
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0)
    pushRx(transfer->data_buffer, (size_t)transfer->actual_num_bytes);
  if (!stopRequested && !deviceGone && device && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
    transfer->num_bytes = (int)((kBulkBufferSize / epInMps) * epInMps);
    if (transfer->num_bytes <= 0) transfer->num_bytes = epInMps;
    if (usb_host_transfer_submit(transfer) != ESP_OK) setError(-1101);
  }
}

void txCallback(usb_transfer_t* transfer) {
  portENTER_CRITICAL(&stateMux);
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0)
    state.tx_bytes += (uint32_t)transfer->actual_num_bytes;
  txInFlight = false;
  portEXIT_CRITICAL(&stateMux);
}

void controlCallback(usb_transfer_t* transfer) {
  controlStatus = transfer->status;
  controlDone = true;
}

void clientEvent(const usb_host_client_event_msg_t* event, void*) {
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    if (!device && pendingAddress == 0) pendingAddress = event->new_dev.address;
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    if (device && event->dev_gone.dev_hdl == device) deviceGone = true;
  }
}

bool parseCdc(const usb_config_desc_t* config) {
  controlInterface = dataInterface = 0xff;
  dataAlt = 0;
  epIn = epOut = 0;
  epInMps = epOutMps = 64;
  uint8_t currentInterface = 0xff;
  uint8_t currentAlt = 0;
  uint8_t currentClass = 0xff;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(config);
  size_t offset = 0;
  const size_t total = config->wTotalLength;
  while (offset + 2u <= total) {
    uint8_t length = p[offset];
    uint8_t type = p[offset + 1u];
    if (length < 2u || offset + length > total) break;
    if (type == 4u && length >= 9u) {
      currentInterface = p[offset + 2u];
      currentAlt = p[offset + 3u];
      currentClass = p[offset + 5u];
      if (currentClass == 0x02u && controlInterface == 0xff) controlInterface = currentInterface;
      if (currentClass == 0x0au && dataInterface == 0xff) {
        dataInterface = currentInterface;
        dataAlt = currentAlt;
      }
    } else if (type == 5u && length >= 7u && currentClass == 0x0au && currentInterface == dataInterface) {
      uint8_t address = p[offset + 2u];
      uint8_t attributes = p[offset + 3u] & 0x03u;
      uint16_t mps = (uint16_t)p[offset + 4u] | ((uint16_t)p[offset + 5u] << 8u);
      if (attributes == 2u) {
        if (address & 0x80u) { epIn = address; epInMps = mps; }
        else { epOut = address; epOutMps = mps; }
      }
    }
    offset += length;
  }
  return controlInterface != 0xff && dataInterface != 0xff && epIn && epOut && epInMps && epOutMps;
}

bool parseVendorBulk(const usb_config_desc_t* config) {
  controlInterface = dataInterface = 0xff;
  dataAlt = 0;
  epIn = epOut = 0;
  epInMps = epOutMps = 64;

  const uint8_t* p = reinterpret_cast<const uint8_t*>(config);
  size_t offset = 0;
  const size_t total = config->wTotalLength;
  uint8_t currentInterface = 0xff;
  uint8_t currentAlt = 0;
  uint8_t currentClass = 0;
  uint8_t candidateIn = 0;
  uint8_t candidateOut = 0;
  uint16_t candidateInMps = 64;
  uint16_t candidateOutMps = 64;

  auto commitCandidate = [&]() -> bool {
    if (currentClass != 0xffu || currentInterface == 0xffu || !candidateIn || !candidateOut) return false;
    controlInterface = dataInterface = currentInterface;
    dataAlt = currentAlt;
    epIn = candidateIn;
    epOut = candidateOut;
    epInMps = candidateInMps;
    epOutMps = candidateOutMps;
    return true;
  };

  while (offset + 2u <= total) {
    uint8_t length = p[offset];
    uint8_t type = p[offset + 1u];
    if (length < 2u || offset + length > total) break;
    if (type == 4u && length >= 9u) {
      if (commitCandidate()) return true;
      currentInterface = p[offset + 2u];
      currentAlt = p[offset + 3u];
      currentClass = p[offset + 5u];
      candidateIn = candidateOut = 0;
      candidateInMps = candidateOutMps = 64;
    } else if (type == 5u && length >= 7u && currentClass == 0xffu) {
      uint8_t address = p[offset + 2u];
      uint8_t attributes = p[offset + 3u] & 0x03u;
      uint16_t mps = (uint16_t)p[offset + 4u] | ((uint16_t)p[offset + 5u] << 8u);
      if (attributes == 2u && mps) {
        if (address & 0x80u) { candidateIn = address; candidateInMps = mps; }
        else { candidateOut = address; candidateOutMps = mps; }
      }
    }
    offset += length;
  }
  return commitCandidate();
}

bool isWchCdc(uint16_t vid, uint16_t pid) {
  // WCH CH343 and CH9102 enumerate as standards-compliant CDC-ACM devices.
  // Keep them on the CDC request path; they are not CH341-protocol bridges.
  return vid == 0x1a86u && (pid == 0x55d3u || pid == 0x55d4u);
}

bool isCp210x(uint16_t vid, uint16_t) {
  // Silicon Labs CP210x parts used on ESP32 development boards normally retain
  // the Silicon Labs VID while allowing product IDs to vary by board/vendor.
  return vid == 0x10c4u;
}

bool isCh34x(uint16_t vid, uint16_t pid) {
  // IDs supported by the upstream CH341 serial driver.
  return (vid == 0x1a86u && (pid == 0x5523u || pid == 0x7522u || pid == 0x7523u)) ||
         (vid == 0x4348u && pid == 0x5523u) ||
         (vid == 0x2184u && pid == 0x0057u) ||
         (vid == 0x9986u && pid == 0x7523u);
}

void fillSetup(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
               const uint8_t* payload, uint16_t payloadLength) {
  uint8_t* b = controlTransfer->data_buffer;
  b[0] = requestType;
  b[1] = request;
  b[2] = (uint8_t)(value & 0xffu); b[3] = (uint8_t)(value >> 8u);
  b[4] = (uint8_t)(index & 0xffu); b[5] = (uint8_t)(index >> 8u);
  b[6] = (uint8_t)(payloadLength & 0xffu); b[7] = (uint8_t)(payloadLength >> 8u);
  for (uint16_t i = 0; i < payloadLength; ++i) b[8u + i] = 0;
  if (!(requestType & 0x80u) && payload) {
    for (uint16_t i = 0; i < payloadLength; ++i) b[8u + i] = payload[i];
  }
  controlTransfer->device_handle = device;
  controlTransfer->bEndpointAddress = 0;
  controlTransfer->num_bytes = 8 + payloadLength;
  controlTransfer->callback = controlCallback;
  controlTransfer->context = nullptr;
}

bool submitControl(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                   const uint8_t* payload, uint16_t payloadLength, int step) {
  if (!device || !controlTransfer || payloadLength + 8u > kControlBufferSize) return false;
  fillSetup(requestType, request, value, index, payload, payloadLength);
  controlStep = step;
  controlDone = false;
  esp_err_t rc = usb_host_transfer_submit_control(client, controlTransfer);
  if (rc != ESP_OK) controlStep = CTRL_NONE;
  return rc == ESP_OK;
}

bool submitCdcLineCoding() {
  uint8_t payload[7];
  uint32_t baud = requestedCoding.baud_rate;
  payload[0] = (uint8_t)baud; payload[1] = (uint8_t)(baud >> 8u);
  payload[2] = (uint8_t)(baud >> 16u); payload[3] = (uint8_t)(baud >> 24u);
  payload[4] = requestedCoding.stop_bits == 2 ? 2u : 0u;
  payload[5] = requestedCoding.parity;
  payload[6] = requestedCoding.data_bits;
  return submitControl(0x21u, 0x20u, 0u, controlInterface, payload, sizeof(payload), CTRL_CDC_LINE);
}

bool submitCdcControlLines() {
  uint16_t value = (state.dtr ? 1u : 0u) | (state.rts ? 2u : 0u);
  return submitControl(0x21u, 0x22u, value, controlInterface, nullptr, 0, CTRL_CDC_LINES);
}

uint16_t cp210xLineControl() {
  uint16_t value = (uint16_t)(requestedCoding.data_bits & 0x0fu) << 8u;
  switch (requestedCoding.parity) {
    case T5_USB_PARITY_ODD: value |= 0x0010u; break;
    case T5_USB_PARITY_EVEN: value |= 0x0020u; break;
    case T5_USB_PARITY_MARK: value |= 0x0030u; break;
    case T5_USB_PARITY_SPACE: value |= 0x0040u; break;
    default: break;
  }
  if (requestedCoding.stop_bits == 2u) value |= 0x0002u;
  return value;
}

bool submitCp210xEnable() {
  return submitControl(0x41u, 0x00u, 0x0001u, dataInterface, nullptr, 0, CTRL_CP210X_ENABLE);
}

bool submitCp210xBaud() {
  uint32_t baud = requestedCoding.baud_rate;
  uint8_t payload[4] = {
      (uint8_t)baud, (uint8_t)(baud >> 8u), (uint8_t)(baud >> 16u), (uint8_t)(baud >> 24u)};
  return submitControl(0x41u, 0x1eu, 0u, dataInterface, payload, sizeof(payload), CTRL_CP210X_BAUD);
}

bool submitCp210xLine() {
  return submitControl(0x41u, 0x03u, cp210xLineControl(), dataInterface, nullptr, 0, CTRL_CP210X_LINE);
}

bool submitCp210xControlLines() {
  uint16_t value = 0x0300u;
  if (state.dtr) value |= 0x0001u;
  if (state.rts) value |= 0x0002u;
  return submitControl(0x41u, 0x07u, value, dataInterface, nullptr, 0, CTRL_CP210X_LINES);
}

uint8_t ch34xLcr() {
  uint8_t lcr = 0xc0u; // Enable receiver and transmitter.
  switch (requestedCoding.data_bits) {
    case 5: break;
    case 6: lcr |= 0x01u; break;
    case 7: lcr |= 0x02u; break;
    default: lcr |= 0x03u; break;
  }
  switch (requestedCoding.parity) {
    case T5_USB_PARITY_ODD: lcr |= 0x08u; break;
    case T5_USB_PARITY_EVEN: lcr |= 0x18u; break;
    case T5_USB_PARITY_MARK: lcr |= 0x28u; break;
    case T5_USB_PARITY_SPACE: lcr |= 0x38u; break;
    default: break;
  }
  if (requestedCoding.stop_bits == 2u) lcr |= 0x04u;
  return lcr;
}

bool ch34xDefaultLineCoding() {
  return requestedCoding.data_bits == 8u && requestedCoding.parity == T5_USB_PARITY_NONE &&
         requestedCoding.stop_bits == 1u;
}

bool ch34xDivisor(uint32_t speed, uint16_t* out) {
  if (!out || speed < 46u || speed > 3000000u) return false;
  constexpr uint32_t clockRate = 48000000u;
  int fact = 1;
  int ps = 3;
  for (; ps >= 0; --ps) {
    uint32_t clkDiv = 1u << (12 - 3 * ps - 1);
    uint32_t minRate = (clockRate + clkDiv * 512u - 1u) / (clkDiv * 512u);
    if (speed > minRate) break;
  }
  if (ps < 0) return false;

  uint32_t clkDiv = 1u << (12 - 3 * ps - fact);
  uint32_t div = clockRate / (clkDiv * speed);
  if (div < 9u || div > 255u) {
    div /= 2u;
    clkDiv *= 2u;
    fact = 0;
  }
  if (div < 2u || div > 256u) return false;

  if (div < 256u) {
    uint64_t actual16 = 16ull * clockRate / (clkDiv * div);
    uint64_t next16 = 16ull * clockRate / (clkDiv * (div + 1u));
    uint64_t wanted16 = 16ull * speed;
    if (actual16 >= wanted16 && wanted16 >= next16 &&
        actual16 - wanted16 >= wanted16 - next16) ++div;
  }
  if (fact == 1 && (div & 1u) == 0u) {
    div /= 2u;
    fact = 0;
  }

  uint16_t value = (uint16_t)(((0x100u - div) & 0xffu) << 8u);
  value |= (uint16_t)((fact & 1) << 2u);
  value |= (uint16_t)(ps & 0x03);
  if (ch34xVersion > 0x27u) value |= 0x0080u;
  *out = value;
  return true;
}

bool submitCh34xReadVersion() {
  return submitControl(0xc0u, 0x5fu, 0u, 0u, nullptr, 2u, CTRL_CH34X_VERSION);
}

bool submitCh34xInit() {
  return submitControl(0x40u, 0xa1u, 0u, 0u, nullptr, 0, CTRL_CH34X_INIT);
}

bool submitCh34xBaud() {
  uint16_t divisor = 0;
  if (!ch34xDivisor(requestedCoding.baud_rate, &divisor)) return false;
  return submitControl(0x40u, 0x9au, 0x1312u, divisor, nullptr, 0, CTRL_CH34X_BAUD);
}

bool submitCh34xLcr() {
  return submitControl(0x40u, 0x9au, 0x2518u, ch34xLcr(), nullptr, 0, CTRL_CH34X_LCR);
}

bool submitCh34xControlLines() {
  uint8_t control = 0;
  if (state.rts) control |= 0x40u;
  if (state.dtr) control |= 0x20u;
  return submitControl(0x40u, 0xa4u, (uint16_t)~(uint16_t)control, 0u, nullptr, 0, CTRL_CH34X_LINES);
}

bool submitLineCoding() {
  switch (driverKind) {
    case SERIAL_DRIVER_CDC:
    case SERIAL_DRIVER_WCH_CDC:
      return submitCdcLineCoding();
    case SERIAL_DRIVER_CP210X: return submitCp210xBaud();
    case SERIAL_DRIVER_CH34X: return submitCh34xBaud();
    default: return false;
  }
}

bool submitControlLines() {
  switch (driverKind) {
    case SERIAL_DRIVER_CDC:
    case SERIAL_DRIVER_WCH_CDC:
      return submitCdcControlLines();
    case SERIAL_DRIVER_CP210X: return submitCp210xControlLines();
    case SERIAL_DRIVER_CH34X: return submitCh34xControlLines();
    default: return false;
  }
}

bool markReady() {
  if (!rxActive) {
    rxTransfer->device_handle = device;
    rxTransfer->bEndpointAddress = epIn;
    rxTransfer->num_bytes = (int)((kBulkBufferSize / epInMps) * epInMps);
    if (rxTransfer->num_bytes <= 0) rxTransfer->num_bytes = epInMps;
    rxTransfer->callback = rxCallback;
    rxTransfer->context = nullptr;
    if (usb_host_transfer_submit(rxTransfer) != ESP_OK) return false;
    rxActive = true;
  }
  portENTER_CRITICAL(&stateMux);
  state.status = T5_USB_STATUS_READY;
  state.line_coding = requestedCoding;
  portEXIT_CRITICAL(&stateMux);
  controlStep = CTRL_NONE;
  return true;
}

void cleanupDevice() {
  if (!device) return;
  if (epIn) { (void)usb_host_endpoint_halt(device, epIn); (void)usb_host_endpoint_flush(device, epIn); }
  if (epOut) { (void)usb_host_endpoint_halt(device, epOut); (void)usb_host_endpoint_flush(device, epOut); }
  if (dataClaimed) (void)usb_host_interface_release(client, device, dataInterface);
  if (controlClaimed && controlInterface != dataInterface) (void)usb_host_interface_release(client, device, controlInterface);
  (void)usb_host_device_close(client, device);
  device = nullptr;
  dataClaimed = controlClaimed = false;
  epIn = epOut = 0;
  txInFlight = false;
  rxActive = false;
  deviceGone = false;
  controlStep = CTRL_NONE;
  driverKind = SERIAL_DRIVER_NONE;
  ch34xVersion = 0;
  portENTER_CRITICAL(&stateMux);
  state.connected = 0;
  state.vid = state.pid = 0;
  state.product[0] = 0;
  if (!stopRequested) state.status = T5_USB_STATUS_WAITING;
  portEXIT_CRITICAL(&stateMux);
}

bool beginDriverConfiguration() {
  switch (driverKind) {
    case SERIAL_DRIVER_CDC:
    case SERIAL_DRIVER_WCH_CDC:
      return submitCdcLineCoding();
    case SERIAL_DRIVER_CP210X: return submitCp210xEnable();
    case SERIAL_DRIVER_CH34X: return submitCh34xReadVersion();
    default: return false;
  }
}

bool configureDevice(uint8_t address) {
  if (usb_host_device_open(client, address, &device) != ESP_OK) return false;
  const usb_device_desc_t* devDesc = nullptr;
  const usb_config_desc_t* config = nullptr;
  if (usb_host_get_device_descriptor(device, &devDesc) != ESP_OK || !devDesc ||
      usb_host_get_active_config_descriptor(device, &config) != ESP_OK || !config) {
    (void)usb_host_device_close(client, device); device = nullptr; return false;
  }

  if (parseCdc(config)) {
    driverKind = isWchCdc(devDesc->idVendor, devDesc->idProduct) ? SERIAL_DRIVER_WCH_CDC : SERIAL_DRIVER_CDC;
  } else if (isCp210x(devDesc->idVendor, devDesc->idProduct) && parseVendorBulk(config)) {
    driverKind = SERIAL_DRIVER_CP210X;
  } else if (isCh34x(devDesc->idVendor, devDesc->idProduct) && parseVendorBulk(config)) {
    driverKind = SERIAL_DRIVER_CH34X;
  } else {
    (void)usb_host_device_close(client, device); device = nullptr; return false;
  }

  if (driverKind == SERIAL_DRIVER_CDC || driverKind == SERIAL_DRIVER_WCH_CDC) {
    if (usb_host_interface_claim(client, device, controlInterface, 0) != ESP_OK) {
      (void)usb_host_device_close(client, device); device = nullptr; return false;
    }
    controlClaimed = true;
    if (dataInterface != controlInterface) {
      if (usb_host_interface_claim(client, device, dataInterface, dataAlt) != ESP_OK) { cleanupDevice(); return false; }
      dataClaimed = true;
    } else {
      dataClaimed = true;
    }
  } else {
    if (usb_host_interface_claim(client, device, dataInterface, dataAlt) != ESP_OK) {
      (void)usb_host_device_close(client, device); device = nullptr; driverKind = SERIAL_DRIVER_NONE; return false;
    }
    dataClaimed = true;
    controlClaimed = true;
  }

  usb_device_info_t info = {};
  if (usb_host_device_info(device, &info) == ESP_OK) copyProduct(info.str_desc_product);
  setProductFallback(driverName());
  portENTER_CRITICAL(&stateMux);
  state.status = T5_USB_STATUS_CONFIGURING;
  state.connected = 1;
  state.vid = devDesc->idVendor;
  state.pid = devDesc->idProduct;
  state.line_coding = requestedCoding;
  state.dtr = 1;
  state.rts = 1;
  portEXIT_CRITICAL(&stateMux);
  controlStep = CTRL_NONE;
  ch34xVersion = 0;
  return beginDriverConfiguration();
}

void handleControlCompletion() {
  int completedStep = controlStep;
  controlDone = false;
  controlStep = CTRL_NONE;
  if (controlStatus != USB_TRANSFER_STATUS_COMPLETED) {
    setError(-1102);
    return;
  }

  switch (completedStep) {
    case CTRL_CDC_LINE:
      if (!submitCdcControlLines()) setError(-1103);
      break;
    case CTRL_CDC_LINES:
      if (!markReady()) setError(-1104);
      break;

    case CTRL_CP210X_ENABLE:
      if (!submitCp210xBaud()) setError(-1110);
      break;
    case CTRL_CP210X_BAUD:
      if (!submitCp210xLine()) setError(-1111);
      break;
    case CTRL_CP210X_LINE:
      if (!submitCp210xControlLines()) setError(-1112);
      break;
    case CTRL_CP210X_LINES:
      if (!markReady()) setError(-1113);
      break;

    case CTRL_CH34X_VERSION:
      ch34xVersion = controlTransfer->data_buffer[8];
      if (!submitCh34xInit()) setError(-1120);
      break;
    case CTRL_CH34X_INIT:
      if (!submitCh34xBaud()) setError(-1121);
      break;
    case CTRL_CH34X_BAUD:
      if (ch34xVersion >= 0x30u) {
        if (!submitCh34xLcr()) setError(-1122);
      } else if (!ch34xDefaultLineCoding()) {
        setError(-1123); // Older CH34x revisions expose fixed/default 8N1 line control here.
      } else if (!submitCh34xControlLines()) {
        setError(-1124);
      }
      break;
    case CTRL_CH34X_LCR:
      if (!submitCh34xControlLines()) setError(-1125);
      break;
    case CTRL_CH34X_LINES:
      if (!markReady()) setError(-1126);
      break;
    default:
      break;
  }
}

bool teardownHostLibrary() {
  esp_err_t freeRc = usb_host_device_free_all();
  bool allFree = freeRc == ESP_OK;
  if (freeRc != ESP_OK && freeRc != ESP_ERR_NOT_FINISHED) return false;

  for (int i = 0; !allFree && i < 500; ++i) {
    uint32_t flags = 0;
    (void)usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      freeRc = usb_host_device_free_all();
      if (freeRc == ESP_OK) allFree = true;
      else if (freeRc != ESP_ERR_NOT_FINISHED) return false;
    }
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
  }

  // ALL_FREE can race the bounded event loop; let uninstall make the final
  // authoritative decision rather than abandoning a clean host state.
  esp_err_t uninstallRc = usb_host_uninstall();
  if (uninstallRc == ESP_OK) return true;

  // Give the daemon one final chance to finish a device-free transition before
  // reporting a teardown failure. Never pretend the host is reusable if the
  // ESP-IDF host library still considers itself installed.
  for (int i = 0; i < 100; ++i) {
    uint32_t flags = 0;
    (void)usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) (void)usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      uninstallRc = usb_host_uninstall();
      if (uninstallRc == ESP_OK) return true;
    }
  }
  return false;
}

void hostTask(void*) {
  usb_host_config_t hostConfig = {};
  usb_host_client_config_t clientConfig = {};
  bool hostInstalled = false;
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 5;
  clientConfig.async.client_event_callback = clientEvent;
  clientConfig.async.callback_arg = nullptr;

  esp_err_t rc = usb_host_install(&hostConfig);
  if (rc != ESP_OK) { setError(rc); goto finish_power; }
  hostInstalled = true;

  rc = usb_host_client_register(&clientConfig, &client);
  if (rc != ESP_OK) { setError(rc); goto finish_host; }

  if (usb_host_transfer_alloc(kBulkBufferSize, 0, &rxTransfer) != ESP_OK ||
      usb_host_transfer_alloc(kBulkBufferSize, 0, &txTransfer) != ESP_OK ||
      usb_host_transfer_alloc(kControlBufferSize, 0, &controlTransfer) != ESP_OK) {
    setError(ESP_ERR_NO_MEM); goto finish_client;
  }

  portENTER_CRITICAL(&stateMux); state.status = T5_USB_STATUS_WAITING; portEXIT_CRITICAL(&stateMux);
  while (!stopRequested) {
    uint32_t flags = 0;
    (void)usb_host_lib_handle_events(0, &flags);
    (void)usb_host_client_handle_events(client, pdMS_TO_TICKS(20));

    if (deviceGone) cleanupDevice();
    if (!device && pendingAddress) {
      uint8_t address = pendingAddress; pendingAddress = 0;
      if (!configureDevice(address)) {
        portENTER_CRITICAL(&stateMux);
        if (state.status != T5_USB_STATUS_ERROR) state.status = T5_USB_STATUS_WAITING;
        portEXIT_CRITICAL(&stateMux);
      }
    }
    if (device && controlDone) handleControlCompletion();
  }

  cleanupDevice();
  if (controlTransfer) { usb_host_transfer_free(controlTransfer); controlTransfer = nullptr; }
  if (txTransfer) { usb_host_transfer_free(txTransfer); txTransfer = nullptr; }
  if (rxTransfer) { usb_host_transfer_free(rxTransfer); rxTransfer = nullptr; }
finish_client:
  if (client) {
    const esp_err_t deregisterRc = usb_host_client_deregister(client);
    if (deregisterRc == ESP_OK) client = nullptr;
    else {
      teardownFailed = true;
      setError(-1140);
    }
  }
finish_host:
  if (hostInstalled && !client && !teardownHostLibrary()) {
    teardownFailed = true;
    setError(-1141);
  }
finish_power:
  (void)setOtgPower(false);
  restoreDebugUsbSerial();
  running = false;
  hostTaskHandle = nullptr;
  if (hostStopped) xSemaphoreGive(hostStopped);
  vTaskDelete(nullptr);
}

bool supported() { return true; }

bool validCoding(const t5_usb_line_coding_t* coding) {
  return coding && coding->baud_rate >= 300u && coding->baud_rate <= 3000000u &&
         coding->data_bits >= 5u && coding->data_bits <= 8u && coding->parity <= T5_USB_PARITY_SPACE &&
         (coding->stop_bits == 1u || coding->stop_bits == 2u);
}

bool serialStart(const t5_usb_line_coding_t* coding) {
  if (!active() || !validCoding(coding)) return false;
  if (running) {
    if (stopRequested) return false;
    requestedCoding = *coding;
    portENTER_CRITICAL(&stateMux); state.line_coding = *coding; portEXIT_CRITICAL(&stateMux);
    return true;
  }
  if (!hostStopped) hostStopped = xSemaphoreCreateBinary();
  if (!hostStopped) { setError(ESP_ERR_NO_MEM); return false; }
  while (xSemaphoreTake(hostStopped, 0) == pdTRUE) {}

  resetState(T5_USB_STATUS_OFF);
  requestedCoding = *coding;
  portENTER_CRITICAL(&stateMux); state.line_coding = *coding; portEXIT_CRITICAL(&stateMux);
  suspendDebugUsbSerial();
  if (!setOtgPower(true)) { restoreDebugUsbSerial(); setError(-1001); return false; }
  if (!txMutex) txMutex = xSemaphoreCreateMutex();
  if (!txMutex) {
    (void)setOtgPower(false);
    restoreDebugUsbSerial();
    setError(ESP_ERR_NO_MEM);
    return false;
  }
  teardownFailed = false;
  stopRequested = false;
  pendingAddress = 0;
  deviceGone = false;
  running = true;
  if (xTaskCreatePinnedToCore(hostTask, "usb-serial-host", 7168, nullptr, 3, &hostTaskHandle, 0) != pdPASS) {
    running = false;
    (void)setOtgPower(false);
    restoreDebugUsbSerial();
    setError(ESP_ERR_NO_MEM);
    return false;
  }
  return true;
}

void serialStop() {
  if (!running) {
    if (!teardownFailed) resetState(T5_USB_STATUS_OFF);
    return;
  }
  stopRequested = true;
  if (client) (void)usb_host_client_unblock(client);
  (void)usb_host_lib_unblock();

  if (!hostStopped || xSemaphoreTake(hostStopped, pdMS_TO_TICKS(kUsbShutdownTimeoutMs)) != pdTRUE) {
    teardownFailed = true;
    setError(-1142);
    return;
  }
  if (!teardownFailed) resetState(T5_USB_STATUS_OFF);
}

bool serialReadState(t5_usb_serial_state_t* out) {
  if (!out) return false;
  portENTER_CRITICAL(&stateMux); *out = state; portEXIT_CRITICAL(&stateMux);
  return true;
}

bool serialSetLineCoding(const t5_usb_line_coding_t* coding) {
  if (!validCoding(coding)) return false;
  requestedCoding = *coding;
  portENTER_CRITICAL(&stateMux); state.line_coding = *coding; portEXIT_CRITICAL(&stateMux);
  if (!device || state.status < T5_USB_STATUS_CONFIGURING) return true;
  if (controlStep != CTRL_NONE) return false;
  portENTER_CRITICAL(&stateMux); state.status = T5_USB_STATUS_CONFIGURING; portEXIT_CRITICAL(&stateMux);
  if (!submitLineCoding()) { setError(-1130); return false; }
  return true;
}

bool serialSetControlLines(bool dtr, bool rts) {
  portENTER_CRITICAL(&stateMux); state.dtr = dtr; state.rts = rts; portEXIT_CRITICAL(&stateMux);
  if (!device) return true;
  if (controlStep != CTRL_NONE) return false;
  portENTER_CRITICAL(&stateMux); state.status = T5_USB_STATUS_CONFIGURING; portEXIT_CRITICAL(&stateMux);
  if (!submitControlLines()) { setError(-1131); return false; }
  return true;
}

size_t serialRead(uint8_t* data, size_t capacity) {
  if (!data || capacity == 0) return 0;
  size_t count = 0;
  portENTER_CRITICAL(&stateMux);
  while (count < capacity && rxTail != rxHead) {
    data[count++] = rxRing[rxTail];
    rxTail = (rxTail + 1u) % kRxRingSize;
  }
  portEXIT_CRITICAL(&stateMux);
  return count;
}

size_t serialWrite(const uint8_t* data, size_t length) {
  if (!data || length == 0 || !device || state.status != T5_USB_STATUS_READY || !txTransfer || !txMutex) return 0;
  if (length > kBulkBufferSize) length = kBulkBufferSize;
  if (xSemaphoreTake(txMutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
  if (txInFlight) { xSemaphoreGive(txMutex); return 0; }
  std::memcpy(txTransfer->data_buffer, data, length);
  txTransfer->device_handle = device;
  txTransfer->bEndpointAddress = epOut;
  txTransfer->num_bytes = (int)length;
  txTransfer->callback = txCallback;
  txTransfer->context = nullptr;
  txInFlight = true;
  esp_err_t rc = usb_host_transfer_submit(txTransfer);
  if (rc != ESP_OK) txInFlight = false;
  xSemaphoreGive(txMutex);
  return rc == ESP_OK ? length : 0;
}

#else
bool supported() { return false; }
bool serialStart(const t5_usb_line_coding_t*) { resetState(T5_USB_STATUS_UNSUPPORTED); return false; }
void serialStop() { resetState(T5_USB_STATUS_UNSUPPORTED); }
bool serialReadState(t5_usb_serial_state_t* out) { if (!out) return false; resetState(T5_USB_STATUS_UNSUPPORTED); *out = state; return true; }
bool serialSetLineCoding(const t5_usb_line_coding_t*) { return false; }
bool serialSetControlLines(bool, bool) { return false; }
size_t serialRead(uint8_t*, size_t) { return 0; }
size_t serialWrite(const uint8_t*, size_t) { return 0; }
#endif

const t5_usb_api_v1 kApi = {
    T5_USB_API_VERSION,
    sizeof(t5_usb_api_v1),
    supported,
    serialStart,
    serialStop,
    serialReadState,
    serialSetLineCoding,
    serialSetControlLines,
    serialRead,
    serialWrite,
};
} // namespace

extern "C" const t5_usb_api_v1* t5_usb_get_api(uint32_t apiVersion) {
  if (apiVersion != T5_USB_API_VERSION || !active()) return nullptr;
  return &kApi;
}
