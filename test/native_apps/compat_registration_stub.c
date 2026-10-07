// The actual direct hardware symbol inventory is linked only on the device.
// Host tests exercise the launcher lifetime, not Arduino hardware addresses.
int native_hardware_compat_register(void) { return 0; }
void native_hardware_compat_unregister(void) {}

int test_compat_storage_uncertain = 0;
void native_hardware_compat_storage_uncertain(void) { ++test_compat_storage_uncertain; }
