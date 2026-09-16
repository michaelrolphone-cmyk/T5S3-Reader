#include <T5DeviceApi.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>

_Static_assert(offsetof(t5_device_api_v2, v1) == 0, "v1 table must remain v2 prefix");
_Static_assert(offsetof(t5_device_api_v2, acquire) == sizeof(t5_device_api_v1),
               "v2 only appends methods");
_Static_assert(sizeof(t5_device_info_t) == sizeof(t5_device_handle_t) + 4 +
               T5_DEVICE_IDENTITY_MAX + T5_DEVICE_LABEL_MAX +
               T5_DEVICE_PROVIDER_MAX +
               T5_DEVICE_CAPABILITY_COUNT * T5_DEVICE_CAPABILITY_MAX,
               "v1 inventory binary layout changed");
int main(void) {
    assert(T5_DEVICE_API_VERSION == 1u && T5_DEVICE_API_VERSION_2 == 2u);
    assert(T5_DEVICE_BUSY == 4 && T5_DEVICE_UNAVAILABLE == 6);
    assert(T5_DEVICE_ERR_BUSY < 0 && T5_DEVICE_ERR_UNAVAILABLE < 0);
    assert((T5_DEVICE_RIGHT_READ | T5_DEVICE_RIGHT_WRITE |
            T5_DEVICE_RIGHT_CONFIGURE) == 7u);
    puts("Device ABI v2 C layout and v1 compatibility tests passed");
    return 0;
}
