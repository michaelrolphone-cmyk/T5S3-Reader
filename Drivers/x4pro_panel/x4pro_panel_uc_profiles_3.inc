static bool uc_otp_clean(const uint8_t *target, uint64_t deadline_ms) {
    const uint8_t psr[] = {0x17, 0x4d};
    if (!uc_register1(0x30, 0x0e) ||
        !uc_write_full600_plane(0x10, target, true, deadline_ms) ||
        !uc_write_full600_plane(0x13, target, false, deadline_ms) ||
        !uc_register1(0x50, 0x97) || !uc_register1(0xe0, 0x02) ||
        !uc_register1(0xe5, 0x1e) || !uc_power_on(deadline_ms) ||
        !uc_register(0x00, psr, sizeof(psr))) return false;
    refresh_ms = now_ms();
    if (refresh_ms == UINT64_MAX || !uc_wait_refresh(deadline_ms, 100u)) return false;
    const risc_display_rect_v1 full = full_visible_rect();
    if (!uc_write_window_plane(0x10, target, &full, deadline_ms)) return false;
    uc_state = UC_STATE_DIFF_SYNCED;
    uc_abs_dirty_valid = false;
    uc_burst_frames = 0;
    uc_fast_updates = 0;
    uc_absolute_updates = 0;
    return true;
}
static void uc_mark_fault(const char *why) {
    set_reason(why);
    uc_state = UC_STATE_FAULTED;
    if (uc_faults < 255u) ++uc_faults;
    if (uc_faults >= 2u) uc_fast_disabled = true;
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    uc_powered = false;
}
static void uc_record_abs_dirty(risc_display_rect_v1 r) {
    uc_abs_dirty = uc_abs_dirty_valid ? rect_union(uc_abs_dirty, r) : r;
    uc_abs_dirty_valid = true;
}
static bool uc_settle_source(const uint8_t *source, risc_display_rect_v1 r, uint64_t deadline_ms) {
    if (!uc_write_window_plane(0x13, source, &r, deadline_ms) ||
        !uc_external_refresh(&r, 2u, true, deadline_ms) ||
        !uc_write_window_plane(0x10, source, &r, deadline_ms)) return false;
    uc_state = UC_STATE_DIFF_SYNCED;
    uc_abs_dirty_valid = false;
    uc_burst_frames = 0;
    return true;
}
static bool uc_settle_if_needed(uint64_t deadline_ms) {
    if (uc_state != UC_STATE_ABS_BURST || !uc_abs_dirty_valid) return true;
    return uc_settle_source(previous_frame, uc_abs_dirty, deadline_ms);
}
static uint8_t uc_select_profile(uint64_t now) {
    if (pending_intent == RISC_DISPLAY_PRESENT_CLEAN || !previous_seeded ||
        uc_state == UC_STATE_UNKNOWN || uc_state == UC_STATE_FAULTED)
        return UC_PROFILE_OTP_CLEAN;
    if (uc_fast_disabled || uc_fast_updates >= UC_FAST_BEFORE_CLEAN ||
        uc_absolute_updates >= UC_ABS_BEFORE_CLEAN)
        return UC_PROFILE_OTP_CLEAN;
    if (uc_state == UC_STATE_ABS_BURST) {
        const bool burst_ok = now >= uc_burst_started_ms && now - uc_burst_started_ms <= UC_MAX_ABS_MS &&
                              uc_burst_frames < UC_MAX_ABS_FRAMES &&
                              now >= uc_last_present_ms && now - uc_last_present_ms <= UC_BURST_GAP_MS;
        return burst_ok && pending_intent != RISC_DISPLAY_PRESENT_QUALITY
                   ? UC_PROFILE_ABS_1F : UC_PROFILE_ABS_SETTLE_2F;
    }
    if (pending_intent == RISC_DISPLAY_PRESENT_QUALITY) return UC_PROFILE_DIFF_2F;
    const bool rapid = uc_last_present_ms && now >= uc_last_present_ms &&
                       now - uc_last_present_ms <= UC_BURST_GAP_MS;
    if (rapid && pending_intent != RISC_DISPLAY_PRESENT_CLEAN) return UC_PROFILE_ABS_1F;
    return UC_PROFILE_DIFF_1F;
}
