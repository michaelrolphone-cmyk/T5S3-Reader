#include <cassert>
#include <cstdio>
#include "../../src/runtime/input/NavigationFocus.h"
static size_t count;
static bool accept = true;
static bool foreground(void*, const risc_input_foreground_v1* claims, size_t n) {
    count = n;
    for (size_t i = 0; i < n; ++i) assert(claims[i].api_version && claims[i].capability[0]);
    return accept;
}
int main() {
    RuntimeInput::NavigationFocus focus;
    const risc_input_navigation_api_v1 api{1, sizeof(api), nullptr, nullptr, foreground, nullptr};
    // Claims made before navigation loads still suppress it on activation.
    char name[] = "some.input";
    assert(focus.acquire(1, name, 1, nullptr));
    name[0] = 'x'; // registry owns strings, never borrowed app memory
    assert(focus.apply(&api) && count == 1);
    assert(focus.acquire(2, "some.input", 1, &api) && count == 2);
    assert(!focus.acquire(2, "duplicate.token", 1, &api));
    assert(focus.release(1, &api) && count == 1);
    assert(!focus.release(1, &api));
    accept = false;
    assert(!focus.acquire(3, "other.input", 1, &api));
    accept = true;
    assert(focus.apply(&api) && count == 1); // failed handoff rolled back
    assert(focus.release(2, &api) && count == 0);
    puts("Navigation focus copied ownership, duplicate grants and rollback: PASS");
}
