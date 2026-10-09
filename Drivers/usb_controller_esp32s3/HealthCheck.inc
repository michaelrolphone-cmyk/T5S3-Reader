/* Included only after BOTH production controller layers. Internal getters
 * below read this same ELF's bounded SDK state; they do not lock, poll, perform
 * MMIO, call a dependency, or change ownership. Healthy armed interrupt DMA
 * keeps an ELF-owned buffer, callback and context, never the caller's dst. */
static int32_t provider_health_check(void) {
    if (health_cleanup_failed || health_unassigned_device || ownedBulk.retained() || ownedControl.retained() ||
        ownedAdmission.retained() || risc_usb_control_faulted() ||
        risc_usb_admission_faulted() || role.state() == UsbRoleSwitch::State::Cleanup)
        return RISC_PROVIDER_HEALTH_RETAINED;
    if (health_bulk_draining && inFlight) return RISC_PROVIDER_HEALTH_RETAINED;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i) {
        const auto &slot = interrupts[i];
        if (health_interrupt_draining[i]) return RISC_PROVIDER_HEALTH_RETAINED;
        if (!slot.dma) {
            if (slot.pending || slot.ready || slot.claim_id) return RISC_PROVIDER_HEALTH_RETAINED;
            continue;
        }
        if (!slot.claim_id || slot.dma->context != &slot ||
            slot.dma->callback != interrupt_complete || (slot.pending && slot.ready))
            return RISC_PROVIDER_HEALTH_RETAINED;
        bool owner = false;
        for (const auto &claim : claims)
            if (claim.token == slot.claim_id && !claim.interfaceReleased) owner = true;
        if (!owner) return RISC_PROVIDER_HEALTH_RETAINED;
    }
    if (ownedBulk.owns_storage() || ownedControl.owns_storage() ||
        ownedAdmission.owns_storage() || risc_usb_control_busy() ||
        risc_usb_admission_owns_resources()) return RISC_PROVIDER_HEALTH_BUSY;
    /* Legacy control/bulk calls are synchronous at this ABI. A submitted
     * transfer remaining at a transition boundary is an uncertain timeout. */
    if (inFlight) return RISC_PROVIDER_HEALTH_RETAINED;
    if (role.state() == UsbRoleSwitch::State::Off &&
        (running || installed || phy || phyRouteCaptured || client || transfer ||
         powerLease || nativePhyLease.held())) {
        /* quiesce() parks the role before refusing tracked live claims. That
         * refusal has not attempted their release or lost native custody. */
        bool live_claim = false;
        for (const auto &claim : claims)
            if (claim.token && !claim.interfaceReleased) live_claim = true;
        if (live_claim && running && installed && phy && client && transfer &&
            powerLease && nativePhyLease.held()) return RISC_PROVIDER_HEALTH_BUSY;
        return RISC_PROVIDER_HEALTH_RETAINED;
    }
    for (const auto &claim : claims)
        if (claim.token && claim.interfaceReleased) return RISC_PROVIDER_HEALTH_RETAINED;
    if (fault || role.state() == UsbRoleSwitch::State::Failed)
        return RISC_PROVIDER_HEALTH_UNKNOWN;
    if (role.state() == UsbRoleSwitch::State::Host &&
        (!running || !installed || !phy || !client || !transfer || !powerLease ||
         !nativePhyLease.held())) return RISC_PROVIDER_HEALTH_UNKNOWN;
    return RISC_PROVIDER_HEALTH_READY;
}
