static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    shutdown_stage = 0;
    previous_seeded = partial_update = false;
    present_state = PRESENT_NONE;
    held = false;
    clock_api = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1u)
            clock_api = (const risc_platform_clock_api_v1 *)dependencies[i].api;
    if (!clock_api) { fail("platform.clock missing"); return false; }
    prepare_pins();
    controller = probe_controller();
    if (controller != PROBE_SSD && controller != PROBE_UC8279) {
        set_reason(controller == PROBE_DISABLED ? "probe-disabled" : "ambiguous-controller");
        return false;
    }
    bytes_fill(frame, 0, sizeof(frame));
    bytes_fill(previous_frame, 0, sizeof(previous_frame));
    uc_state = UC_STATE_UNPROBED;
    uc_last_profile = UC_PROFILE_OTP_CLEAN;
    uc_faults = 0;
    uc_powered = false;
    uc_fast_disabled = false;
    uc_fast_updates = uc_absolute_updates = uc_burst_frames = 0;
    uc_abs_dirty_valid = false;
    uc_last_present_ms = 0;
    started = controller == PROBE_UC8279 ? uc_reset_and_init() : ssd_init_panel();
    return started;
}
static void stop(void) {
    started = false;
    held = false;
    if (controller == PROBE_UC8279) uc_spi_release();
}
static bool quiesce(void) {
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    if (!pins_ready || shutdown_stage == 3u) { started = false; return true; }
    if (!clock_api || (controller != PROBE_SSD && controller != PROBE_UC8279)) return false;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX) return false;
    const uint64_t deadline = began + 4000u;
    if (controller == PROBE_UC8279 && uc_state == UC_STATE_ABS_BURST) {
        if (!uc_settle_if_needed(deadline)) {
            uc_mark_fault("quiesce settle failed");
            return false;
        }
    }
    if (shutdown_stage == 0u) {
        const bool busy = controller == PROBE_UC8279 ? !x4pro_pin_read(X4PRO_PIN_EPD_BUSY)
                                                     : x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
        if (busy) return false;
        if (controller == PROBE_UC8279) {
            if (!uc_command(0x02)) return false;
            uc_powered = false;
        } else {
            bb_command(0x3c); bb_data1(0x80); bb_command(0x22); bb_data1(0x03); bb_command(0x20);
        }
        shutdown_stage = 1u;
        sleep_ms(controller == PROBE_UC8279 ? 1u : 200u);
    }
    if (shutdown_stage == 1u) {
        while (now_ms() < deadline) {
            const bool busy = controller == PROBE_UC8279 ? !x4pro_pin_read(X4PRO_PIN_EPD_BUSY)
                                                         : x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
            if (!busy) { shutdown_stage = 2u; break; }
            sleep_ms(10);
        }
        if (shutdown_stage != 2u) return false;
    }
    if (controller == PROBE_UC8279) {
        if (!uc_command(0x07) || !uc_data1(0xa5)) return false;
        uc_state = UC_STATE_SLEEPING;
        uc_spi_release();
    } else {
        bb_command(0x10); bb_data1(0x03);
    }
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_pin_hold(X4PRO_PIN_EPD_RST, true);
    shutdown_stage = 3u;
    started = false;
    return true;
}
