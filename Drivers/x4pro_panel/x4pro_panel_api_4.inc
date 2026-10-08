static bool append(char *destination, size_t capacity, size_t *used, const char *text) {
    while (*text && *used + 1u < capacity) destination[(*used)++] = *text++;
    destination[*used] = 0;
    return *text == 0;
}
static bool append_u(char *destination, size_t capacity, size_t *used, uint64_t value) {
    char digits[20];
    size_t n = 0;
    do { digits[n++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && n < sizeof(digits));
    while (n && *used + 1u < capacity) destination[(*used)++] = digits[--n];
    destination[*used] = 0;
    return n == 0;
}
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity) return false;
    size_t used = 0;
    destination[0] = 0;
    append(destination, capacity, &used, probe_text);
    append(destination, capacity, &used, " v=0.2.0 token="); append_u(destination, capacity, &used, pending_token);
    append(destination, capacity, &used, " state="); append_u(destination, capacity, &used, present_state);
    append(destination, capacity, &used, " reason="); append(destination, capacity, &used, reason);
    append(destination, capacity, &used, " profile="); append_u(destination, capacity, &used, uc_last_profile);
    append(destination, capacity, &used, " ucstate="); append_u(destination, capacity, &used, uc_state);
    append(destination, capacity, &used, " fastoff="); append_u(destination, capacity, &used, uc_fast_disabled ? 1u : 0u);
    append(destination, capacity, &used, " faults="); append_u(destination, capacity, &used, uc_faults);
    append(destination, capacity, &used, " xfer="); append_u(destination, capacity, &used, bytes_sent);
    append(destination, capacity, &used, " busy=");
    append_u(destination, capacity, &used, busy_done_ms >= busy_assert_ms ? busy_done_ms - busy_assert_ms : 0u);
    return used > 0;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-panel",
      "display.output", 1u, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.base : 0;
}
