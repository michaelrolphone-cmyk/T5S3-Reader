#pragma once
/* Transport-neutral raw touch capability.
 *
 * Exactly one physical provider owns and acknowledges the touch controller.
 * Consumers never read controller registers directly. The event stream preserves
 * DOWN/MOVE/UP transitions while snapshot() is the authoritative current state,
 * allowing a consumer to recover safely after queue overflow or a slow frame.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_TOUCH_API_V1 1u
#define RISC_TOUCH_MAX_CONTACTS 5u
#define RISC_TOUCH_EVENT_QUEUE_MIN 16u

enum {
    RISC_TOUCH_PHASE_DOWN = 1u,
    RISC_TOUCH_PHASE_MOVE = 2u,
    RISC_TOUCH_PHASE_UP = 3u,
    /* Indicates that the consumer must discard local history and resnapshot.
     * A provider emits this when bounded event history was lost. */
    RISC_TOUCH_PHASE_RESYNC = 4u
};

typedef struct {
    uint8_t id;
    uint8_t phase;
    uint16_t x;
    uint16_t y;
    uint64_t timestamp_us;
    uint32_t sequence;
} risc_touch_event_v1;

typedef struct {
    uint8_t id;
    uint8_t active;
    uint16_t x;
    uint16_t y;
} risc_touch_contact_v1;

typedef struct {
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint8_t contact_count;
    uint8_t reserved[3];
    risc_touch_contact_v1 contacts[RISC_TOUCH_MAX_CONTACTS];
} risc_touch_snapshot_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;

    /* Returns a nonzero independent cursor. start_sequence==0 starts after the
     * current event tail, preventing stale pre-launch touches from leaking into
     * a new consumer. A nonzero sequence requests replay when still retained. */
    uint64_t (*subscribe)(void *context, uint32_t start_sequence);
    bool (*unsubscribe)(void *context, uint64_t subscription);

    /* Bounded provider work. Returns false only for provider/session failure.
     * Consumers should call next() afterwards until it returns 0. */
    bool (*poll)(void *context, uint32_t budget_ms);

    /* 1 = event copied, 0 = no event ready, -1 = cursor overrun/invalid.
     * On -1 the consumer MUST call snapshot() and resubscribe. */
    int32_t (*next)(void *context, uint64_t subscription, risc_touch_event_v1 *out);

    /* Authoritative current contacts and surface geometry. This function is the
     * recovery source of truth and must not consume queued events. */
    bool (*snapshot)(void *context, risc_touch_snapshot_v1 *out);

    /* Drop provider-side transient history and require a fresh physical DOWN
     * before another DOWN/UP pair is emitted. Used at UI/security boundaries. */
    bool (*reset)(void *context);
} risc_touch_api_v1;

#ifdef __cplusplus
}
#endif
