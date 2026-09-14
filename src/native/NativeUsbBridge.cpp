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

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
t5_usb_serial_state_t state = {};
uint8_t rxRing[kRxRingSize];
size_t rxHead = 0;
size_t rxTail = 0;
bool running = false;

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
volatile bool stopRequested = false;
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
int controlStep = 0;
t5_usb_line_coding_t requestedCoding = {115200, 8, T5_USB_PARITY_NONE, 1, 0};

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
  state.connected = 0;
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

void fillSetup(uint8_t request, uint16_t value, const uint8_t* payload, uint16_t payloadLength) {
  uint8_t* b = controlTransfer->data_buffer;
  b[0] = 0x21; // host-to-device, class, interface
  b[1] = request;
  b[2] = (uint8_t)(value & 0xffu); b[3] = (uint8_t)(value >> 8u);
  b[4] = controlInterface; b[5] = 0;
  b[6] = (uint8_t)(payloadLength & 0xffu); b[7] = (uint8_t)(payloadLength >> 8u);
  for (uint16_t i = 0; i < payloadLength; ++i) b[8u + i] = payload[i];
  controlTransfer->device_handle = device;
  controlTransfer->bEndpointAddress = 0;
  controlTransfer->num_bytes = 8 + payloadLength;
  controlTransfer->callback = controlCallback;
  controlTransfer->context = nullptr;
}

bool submitLineCoding() {
  uint8_t payload[7];
  uint32_t baud = requestedCoding.baud_rate;
  payload[0] = (uint8_t)baud; payload[1] = (uint8_t)(baud >> 8u);
  payload[2] = (uint8_t)(baud >> 16u); payload[3] = (uint8_t)(baud >> 24u);
  payload[4] = requestedCoding.stop_bits == 2 ? 2u : 0u;
  payload[5] = requestedCoding.parity;
  payload[6] = requestedCoding.data_bits;
  fillSetup(0x20u, 0u, payload, sizeof(payload)); // SET_LINE_CODING
  controlStep = 1;
  controlDone = false;
  return usb_host_transfer_submit_control(client, controlTransfer) == ESP_OK;
}

bool submitControlLines() {
  uint16_t value = (state.dtr ? 1u : 0u) | (state.rts ? 2u : 0u);
  fillSetup(0x22u, value, nullptr, 0); // SET_CONTROL_LINE_STATE
  controlStep = 2;
  controlDone = false;
  return usb_host_transfer_submit_control(client, controlTransfer) == ESP_OK;
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
  deviceGone = false;
  controlStep = 0;
  portENTER_CRITICAL(&stateMux);
  state.connected = 0;
  state.vid = state.pid = 0;
  state.product[0] = 0;
  if (!stopRequested) state.status = T5_USB_STATUS_WAITING;
  portEXIT_CRITICAL(&stateMux);
}

bool configureDevice(uint8_t address) {
  if (usb_host_device_open(client, address, &device) != ESP_OK) return false;
  const usb_device_desc_t* devDesc = nullptr;
  const usb_config_desc_t* config = nullptr;
  if (usb_host_get_device_descriptor(device, &devDesc) != ESP_OK || !devDesc ||
      usb_host_get_active_config_descriptor(device, &config) != ESP_OK || !config || !parseCdc(config)) {
    (void)usb_host_device_close(client, device); device = nullptr; return false;
  }
  if (usb_host_interface_claim(client, device, controlInterface, 0) != ESP_OK) {
    (void)usb_host_device_close(client, device); device = nullptr; return false;
  }
  controlClaimed = true;
  if (dataInterface != controlInterface) {
    if (usb_host_interface_claim(client, device, dataInterface, dataAlt) != ESP_OK) { cleanupDevice(); return false; }
    dataClaimed = true;
  } else dataClaimed = true;

  usb_device_info_t info = {};
  if (usb_host_device_info(device, &info) == ESP_OK) copyProduct(info.str_desc_product);
  portENTER_CRITICAL(&stateMux);
  state.status = T5_USB_STATUS_CONFIGURING;
  state.connected = 1;
  state.vid = devDesc->idVendor;
  state.pid = devDesc->idProduct;
  state.line_coding = requestedCoding;
  state.dtr = 1;
  state.rts = 1;
  portEXIT_CRITICAL(&stateMux);
  return submitLineCoding();
}

void hostTask(void*) {
  usb_host_config_t hostConfig = {};
  usb_host_client_config_t clientConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 5;
  clientConfig.async.client_event_callback = clientEvent;
  clientConfig.async.callback_arg = nullptr;

  esp_err_t rc = usb_host_install(&hostConfig);
  if (rc != ESP_OK) { setError(rc); goto finish_power; }

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
    if (device && controlDone) {
      controlDone = false;
      if (controlStatus != USB_TRANSFER_STATUS_COMPLETED) {
        setError(-1102);
      } else if (controlStep == 1) {
        if (!submitControlLines()) setError(-1103);
      } else if (controlStep == 2) {
        rxTransfer->device_handle = device;
        rxTransfer->bEndpointAddress = epIn;
        rxTransfer->num_bytes = (int)((kBulkBufferSize / epInMps) * epInMps);
        if (rxTransfer->num_bytes <= 0) rxTransfer->num_bytes = epInMps;
        rxTransfer->callback = rxCallback;
        rxTransfer->context = nullptr;
        if (usb_host_transfer_submit(rxTransfer) == ESP_OK) {
          portENTER_CRITICAL(&stateMux); state.status = T5_USB_STATUS_READY; portEXIT_CRITICAL(&stateMux);
        } else setError(-1104);
      }
    }
  }

  cleanupDevice();
  if (controlTransfer) { usb_host_transfer_free(controlTransfer); controlTransfer = nullptr; }
  if (txTransfer) { usb_host_transfer_free(txTransfer); txTransfer = nullptr; }
  if (rxTransfer) { usb_host_transfer_free(rxTransfer); rxTransfer = nullptr; }
finish_client:
  if (client) { (void)usb_host_client_deregister(client); client = nullptr; }
  (void)usb_host_device_free_all();
  for (int i = 0; i < 20; ++i) {
    uint32_t f = 0;
    (void)usb_host_lib_handle_events(pdMS_TO_TICKS(5), &f);
    if (f & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) break;
  }
finish_host:
  (void)usb_host_uninstall();
finish_power:
  (void)setOtgPower(false);
  running = false;
  hostTaskHandle = nullptr;
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
    requestedCoding = *coding;
    return true;
  }
  resetState(T5_USB_STATUS_OFF);
  requestedCoding = *coding;
  if (!setOtgPower(true)) { setError(-1001); return false; }
  if (!txMutex) txMutex = xSemaphoreCreateMutex();
  if (!txMutex) { (void)setOtgPower(false); setError(ESP_ERR_NO_MEM); return false; }
  stopRequested = false;
  pendingAddress = 0;
  deviceGone = false;
  running = true;
  if (xTaskCreatePinnedToCore(hostTask, "usb-cdc-host", 6144, nullptr, 3, &hostTaskHandle, 0) != pdPASS) {
    running = false; (void)setOtgPower(false); setError(ESP_ERR_NO_MEM); return false;
  }
  return true;
}

void serialStop() {
  if (!running) { resetState(T5_USB_STATUS_OFF); return; }
  stopRequested = true;
  if (client) (void)usb_host_client_unblock(client);
  (void)usb_host_lib_unblock();
  for (int i = 0; i < 100 && running; ++i) delay(10);
  resetState(T5_USB_STATUS_OFF);
}

bool serialReadState(t5_usb_serial_state_t* out) {
  if (!out) return false;
  portENTER_CRITICAL(&stateMux); *out = state; portEXIT_CRITICAL(&stateMux);
  return true;
}

bool serialSetLineCoding(const t5_usb_line_coding_t* coding) {
  if (!validCoding(coding)) return false;
  requestedCoding = *coding;
  if (!device || state.status < T5_USB_STATUS_CONFIGURING) return true;
  portENTER_CRITICAL(&stateMux); state.line_coding = *coding; state.status = T5_USB_STATUS_CONFIGURING; portEXIT_CRITICAL(&stateMux);
  return submitLineCoding();
}

bool serialSetControlLines(bool dtr, bool rts) {
  portENTER_CRITICAL(&stateMux); state.dtr = dtr; state.rts = rts; portEXIT_CRITICAL(&stateMux);
  if (!device) return true;
  return submitControlLines();
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
