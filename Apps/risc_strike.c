#include "T5ProviderCapabilityApi.h"
#include "RiscUsbHidV1.h"

/* Keep the renderer/gameplay core separate from the controller-facing shell.
 * The core's original entry points are retained as hidden implementation
 * details so this file can bind the full XInput capability without changing
 * the renderer. */
#define app_main fps_legacy_app_main
#define app_hardware_takeover fps_legacy_hardware_takeover
#define visibility(value) visibility("hidden")
#include "risc_strike_engine.inc"
#undef visibility
#undef app_hardware_takeover
#undef app_main

#define FPS_XINPUT_CAPABILITY "usb.xinput.gamepad"
#define FPS_XINPUT_MAX_GAMEPADS 4u
#define FPS_XINPUT_POLL_BUDGET 8u
#define FPS_XINPUT_BUTTON_LB (1u << 4)
#define FPS_XINPUT_BUTTON_RB (1u << 5)
#define FPS_XINPUT_BUTTON_SELECT (1u << 8)
#define FPS_XINPUT_BUTTON_START (1u << 9)

typedef struct {
    bool connected;
    uint32_t buttons;
    uint32_t navigation;
} fps_controller_state_t;

static const t5_provider_capability_api_v1 *g_capability_api;
static const risc_usb_gamepad_api_v1 *g_xinput_api;
static t5_provider_capability_lease_t g_xinput_lease =
    T5_PROVIDER_CAPABILITY_LEASE_INVALID;
static bool g_paused;
static uint32_t g_pause_started_ms;

static bool fps_capability_api_has(size_t offset, size_t member_size) {
    return g_capability_api &&
           g_capability_api->struct_size >= offset + member_size;
}

static bool fps_gamepad_api_has(size_t offset, size_t member_size) {
    return g_xinput_api &&
           g_xinput_api->struct_size >= offset + member_size;
}

static void fps_release_xinput(void) {
    if (g_capability_api &&
        fps_capability_api_has(offsetof(t5_provider_capability_api_v1, release),
                               sizeof(g_capability_api->release)) &&
        g_capability_api->release &&
        g_xinput_lease != T5_PROVIDER_CAPABILITY_LEASE_INVALID) {
        (void)g_capability_api->release(g_xinput_lease);
    }
    g_xinput_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
    g_xinput_api = NULL;
    g_capability_api = NULL;
}

static void fps_acquire_xinput(void) {
    g_capability_api =
        t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!g_capability_api ||
        g_capability_api->api_version != T5_PROVIDER_CAPABILITY_API_VERSION ||
        !fps_capability_api_has(offsetof(t5_provider_capability_api_v1, acquire),
                                sizeof(g_capability_api->acquire)) ||
        !g_capability_api->acquire) {
        g_capability_api = NULL;
        return;
    }

    const void *interface_ptr = NULL;
    t5_provider_capability_lease_t lease =
        T5_PROVIDER_CAPABILITY_LEASE_INVALID;
    if (!g_capability_api->acquire(FPS_XINPUT_CAPABILITY,
                                   RISC_USB_GAMEPAD_API_V1,
                                   &lease,
                                   &interface_ptr) ||
        lease == T5_PROVIDER_CAPABILITY_LEASE_INVALID || !interface_ptr) {
        return;
    }

    g_xinput_lease = lease;
    g_xinput_api = (const risc_usb_gamepad_api_v1 *)interface_ptr;
    if (g_xinput_api->api_version != RISC_USB_GAMEPAD_API_V1 ||
        !fps_gamepad_api_has(offsetof(risc_usb_gamepad_api_v1, snapshot),
                             sizeof(g_xinput_api->snapshot)) ||
        !g_xinput_api->poll || !g_xinput_api->snapshot) {
        fps_release_xinput();
    }
}

static uint32_t fps_hat_navigation(uint8_t hat) {
    switch (hat) {
        case 0:
            return T5_APP_BUTTON_UP;
        case 1:
            return T5_APP_BUTTON_UP | T5_APP_BUTTON_RIGHT;
        case 2:
            return T5_APP_BUTTON_RIGHT;
        case 3:
            return T5_APP_BUTTON_DOWN | T5_APP_BUTTON_RIGHT;
        case 4:
            return T5_APP_BUTTON_DOWN;
        case 5:
            return T5_APP_BUTTON_DOWN | T5_APP_BUTTON_LEFT;
        case 6:
            return T5_APP_BUTTON_LEFT;
        case 7:
            return T5_APP_BUTTON_UP | T5_APP_BUTTON_LEFT;
        default:
            return 0u;
    }
}

static void fps_poll_controller(fps_controller_state_t *controller) {
    if (!controller) return;
    memset(controller, 0, sizeof(*controller));
    if (!g_xinput_api ||
        !g_xinput_api->poll(g_xinput_api->context, FPS_XINPUT_POLL_BUDGET)) {
        return;
    }

    risc_usb_gamepad_state_v1 states[FPS_XINPUT_MAX_GAMEPADS];
    size_t count = FPS_XINPUT_MAX_GAMEPADS;
    memset(states, 0, sizeof(states));
    if (!g_xinput_api->snapshot(g_xinput_api->context, states, &count)) return;
    if (count > FPS_XINPUT_MAX_GAMEPADS) count = FPS_XINPUT_MAX_GAMEPADS;

    for (size_t i = 0; i < count; ++i) {
        if (!states[i].connected) continue;
        controller->connected = true;
        controller->buttons |= states[i].buttons;
        controller->navigation |= fps_hat_navigation(states[i].hat);
    }
}

static void fps_shift_deadline(uint32_t *deadline, uint32_t delta_ms) {
    if (deadline && *deadline != 0u) *deadline += delta_ms;
}

static void fps_pause(bool paused, uint32_t now) {
    if (paused == g_paused) return;
    if (paused) {
        g_paused = true;
        g_pause_started_ms = now;
        return;
    }

    const uint32_t paused_ms = now - g_pause_started_ms;
    fps_shift_deadline(&g_next_fire_ms, paused_ms);
    fps_shift_deadline(&g_muzzle_until_ms, paused_ms);
    fps_shift_deadline(&g_damage_until_ms, paused_ms);
    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) {
        fps_shift_deadline(&g_enemies[i].next_attack_ms, paused_ms);
        fps_shift_deadline(&g_enemies[i].hit_until_ms, paused_ms);
    }
    g_paused = false;
    g_pause_started_ms = 0u;
}

static void fps_clear_rect(uint8_t *buffer, int x, int y,
                           int width, int height) {
    if (!buffer || width <= 0 || height <= 0) return;
    const int x0 = fps_max_i(x, 0);
    const int x1 = fps_min_i(x + width - 1, FPS_LOGICAL_W - 1);
    const int y0 = fps_max_i(y, 0);
    const int y1 = fps_min_i(y + height - 1, FPS_LOGICAL_H - 1);
    for (int px = x0; px <= x1; ++px)
        fps_clear_vspan(buffer, px, y0, y1);
}

static void fps_draw_controller_title(uint8_t *buffer) {
    fps_frame(buffer, 20, 20, 920, 500);
    fps_frame(buffer, 26, 26, 908, 488);
    fps_text(buffer, 265, 64, "RISC STRIKE", 6);
    fps_hline(buffer, 90, 870, 128);
    fps_text(buffer, 291, 148, "3D FIRST PERSON SHOOTER", 2);
    /* A centered corridor vignette echoes the actual flat-shaded world. */
    fps_frame(buffer, 292, 206, 376, 166);
    fps_line(buffer, 292, 206, 402, 248);
    fps_line(buffer, 668, 206, 558, 248);
    fps_line(buffer, 292, 371, 402, 330);
    fps_line(buffer, 668, 371, 558, 330);
    fps_frame(buffer, 402, 248, 156, 82);
    fps_rect(buffer, 466, 266, 28, 64);
    fps_text(buffer, 106, 408, "START  BEGIN", 2);
    fps_text(buffer, 388, 408, "D-PAD  MOVE / TURN", 2);
    fps_text(buffer, 106, 448, "LB  STRAFE", 2);
    fps_text(buffer, 388, 448, "RB  FIRE", 2);
    fps_text(buffer, 106, 485, "START  PAUSE     SELECT  EXIT", 1);
    fps_text(buffer, 817, 485, "V1.0.1", 1);
}

static void fps_draw_controller_hud(uint8_t *buffer, uint32_t now) {
    char line[72];
    fps_frame(buffer, 8, 468, FPS_LOGICAL_W - 16, 64);
    fps_text(buffer, 22, 480, "RISC STRIKE", 2);
    snprintf(line, sizeof(line), "HEALTH %d", g_health);
    fps_text(buffer, 290, 480, line, 2);
    snprintf(line, sizeof(line), "TARGETS %d  SCORE %lu",
             fps_alive_count(), (unsigned long)g_score);
    fps_text(buffer, 548, 480, line, 2);
    fps_hline(buffer, 18, 940, 508);
    fps_text(buffer, 22, 515,
             "RB FIRE     LB + D-PAD STRAFE     START PAUSE     SELECT EXIT", 1);

    if (!fps_time_reached(now, g_damage_until_ms)) {
        fps_frame(buffer, 2, FPS_VIEW_TOP + 2, FPS_LOGICAL_W - 4,
                  FPS_VIEW_BOTTOM - FPS_VIEW_TOP - 4);
        fps_frame(buffer, 6, FPS_VIEW_TOP + 6, FPS_LOGICAL_W - 12,
                  FPS_VIEW_BOTTOM - FPS_VIEW_TOP - 12);
    }
}

static void fps_draw_pause_overlay(uint8_t *buffer) {
    const int x = 290;
    const int y = 150;
    const int width = 380;
    const int height = 220;
    fps_clear_rect(buffer, x, y, width, height);
    fps_frame(buffer, x, y, width, height);
    fps_frame(buffer, x + 5, y + 5, width - 10, height - 10);
    fps_text(buffer, 387, y + 38, "PAUSED", 5);
    fps_text(buffer, 376, y + 126, "START  RESUME", 2);
    fps_text(buffer, 388, y + 166, "SELECT  EXIT", 2);
}

static void fps_draw_controller_end(uint8_t *buffer, bool won) {
    fps_frame(buffer, 40, 36, FPS_LOGICAL_W - 80, 468);
    fps_frame(buffer, 46, 42, FPS_LOGICAL_W - 92, 456);
    fps_text(buffer, won ? 283 : 330, 102,
             won ? "LEVEL CLEAR" : "GAME OVER", 5);
    char line[48];
    snprintf(line, sizeof(line), "SCORE %lu", (unsigned long)g_score);
    fps_text(buffer, 370, 210, line, 3);
    fps_text(buffer, 270, 300, "START  PLAY AGAIN", 3);
    fps_text(buffer, 360, 360, "SELECT  EXIT", 2);
    fps_text(buffer, 280, 440,
             won ? "ALL TARGETS ELIMINATED" : "THE TARGETS GOT YOU", 2);
}

static bool fps_render_controller(uint32_t now) {
    if (!g_video->can_submit()) return false;
    size_t bytes = 0;
    uint8_t *buffer = g_video->backbuffer(&bytes);
    const size_t required =
        (size_t)g_surface.stride_bytes * g_surface.height;
    if (!buffer || bytes < required) return false;
    memset(buffer, 0x00, bytes);

    if (g_state == FPS_STATE_TITLE) {
        fps_draw_controller_title(buffer);
    } else if (g_state == FPS_STATE_PLAYING) {
        const uint32_t render_now = g_paused ? g_pause_started_ms : now;
        fps_draw_world(buffer, render_now);
        fps_draw_controller_hud(buffer, render_now);
        if (g_paused) fps_draw_pause_overlay(buffer);
    } else {
        fps_draw_controller_end(buffer, g_state == FPS_STATE_WON);
    }
    return g_video->submit(0, g_surface.height);
}

static void fps_show_controller_video_error(void) {
    if (g_surface.width != 960u || g_surface.height != 540u ||
        g_surface.stride_bytes < 240u ||
        g_surface.pixel_format != T5_VIDEO_PIXEL_GRAY_2BPP_MSB) {
        return;
    }
    size_t bytes = 0;
    uint8_t *buffer = g_video->backbuffer(&bytes);
    if (!buffer ||
        bytes < (size_t)g_surface.stride_bytes * g_surface.height) {
        return;
    }
    memset(buffer, 0x00, bytes);
    fps_text(buffer, 280, 150, "RISC STRIKE", 4);
    fps_text(buffer, 245, 260, "UNSUPPORTED VIDEO SURFACE", 2);
    fps_text(buffer, 370, 340, "SELECT  EXIT", 2);
    if (g_video->can_submit()) (void)g_video->submit(0, g_surface.height);
}

static bool fps_update_controller_input(
    uint32_t generic_buttons,
    uint32_t generic_down,
    const fps_controller_state_t *controller,
    uint32_t controller_down,
    uint32_t now,
    float dt) {
    const bool xinput = controller && controller->connected;
    uint32_t buttons = generic_buttons;
    uint32_t down = generic_down;

    /* The navigation bridge may also translate Xbox A/B into Confirm/Back.
     * Once the full XInput state is available, suppress those aliases so the
     * face buttons remain unassigned and cannot fire or exit accidentally. */
    if (xinput) {
        buttons &= ~(T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM);
        down &= ~(T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM);
        buttons |= controller->navigation;
    }

    const bool select_pressed =
        xinput && (controller_down & FPS_XINPUT_BUTTON_SELECT) != 0u;
    const bool start_pressed =
        xinput && (controller_down & FPS_XINPUT_BUTTON_START) != 0u;
    const bool fallback_confirm =
        !xinput && (down & T5_APP_BUTTON_CONFIRM) != 0u;

    const bool has_horizontal =
        (buttons & (T5_APP_BUTTON_LEFT | T5_APP_BUTTON_RIGHT)) != 0u;
    const bool fallback_strafe =
        !xinput && has_horizontal &&
        (buttons & T5_APP_BUTTON_BACK) != 0u;
    const bool fallback_exit =
        !xinput && !fallback_strafe &&
        (down & T5_APP_BUTTON_BACK) != 0u;

    if (select_pressed || fallback_exit) return false;

    if (g_state == FPS_STATE_TITLE || g_state == FPS_STATE_WON ||
        g_state == FPS_STATE_LOST) {
        if (start_pressed || fallback_confirm) {
            fps_reset_game();
            g_paused = false;
            g_pause_started_ms = 0u;
        }
        return true;
    }

    if (start_pressed) {
        fps_pause(!g_paused, now);
        return true;
    }
    if (g_paused) return true;

    const float forward =
        ((buttons & T5_APP_BUTTON_UP) ? 1.0f : 0.0f) -
        ((buttons & T5_APP_BUTTON_DOWN) ? 1.0f : 0.0f);
    const float horizontal =
        ((buttons & T5_APP_BUTTON_RIGHT) ? 1.0f : 0.0f) -
        ((buttons & T5_APP_BUTTON_LEFT) ? 1.0f : 0.0f);
    const bool strafe = xinput
        ? (controller->buttons & FPS_XINPUT_BUTTON_LB) != 0u
        : fallback_strafe;

    if (strafe) {
        const float side_speed = horizontal * 1.45f * dt;
        fps_try_move(-fps_sin(g_player_angle) * side_speed,
                     fps_cos(g_player_angle) * side_speed);
    } else {
        g_player_angle =
            fps_wrap_angle(g_player_angle + horizontal * 2.2f * dt);
    }

    if (forward != 0.0f) {
        const float move = forward * 1.8f * dt;
        fps_try_move(fps_cos(g_player_angle) * move,
                     fps_sin(g_player_angle) * move);
    }

    const bool fire_pressed = xinput
        ? (controller_down & FPS_XINPUT_BUTTON_RB) != 0u
        : fallback_confirm;
    if (fire_pressed) fps_fire(now);
    return true;
}

__attribute__((visibility("default"))) uint32_t app_hardware_takeover(void) {
    return T5_HARDWARE_TAKEOVER_DISPLAY;
}

__attribute__((visibility("default"))) void app_main(void) {
    g_app = t5_app_get_api(T5_APP_ABI_VERSION);
    g_video = t5_video_get_api(T5_VIDEO_API_VERSION);
    if (!g_app || !g_video || g_video->struct_size < sizeof(*g_video) ||
        !g_app->poll || !g_app->millis || !g_video->start_format ||
        !g_video->backbuffer || !g_video->can_submit ||
        !g_video->submit || !g_video->stop) {
        return;
    }

    const bool can_configure_back =
        fps_api_has(offsetof(t5_app_api_v1, set_back_exits_app),
                    sizeof(g_app->set_back_exits_app)) &&
        g_app->set_back_exits_app;
    if (can_configure_back) g_app->set_back_exits_app(false);
    fps_acquire_xinput();

    bool video_started = false;
    memset(&g_surface, 0, sizeof(g_surface));
    if (!g_video->start_format(&g_surface, T5_VIDEO_PIXEL_GRAY_2BPP_MSB)) goto cleanup;
    video_started = true;

    if (g_surface.width != 960u || g_surface.height != 540u ||
        g_surface.stride_bytes != 240u ||
        g_surface.pixel_format != T5_VIDEO_PIXEL_GRAY_2BPP_MSB ||
        (g_surface.flags & T5_VIDEO_FLAG_ONE_IS_BLACK) == 0u) {
        fps_show_controller_video_error();
        const uint32_t deadline = g_app->millis() + 2500u;
        uint32_t previous_controller_buttons = 0u;
        while (!fps_time_reached(g_app->millis(), deadline)) {
            t5_app_input_t input = {0};
            if (!g_app->poll(&input, 20u) || input.exit_requested) break;
            fps_controller_state_t controller;
            fps_poll_controller(&controller);
            const uint32_t controller_down =
                controller.buttons & ~previous_controller_buttons;
            previous_controller_buttons =
                controller.connected ? controller.buttons : 0u;
            if ((controller_down & FPS_XINPUT_BUTTON_SELECT) != 0u ||
                (!controller.connected &&
                 (input.buttons & T5_APP_BUTTON_BACK) != 0u)) {
                break;
            }
        }
        goto cleanup;
    }

    fps_log(g_xinput_api
        ? "Risc Strike 1.0.1 started with XInput controls"
        : "Risc Strike 1.0.1 started without XInput provider");
    g_state = FPS_STATE_TITLE;
    g_prev_buttons = 0u;
    g_back_hold_start_ms = 0u;
    g_back_holding = false;
    g_paused = false;
    g_pause_started_ms = 0u;
    g_last_frame_ms = g_app->millis();
    (void)fps_render_controller(g_last_frame_ms);

    bool running = true;
    uint32_t queued_down = 0u;
    uint32_t previous_controller_buttons = 0u;
    while (running) {
        t5_app_input_t input = {0};
        if (!g_app->poll(&input, 4u) || input.exit_requested) break;

        const uint32_t now = g_app->millis();
        const uint32_t buttons = input.buttons;
        queued_down |= buttons & ~g_prev_buttons;
        g_prev_buttons = buttons;

        const uint32_t elapsed_ms = now - g_last_frame_ms;
        if (elapsed_ms < FPS_TARGET_FRAME_MS) continue;

        fps_controller_state_t controller;
        fps_poll_controller(&controller);
        const uint32_t controller_down =
            controller.buttons & ~previous_controller_buttons;
        previous_controller_buttons =
            controller.connected ? controller.buttons : 0u;

        const float dt =
            fps_clampf((float)elapsed_ms * 0.001f, 0.0f, 0.075f);
        const uint32_t frame_down = queued_down;
        queued_down = 0u;
        g_last_frame_ms = now;

        running = fps_update_controller_input(buttons, frame_down,
                                              &controller, controller_down,
                                              now, dt);
        if (!running) break;
        if (g_state == FPS_STATE_PLAYING && !g_paused)
            fps_update_enemies(dt, now);
        (void)fps_render_controller(now);
    }

    fps_log("Risc Strike stopped");

cleanup:
    if (video_started) g_video->stop();
    fps_release_xinput();
    if (can_configure_back) g_app->set_back_exits_app(true);
}
