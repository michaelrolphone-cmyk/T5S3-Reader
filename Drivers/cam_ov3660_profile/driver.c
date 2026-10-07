#include "RiscCameraEsp32s3ProfileV1.h"
static const risc_camera_esp32s3_profile_v1 profile = {
    1, sizeof(profile), {11,9,8,10,12,18,17,16}, 13,6,7,15,4,5,4,0x3c,20000000,0x3660
};
static bool start(const risc_provider_dependency_v1 *d, size_t n) { (void)d; return n==0; }
static bool quiesce(void) { return true; }
static void stop(void) {}
static const risc_driver_v2 driver = {2,sizeof(driver),"cam-ov3660-profile",
    RISC_CAMERA_ESP32S3_PROFILE,1,&profile,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi==2?&driver:NULL;
}
