#include "x4pro_proto.h"
#include <stdio.h>

static int failures;

static void expect(int cond, const char *message) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", message);
        ++failures;
    }
}

int main(void) {
    expect(x4pro_cw2017_millivolts(0x0F, 0xFF) == (uint16_t)(((0x0FFFu * 5u + 8u) >> 4)), "vcell");
    uint16_t x = 0, y = 0;
    uint8_t id = 0;
    uint8_t point[8] = {10, 0, 20, 0, 0, 0, 0, 3};
    expect(x4pro_gt911_map(point, &x, &y, &id) && x == 20 && y == 10 && id == 3, "swapxy");
    point[0] = 224;
    point[1] = 1;
    expect(!x4pro_gt911_map(point, &x, &y, &id), "x range");
    uint8_t frame[6];
    x4pro_sd_command(0, 0, frame);
    expect(frame[0] == 0x40 && (frame[5] & 1u), "cmd0 host direction");
    if (failures) return 1;
    puts("x4pro protocol: PASS");
    return 0;
}
