static bool uc_present(uint64_t deadline_ms) {
    const uint64_t now = now_ms();
    if (now == UINT64_MAX) return false;
    uint8_t profile = uc_select_profile(now);
    risc_display_rect_v1 region = partial_update ? update_area : full_visible_rect();
    if (profile == UC_PROFILE_ABS_SETTLE_2F && uc_abs_dirty_valid)
        region = rect_union(region, uc_abs_dirty);
    uc_last_profile = profile;
    bool ok = false;
    if (profile == UC_PROFILE_OTP_CLEAN) {
        if (uc_state == UC_STATE_FAULTED || !uc_spi_ready) {
            x4pro_epd_reset_unhold();
            x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
            if (!uc_reset_and_init()) return false;
        }
        ok = uc_otp_clean(frame, deadline_ms);
    } else if (profile == UC_PROFILE_DIFF_2F || profile == UC_PROFILE_DIFF_1F) {
        const uint8_t frames = profile == UC_PROFILE_DIFF_2F ? 2u : 1u;
        if (uc_state != UC_STATE_DIFF_SYNCED) { set_reason("differential state invalid"); return false; }
        ok = uc_write_window_plane(0x10, previous_frame, &region, deadline_ms) &&
             uc_write_window_plane(0x13, frame, &region, deadline_ms) &&
             uc_external_refresh(&region, frames, false, deadline_ms) &&
             uc_write_window_plane(0x10, frame, &region, deadline_ms);
        if (ok) uc_state = UC_STATE_DIFF_SYNCED;
    } else if (profile == UC_PROFILE_ABS_1F) {
        if (uc_state != UC_STATE_ABS_BURST) {
            uc_burst_started_ms = now;
            uc_burst_frames = 0;
        }
        ok = uc_write_window_plane(0x13, frame, &region, deadline_ms) &&
             uc_external_refresh(&region, 1u, true, deadline_ms);
        if (ok) {
            uc_state = UC_STATE_ABS_BURST;
            ++uc_burst_frames;
            ++uc_absolute_updates;
            uc_record_abs_dirty(region);
        }
    } else {
        ok = uc_write_window_plane(0x13, frame, &region, deadline_ms) &&
             uc_external_refresh(&region, 2u, true, deadline_ms) &&
             uc_write_window_plane(0x10, frame, &region, deadline_ms);
        if (ok) {
            uc_state = UC_STATE_DIFF_SYNCED;
            uc_abs_dirty_valid = false;
            uc_burst_frames = 0;
        }
    }
    if (!ok) {
        uc_mark_fault(reason[0] == 'n' ? "uc present failed" : reason);
        return false;
    }
    copy_rect(previous_frame, frame, &region);
    previous_seeded = true;
    ++uc_fast_updates;
    uc_last_present_ms = now_ms();
    return true;
}

