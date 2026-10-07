/* Private C++ runtime hooks for the extracted engines. No constructors or
 * firmware peripheral entry points are registered by this image. */
#include <cstddef>
#include <cstdlib>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
void operator delete(void *memory) noexcept { heap_caps_free(memory); }
void operator delete(void *memory,std::size_t) noexcept { heap_caps_free(memory); }
extern "C" void __cxa_pure_virtual() { abort(); }
extern "C" void ets_delay_us(uint32_t us) { esp_rom_delay_us(us); }
