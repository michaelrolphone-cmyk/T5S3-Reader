#pragma once
#include <RiscProviderV2.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_CAMERA_CAPTURE_API_V1 1u
#define RISC_CAMERA_CAPTURE_CAPABILITY "camera.capture"
#define RISC_CAMERA_JPEG 1u
/* Calls run on the serialized provider owner under a live capability lease.
 * Tokens identify one capture within that mapped provider generation. No frame
 * pointers cross the ELF boundary. The returned endpoint needs a separately
 * authorized generic stream grant; possession of its handle grants no access.
 * One bounded frame/job; finish or failure stays queryable until release.
 * AGAIN requires returning to the scheduler, never polling in a busy loop. */
typedef struct {
    uint32_t struct_size, format, width, height, quality, deadline_ms;
} risc_camera_request_v1;
enum { RISC_CAMERA_IDLE, RISC_CAMERA_CAPTURING, RISC_CAMERA_DELIVERING,
       RISC_CAMERA_DONE, RISC_CAMERA_FAILED };
typedef struct {
    uint32_t struct_size, state, format, width, height, length, transferred;
    int32_t result;
    char detail[64]; /* copied bounded failure/timeout diagnostic */
} risc_camera_status_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    int32_t (*capture)(void *, const risc_camera_request_v1 *, uint64_t *job, uint32_t *stream);
    /* Bounded read via the provider-owned stream under the camera lease.
     * Consumer never receives the DMA buffer; max capacity is 512 bytes. */
    int32_t (*read)(void *, uint64_t job, void *bytes, uint32_t capacity, uint32_t *count);
    int32_t (*status)(void *, uint64_t job, risc_camera_status_v1 *);
    int32_t (*cancel)(void *, uint64_t job);
    /* Closes the endpoint. An uncertain hardware stop returns BUSY and retains
     * job, DMA and module resources; caller must retain its capability lease. */
    int32_t (*release)(void *, uint64_t job);
} risc_camera_capture_api_v1;
#ifdef __cplusplus
}
#endif
