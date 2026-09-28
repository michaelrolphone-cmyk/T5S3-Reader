#ifndef MODEL_VIEWER_CONTROLS_H
#define MODEL_VIEWER_CONTROLS_H

/* App-owned camera policy over installed HID/XInput current-state providers.
 * No report parsing, USB lifecycle calls, event subscriptions or input FIFO. */
#include "T5ProviderCapabilityApi.h"
#include "RiscUsbHidV1.h"
#include <string.h>

#define MV_PAD_BACK    (1u << 0)
#define MV_PAD_FINE    (1u << 1)
#define MV_PAD_LEFT    (1u << 2)
#define MV_PAD_RIGHT   (1u << 3)
#define MV_PAD_UP      (1u << 4)
#define MV_PAD_DOWN    (1u << 5)
#define MV_PAD_LB      (1u << 6)
#define MV_PAD_RB      (1u << 7)
#define MV_PAD_DIRECTIONS (MV_PAD_LEFT | MV_PAD_RIGHT | MV_PAD_UP | MV_PAD_DOWN)
#define MV_PAD_DEVICE_LIMIT 4u
#define MV_MOTION_RAMP_MS 120u
#define MV_MOTION_MAX_FRAME_MS 48u
#define MV_ROTATE_RADIANS_PER_SEC 3.90f
#define MV_PAN_PIXELS_PER_SEC 2880.0f
#define MV_ZOOM_RATE_PER_SEC 18.0f
#define MV_FINE_SPEED_SCALE 0.25f
#define MV_MODE_NEUTRAL_MS 48u

typedef struct {
    const risc_usb_gamepad_api_v1 *api;
    t5_provider_capability_lease_t lease;
    uint64_t device;
    uint32_t held;
    bool armed, connected, snapshot_valid;
} mv_pad_source_t;

typedef struct {
    const t5_provider_capability_api_v1 *caps;
    mv_pad_source_t sources[2]; /* HID, XInput */
    uint32_t held;
    unsigned selected;
    bool mapped_camera_blocked;
} mv_controller_t;

typedef struct {
    uint32_t last_ms, action, ramp_ms, pending_ms;
    bool initialized;
    uint32_t bumper_mode, neutral_since_ms;
    bool neutral_pending;
} mv_motion_t;

typedef struct {
    float yaw, pitch, pan_x, pan_y, zoom_factor;
} mv_motion_delta_t;

static uint32_t mv_pad_decode(const risc_usb_gamepad_state_v1 *pad, bool xinput) {
    if (!pad->connected) return 0u;
    uint32_t held = 0;
    /* XInput normalizes B/A to bits 0/1. The tested HID SNES pad exposes
     * A/B as Button 1/2. A is exclusively a held precision modifier here,
     * not a camera-reset action (including when pressed before a direction). */
    if (pad->buttons & (xinput ? 1u : 2u)) held |= MV_PAD_BACK;
    if (pad->buttons & (xinput ? 2u : 1u)) held |= MV_PAD_FINE;
    if (pad->buttons & (1u << 4)) held |= MV_PAD_LB;
    if (pad->buttons & (1u << 5)) held |= MV_PAD_RB;
    if (pad->hat <= 7u) {
        if (pad->hat >= 5u) held |= MV_PAD_LEFT;
        if (pad->hat >= 1u && pad->hat <= 3u) held |= MV_PAD_RIGHT;
        if (pad->hat == 7u || pad->hat <= 1u) held |= MV_PAD_UP;
        if (pad->hat >= 3u && pad->hat <= 5u) held |= MV_PAD_DOWN;
    }
    /* Some SNES-style HID pads encode their D-pad as X/Y instead of a hat. */
    if (pad->x < -16000) held |= MV_PAD_LEFT;
    if (pad->x > 16000) held |= MV_PAD_RIGHT;
    if (pad->y < -16000) held |= MV_PAD_UP;
    if (pad->y > 16000) held |= MV_PAD_DOWN;
    return held;
}

static void mv_controller_close(mv_controller_t *controller) {
    if (controller->caps) {
        for (unsigned i = 0; i < 2u; ++i) {
            if (controller->sources[i].lease != T5_PROVIDER_CAPABILITY_LEASE_INVALID)
                (void)controller->caps->release(controller->sources[i].lease);
        }
    }
    memset(controller, 0, sizeof(*controller));
    controller->selected = 2u;
}

static void mv_controller_open(mv_controller_t *controller,
                               const t5_provider_capability_api_v1 *caps) {
    memset(controller, 0, sizeof(*controller));
    controller->selected = 2u;
    if (!caps || caps->api_version != T5_PROVIDER_CAPABILITY_API_VERSION ||
        caps->struct_size < offsetof(t5_provider_capability_api_v1, release) + sizeof(caps->release) ||
        !caps->acquire || !caps->release) return;
    controller->caps = caps;
    static const char *const names[2] = {"usb.hid.gamepad", "usb.xinput.gamepad"};
    for (unsigned i = 0; i < 2u; ++i) {
        const void *interface_ptr = NULL;
        t5_provider_capability_lease_t lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
        const bool acquired = caps->acquire(names[i], RISC_USB_GAMEPAD_API_V1,
                                             &lease, &interface_ptr);
        const risc_usb_gamepad_api_v1 *api = interface_ptr;
        if (acquired && lease != T5_PROVIDER_CAPABILITY_LEASE_INVALID && api &&
            api->api_version == RISC_USB_GAMEPAD_API_V1 &&
            api->struct_size >= offsetof(risc_usb_gamepad_api_v1, snapshot) + sizeof(api->snapshot) &&
            api->poll && api->snapshot) {
            controller->sources[i].api = api;
            controller->sources[i].lease = lease;
        } else if (lease != T5_PROVIDER_CAPABILITY_LEASE_INVALID) {
            (void)caps->release(lease);
        }
    }
}

static void mv_pad_poll(mv_pad_source_t *source, bool xinput) {
    source->held = 0;
    source->connected = false;
    source->snapshot_valid = false;
    const risc_usb_gamepad_api_v1 *api = source->api;
    if (!api) return;
    risc_usb_gamepad_state_v1 states[MV_PAD_DEVICE_LIMIT] = {{0}};
    size_t count = MV_PAD_DEVICE_LIMIT;
    if (!api->poll(api->context, 4u) ||
        !api->snapshot(api->context, states, &count) || count > MV_PAD_DEVICE_LIMIT) {
        source->device = 0;
        source->armed = false;
        return;
    }
    source->snapshot_valid = true;
    const risc_usb_gamepad_state_v1 *chosen = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!states[i].connected || !states[i].device) continue;
        if (!chosen || states[i].device == source->device) chosen = &states[i];
    }
    if (!chosen) {
        source->device = 0;
        source->armed = false;
        return;
    }
    source->connected = true;
    if (chosen->device != source->device) {
        source->device = chosen->device;
        source->armed = false;
    }
    const uint32_t held = mv_pad_decode(chosen, xinput);
    /* A held launch/exit control, reconnect or provider fault must not inject
     * a new gesture. Require one neutral current snapshot to rearm. */
    if (!source->armed) {
        if (!held) source->armed = true;
        return;
    }
    source->held = held;
}

static uint32_t mv_controller_poll(mv_controller_t *controller) {
    for (unsigned i = 0; i < 2u; ++i) mv_pad_poll(&controller->sources[i], i == 1u);
    unsigned selected = controller->selected;
    if (selected >= 2u || !controller->sources[selected].connected) {
        selected = 2u;
        for (unsigned i = 0; i < 2u; ++i)
            if (controller->sources[i].connected) { selected = i; break; }
    }
    /* A second connected pad may take over only when the first is neutral.
     * Never combine one controller's bumper with another controller's D-pad. */
    if (selected < 2u && !controller->sources[selected].held) {
        for (unsigned i = 0; i < 2u; ++i)
            if (controller->sources[i].held) { selected = i; break; }
    }
    controller->selected = selected;
    controller->held = selected < 2u ? controller->sources[selected].held : 0u;
    return controller->held;
}

/* The compatibility poll has no source ID: a mapped direction can be the
 * same gamepad input with its bumper stripped off. Do not apply both paths.
 * Raw ownership includes neutral/rearming/fault snapshots, not only a held
 * direction. After unplug, drain to a neutral mapped snapshot before enabling
 * keyboard/physical-camera fallback. Back/exit is deliberately never masked. */
static uint32_t mv_controller_filter_mapped(mv_controller_t *controller,
                                            uint32_t buttons, uint32_t camera_mask) {
    bool raw_owns_camera = controller->held != 0u;
    for (unsigned i = 0; i < 2u; ++i) {
        const mv_pad_source_t *source = &controller->sources[i];
        if (source->api && (source->connected || !source->snapshot_valid))
            raw_owns_camera = true;
    }
    if (raw_owns_camera) controller->mapped_camera_blocked = true;
    else if (!(buttons & camera_mask)) controller->mapped_camera_blocked = false;
    return controller->mapped_camera_blocked ? buttons & ~camera_mask : buttons;
}

static uint32_t mv_motion_action(uint32_t held) {
    uint32_t action = held & MV_PAD_DIRECTIONS;
    if ((action & (MV_PAD_LEFT | MV_PAD_RIGHT)) == (MV_PAD_LEFT | MV_PAD_RIGHT))
        action &= ~(MV_PAD_LEFT | MV_PAD_RIGHT);
    if ((action & (MV_PAD_UP | MV_PAD_DOWN)) == (MV_PAD_UP | MV_PAD_DOWN))
        action &= ~(MV_PAD_UP | MV_PAD_DOWN);
    if (held & MV_PAD_RB) action = action ? action | MV_PAD_RB : 0u;
    else if (held & MV_PAD_LB) {
        action &= MV_PAD_UP | MV_PAD_DOWN;
        if (action) action |= MV_PAD_LB;
    }
    if (action && (held & MV_PAD_FINE)) action |= MV_PAD_FINE;
    return action;
}

/* A pan/zoom gesture cannot silently become rotation because the bumper was
 * released before the D-pad, or was absent in an intervening raw snapshot.
 * Remember mode admission, NOT old movement: when the bumper is missing,
 * movement pauses. Only an observed 48 ms neutral D-pad + bumpers rearms
 * unmodified rotation. A does not prevent neutral rearming. Explicit LB/RB
 * mode changes remain immediate and RB keeps priority when both are held. */
static uint32_t mv_motion_gesture_action(mv_motion_t *motion, uint32_t held,
                                         uint32_t now) {
    const uint32_t bumper = (held & MV_PAD_RB) ? MV_PAD_RB : (held & MV_PAD_LB);
    if (bumper) {
        motion->bumper_mode = bumper;
        motion->neutral_pending = false;
    } else if (motion->bumper_mode) {
        if (held & MV_PAD_DIRECTIONS) {
            motion->neutral_pending = false;
        } else if (!motion->neutral_pending) {
            motion->neutral_pending = true;
            motion->neutral_since_ms = now;
        } else if ((uint32_t)(now - motion->neutral_since_ms) >= MV_MODE_NEUTRAL_MS) {
            motion->bumper_mode = 0;
            motion->neutral_pending = false;
        }
        return 0u;
    }
    return mv_motion_action(held);
}

/* exp(z) over the full new bound |z| <= .864, evaluated with Horner's
 * method. The old short-range polynomial is inaccurate at 8x zoom speed.
 * This ninth-order form also avoids repeated-squaring roundoff at fine speed
 * and imports no libm/division routine into the native ELF. */
static float mv_zoom_factor(float z) {
    return 1.0f + z * (1.0f + z * (0.5f + z * ((1.0f/6.0f) +
           z * ((1.0f/24.0f) + z * ((1.0f/120.0f) + z * ((1.0f/720.0f) +
           z * ((1.0f/5040.0f) + z * ((1.0f/40320.0f) + z * (1.0f/362880.0f)))))))));
}

/* Observe on every poll, including render checkpoints. Only apply a delta when
 * a new frame can start. Fractional motion is retained in the camera; pending
 * time is bounded so a busy display/long mesh pass cannot produce a catch-up
 * jump. Release/reversal/modifier changes discard pending old-mode movement. */
static bool mv_motion_step(mv_motion_t *motion, uint32_t held, uint32_t now,
                            bool frame_ready, mv_motion_delta_t *delta) {
    *delta = (mv_motion_delta_t){.zoom_factor = 1.0f};
    const uint32_t action = mv_motion_gesture_action(motion, held, now);
    uint32_t elapsed = now - motion->last_ms;
    motion->last_ms = now;
    if (!action && motion->bumper_mode && (held & MV_PAD_DIRECTIONS) &&
        !(held & (MV_PAD_LB | MV_PAD_RB))) {
        /* Pause without replay or a new start ramp if the same chord returns.
         * Never integrate the time spent missing its modifier. */
        motion->pending_ms = 0;
        return false;
    }
    if (!motion->initialized || action != motion->action) {
        /* Precision may be pressed/released mid-gesture. Preserve the start
         * ramp on speed-only changes, but never carry old-speed pending time. */
        if (!motion->initialized ||
            (action & ~MV_PAD_FINE) != (motion->action & ~MV_PAD_FINE))
            motion->ramp_ms = 0;
        motion->initialized = true;
        motion->action = action;
        motion->pending_ms = 0;
        return false;
    }
    if (!action) return false;
    const uint32_t room = MV_MOTION_MAX_FRAME_MS - motion->pending_ms;
    if (elapsed > room) elapsed = room;
    motion->pending_ms += elapsed;
    if (!frame_ready || !motion->pending_ms) return false;
    elapsed = motion->pending_ms;
    motion->pending_ms = 0;
    uint32_t ramp = MV_MOTION_RAMP_MS - motion->ramp_ms;
    if (ramp > elapsed) ramp = elapsed;
    /* Exact integral of a linear 120 ms start ramp followed by constant speed.
     * No deceleration tail: letting go stops at the next current snapshot. */
    const float effective_ms = (float)(2u * motion->ramp_ms + ramp) * (float)ramp *
                              (0.5f / (float)MV_MOTION_RAMP_MS) + (float)(elapsed - ramp);
    motion->ramp_ms += ramp;
    const float speed = (action & MV_PAD_FINE) ? MV_FINE_SPEED_SCALE : 1.0f;
    const float dt = effective_ms * 0.001f * speed;
    const int dx = ((action & MV_PAD_RIGHT) != 0u) - ((action & MV_PAD_LEFT) != 0u);
    const int dy = ((action & MV_PAD_DOWN) != 0u) - ((action & MV_PAD_UP) != 0u);
    if (action & MV_PAD_RB) {
        const float distance = MV_PAN_PIXELS_PER_SEC * dt * (dx && dy ? 0.70710678f : 1.0f);
        delta->pan_x = (float)dx * distance;
        delta->pan_y = (float)dy * distance;
    } else if (action & MV_PAD_LB) {
        const float z = -(float)dy * MV_ZOOM_RATE_PER_SEC * dt;
        delta->zoom_factor = mv_zoom_factor(z);
    } else {
        delta->yaw = (float)dx * MV_ROTATE_RADIANS_PER_SEC * dt;
        delta->pitch = (float)dy * MV_ROTATE_RADIANS_PER_SEC * dt;
    }
    return dt > 0.0f;
}
#endif
