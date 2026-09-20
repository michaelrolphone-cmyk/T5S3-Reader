// The actual direct hardware symbol inventory is linked only on the device.
// Host tests exercise the launcher lifetime, not Arduino hardware addresses.
int native_hardware_compat_register(void) { return 0; }
void native_hardware_compat_unregister(void) {}
