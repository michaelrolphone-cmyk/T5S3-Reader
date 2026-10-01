#include <T5AppApi.h>
#include <T5LoRaApi.h>
#include "runtime/resources/RadioPower.h"
#include <cassert>
#include <cstdio>
unsigned hardwareCalls;
static unsigned acquired, released, depth;
static bool fault;
extern "C" bool risc_sd_spi_faulted() { return fault; }
extern "C" void risc_sd_spi_begin_operation() { assert(!fault); ++depth; }
extern "C" void risc_sd_spi_end_operation() { assert(!fault && depth); --depth; }
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t) { static t5_app_api_v1 api{}; return &api; }
namespace RadioPower {
bool acquire(Owner) { ++acquired; return true; }
void release(Owner) { ++released; }
}
int main() {
 auto* api=t5_lora_get_api(T5_LORA_API_VERSION);
 assert(api && api->start(nullptr) && acquired==1 && !depth);
 t5_lora_state_t state{};
 assert(api->read_state(&state) && state.status==T5_LORA_STATUS_READY);
 const auto calls=hardwareCalls;
 fault=true;
 assert(api->read_state(&state) && state.status==T5_LORA_STATUS_ERROR &&
        state.last_error==T5_LORA_ERROR_REBOOT_REQUIRED && !state.receiver_active);
 uint8_t data=1; t5_lora_packet_t packet{};
 assert(!api->start(nullptr) && !api->transmit(&data,1) && !api->poll_packet(&packet));
 assert(!api->prepare_display() && !api->finish_display());
 api->stop(); api->stop();
 assert(hardwareCalls==calls && acquired==1 && !released && !depth);
 puts("Actual LoRa bridge fault: unavailable, no reset/SPI/pin/rail teardown or retry PASS");
}
