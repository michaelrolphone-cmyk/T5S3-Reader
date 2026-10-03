#include <stdatomic.h>
void provider_entry(void *argument) {
    atomic_uint **counter=argument;
    atomic_fetch_add(*counter,1);
}
