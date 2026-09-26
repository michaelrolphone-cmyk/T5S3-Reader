#include "RiscTouchV1.h"
#include <assert.h>
#include <stddef.h>

int main(void) {
    assert(RISC_TOUCH_API_V1 == 1u);
    assert(RISC_TOUCH_MAX_CONTACTS == 5u);
    assert(RISC_TOUCH_PHASE_DOWN != RISC_TOUCH_PHASE_UP);
    assert(offsetof(risc_touch_api_v1, context) > offsetof(risc_touch_api_v1, struct_size));
    assert(sizeof(((risc_touch_snapshot_v1*)0)->contacts) /
               sizeof(risc_touch_contact_v1) == RISC_TOUCH_MAX_CONTACTS);
    risc_touch_snapshot_v1 snapshot = {0};
    assert(snapshot.contact_count == 0);
    return 0;
}
