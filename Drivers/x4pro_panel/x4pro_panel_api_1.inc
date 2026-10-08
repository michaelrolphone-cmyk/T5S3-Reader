/* ---------------- display.output provider API ---------------- */
static bool get_info(void *context, risc_display_info_v1 *out) {
    (void)context;
    if (!out) return false;
    bytes_fill((uint8_t *)out, 0, sizeof(*out));
    out->api_version = RISC_DISPLAY_OUTPUT_API_V1;
    out->struct_size = sizeof(*out);
    out->width = X4PRO_PANEL_WIDTH;
    out->height = X4PRO_PANEL_HEIGHT;
    out->supported_formats = RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);
    out->preferred_format = RISC_DISPLAY_FORMAT_MONO1;
    out->supported_rotations = RISC_DISPLAY_ROTATION_0;
    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE |
                 RISC_DISPLAY_INFO_CLEAN_PRESENT | RISC_DISPLAY_INFO_QUIESCE_SLEEP;
    out->damage_x_alignment = 8;
    out->damage_width_alignment = 8;
    out->damage_y_alignment = 1;
    out->damage_height_alignment = 1;
    out->nominal_refresh_millihz = controller == PROBE_UC8279 ? 8000u : 500u;
    out->typical_present_latency_us = controller == PROBE_UC8279 ? 125000u : 1600000u;
    return true;
}
static bool acquire(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!started || shutdown_stage || held || !out || format != RISC_DISPLAY_FORMAT_MONO1) return false;
    if (++frame_serial == 0) ++frame_serial;
    held = true;
    *out = (risc_display_surface_v1){frame_serial, frame, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT,
        ROW_BYTES, FRAME_BYTES, RISC_DISPLAY_FORMAT_MONO1};
    return true;
}
static void release(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return;
    if (held && frame_id == frame_serial) held = false;
}
static bool submit(void *context, risc_display_frame_v1 frame_id, const risc_display_rect_v1 *damage,
                   size_t count, const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *token_out) {
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial ||
        present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE ||
        !token_out || count > RISC_DISPLAY_MAX_DAMAGE_RECTS || (count && !damage)) return false;
    pending_intent = options ? options->intent : RISC_DISPLAY_PRESENT_DEFAULT;
    partial_update = count && previous_seeded && pending_intent != RISC_DISPLAY_PRESENT_CLEAN;
    update_area = full_visible_rect();
    if (count) {
        uint32_t left = X4PRO_PANEL_WIDTH, top = X4PRO_PANEL_HEIGHT, right = 0, bottom = 0;
        for (size_t i = 0; i < count; ++i) {
            const risc_display_rect_v1 *r = &damage[i];
            if (r->x < 0 || r->y < 0 || !r->width || !r->height ||
                r->width > X4PRO_PANEL_WIDTH || r->height > X4PRO_PANEL_HEIGHT ||
                (uint32_t)r->x > X4PRO_PANEL_WIDTH - r->width ||
                (uint32_t)r->y > X4PRO_PANEL_HEIGHT - r->height) return false;
            if ((uint32_t)r->x < left) left = (uint32_t)r->x;
            if ((uint32_t)r->y < top) top = (uint32_t)r->y;
            if ((uint32_t)r->x + r->width > right) right = (uint32_t)r->x + r->width;
            if ((uint32_t)r->y + r->height > bottom) bottom = (uint32_t)r->y + r->height;
        }
        left &= ~7u;
        right = (right + 7u) & ~7u;
        update_area = (risc_display_rect_v1){(int32_t)left, (int32_t)top, right - left, bottom - top};
    }
    if (++token_serial == 0) ++token_serial;
    pending_token = token_serial;
    present_state = PRESENT_QUEUED;
    transfer_started = false;
    *token_out = pending_token;
    return true;
}
static bool present_status(void *context, risc_display_present_token_v1 token,
                           risc_display_present_status_v1 *out) {
    (void)context;
    if (!out || token != pending_token) return false;
    out->state = present_state;
    return true;
}
