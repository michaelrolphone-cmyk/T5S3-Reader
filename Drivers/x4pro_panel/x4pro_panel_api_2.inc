static bool wait_present(void *context, risc_display_present_token_v1 token, uint32_t timeout_ms,
                         risc_display_present_status_v1 *out) {
    if (token != pending_token) return false;
    if (present_state == PRESENT_QUEUED && timeout_ms > 0 && !transfer_started) {
        uint64_t now = 0;
        wait_budget_ms = timeout_ms;
        bytes_sent = 0;
        refresh_ms = busy_assert_ms = busy_done_ms = transfer_start_ms = transfer_end_ms = 0;
        reason = "none";
        if (!sample_now(&now) || timeout_ms > UINT64_MAX - now) {
            set_reason("invalid wait budget");
            present_state = PRESENT_FAILED;
            held = false;
            return present_status(context, token, out);
        }
        wait_start_ms = transfer_yielded_ms = now;
        transfer_work = 0;
        transfer_start_ms = now;
        present_state = PRESENT_ACTIVE;
        transfer_started = true;
        const bool ok = controller == PROBE_UC8279 ? uc_present(now + timeout_ms)
                                                    : ssd_transfer_frame(now + timeout_ms);
        transfer_end_ms = now_ms();
        if (!ok) {
            present_state = PRESENT_FAILED;
            held = false;
        } else {
            present_state = PRESENT_COMPLETE;
            reason = "complete";
            held = false;
        }
    }
    return present_status(context, token, out);
}
static bool set_brightness(void *context, uint16_t level, uint16_t maximum) {
    (void)context; (void)level; (void)maximum;
    return false;
}
static bool seed_previous(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial ||
        present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX) return false;
    for (size_t i = 0; i < FRAME_BYTES; ++i) {
        previous_frame[i] = frame[i];
        if ((i & 4095u) == 4095u) {
            const uint64_t now = now_ms();
            if (now == UINT64_MAX || now < began || now - began >= 100u) return false;
            sleep_ms(1);
        }
    }
    previous_seeded = true;
    if (controller == PROBE_UC8279) uc_state = UC_STATE_UNKNOWN;
    return true;
}
static const risc_display_output_api_v1_history api = {
    { RISC_DISPLAY_OUTPUT_API_V1, sizeof(api), 0, get_info, acquire, release, submit,
      present_status, wait_present, set_brightness },
    RISC_DISPLAY_HISTORY_TAG, 1u, seed_previous
};
