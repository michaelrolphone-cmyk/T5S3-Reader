#include <assert.h>
#include <stdint.h>

typedef struct {
    int32_t ymd;
    int16_t punches[4];
} tc_day_t;

static int16_t worked(const tc_day_t *day) {
    if (!day || day->punches[0] < 0 || day->punches[3] < day->punches[0]) return -1;
    int16_t total = (int16_t)(day->punches[3] - day->punches[0]);
    if (day->punches[1] >= 0 && day->punches[2] >= day->punches[1]) {
        const int16_t lunch_start =
            day->punches[1] > day->punches[0] ? day->punches[1] : day->punches[0];
        const int16_t lunch_end =
            day->punches[2] < day->punches[3] ? day->punches[2] : day->punches[3];
        if (lunch_end > lunch_start) {
            total = (int16_t)(total - (lunch_end - lunch_start));
        }
    }
    return total < 0 ? 0 : total;
}

static void expect(int16_t in, int16_t ls, int16_t le, int16_t out, int16_t expected) {
    tc_day_t day = {20260926, {in, ls, le, out}};
    assert(worked(&day) == expected);
}

int main(void) {
    expect(540, 720, 780, 1020, 420);
    expect(540, 420, 480, 1020, 480);
    expect(540, 1080, 1140, 1020, 480);
    expect(540, 510, 570, 1020, 450);
    expect(540, 990, 1050, 1020, 450);
    expect(540, 480, 1080, 1020, 0);
    expect(540, 480, 540, 1020, 480);
    expect(540, 1020, 1080, 1020, 480);
    expect(540, -1, -1, 1020, 480);
    expect(540, 780, 720, 1020, 480);
    return 0;
}
