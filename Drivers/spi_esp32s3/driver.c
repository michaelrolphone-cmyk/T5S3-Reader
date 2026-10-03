/* Installed SPI bus ELF: admission, generation-safe sessions and lifecycle.
 * Only this provider imports the temporary raw-controller firmware port.
 * SD protocol/FAT users resolve spi.bus and cannot import that private port.
 */
#include "RiscSpiBusV1.h"
#include "RiscFirmwareSpiCompatV1.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static SemaphoreHandle_t lock;
static bool started;
static uint64_t generation, claim, session;
static TaskHandle_t session_owner;
static TickType_t session_began;
static size_t session_bytes;

static bool enter(void) {
    TickType_t wait = pdMS_TO_TICKS(100);
    return lock && xSemaphoreTake(lock, wait ? wait : 1) == pdTRUE;
}
static void leave(void) { (void)xSemaphoreGive(lock); }
static bool owns(uint64_t token) {
    return started && token && token == session && session_owner == xTaskGetCurrentTaskHandle();
}
static bool live(uint64_t token) {
    return owns(token) && (TickType_t)(xTaskGetTickCount()-session_began) < pdMS_TO_TICKS(RISC_SPI_SESSION_MAX_MS);
}
static bool claim_device(void *context, uint8_t chip_select, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    // The independently selected T5 profile grants its SD CS12 resource.
    // CS46 remains resident LoRa ownership until that client is extracted.
    if (!out || chip_select != 12 || !enter()) return false;
    const bool ok = started && !claim && generation != UINT64_MAX;
    if (ok) *out = claim = ++generation;
    leave(); return ok;
}
static bool begin(void *context, uint64_t token, uint32_t hz, bool selected, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || !enter()) return false;
    bool ok = started && token && token == claim && !session &&
              generation != UINT64_MAX && xTaskGetCurrentTaskHandle() != NULL;
    if (ok) ok = risc_fw_spi_begin_v1(12, hz, selected);
    if (ok) {
        *out = session = ++generation;
        session_owner = xTaskGetCurrentTaskHandle();
        session_began = xTaskGetTickCount();
        session_bytes = 0;
    }
    leave(); return ok;
}
static bool select_device(void *context, uint64_t token, bool selected) {
    (void)context;
    if (!enter()) return false;
    const bool ok = live(token) && risc_fw_spi_select_v1(selected);
    leave(); return ok;
}
static bool transfer(void *context, uint64_t token, const uint8_t *tx, uint8_t *rx, size_t bytes) {
    (void)context;
    if (!bytes || bytes > RISC_SPI_TRANSFER_MAX || !enter()) return false;
    bool ok = live(token) && bytes <= RISC_SPI_SESSION_MAX_BYTES-session_bytes;
    if (ok) {
        // Count attempted bytes as well as completed bytes. On failure the
        // caller may end the session but cannot extend/retry its I/O budget.
        session_bytes += bytes;
        ok = risc_fw_spi_transfer_v1(tx, rx, bytes);
        if (!ok) session_bytes = RISC_SPI_SESSION_MAX_BYTES;
    }
    leave(); return ok;
}
static bool end(void *context, uint64_t token) {
    (void)context;
    if (!enter()) return false;
    const bool ok = owns(token) && risc_fw_spi_end_v1();
    if (ok) { session = 0; session_owner = NULL; session_bytes = 0; }
    leave(); return ok;
}
static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!enter()) return false;
    const bool ok = started && token && token == claim && !session;
    if (ok) claim = 0;
    leave(); return ok;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    (void)deps;
    if (count || started || claim || session) return false;
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    started = true; return true;
}
static bool quiesce(void) {
    if (!lock) return !started && !claim && !session;
    if (!enter()) return false;
    const bool safe = !claim && !session;
    if (safe) started = false;
    leave();
    // Admission is revoked by the provider graph before quiesce. Deletion is
    // safe only after all claims/sessions have drained, including failed ones.
    if (safe) { vSemaphoreDelete(lock); lock = NULL; }
    return safe;
}
static void stop(void) { (void)quiesce(); }
static const risc_spi_bus_api_v1 api = {
    RISC_SPI_BUS_API_V1, sizeof(api), NULL,
    claim_device, begin, select_device, transfer, end, release_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "spi-esp32s3-v1",
    "spi.bus", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
