#pragma once
#include <cstdint>
#include <cstdlib>
constexpr uint32_t MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
inline void* heap_caps_malloc(size_t size,uint32_t){return std::malloc(size);}
inline void* heap_caps_calloc(size_t count,size_t size,uint32_t){return std::calloc(count,size);}
inline void* heap_caps_realloc(void* p,size_t size,uint32_t){return std::realloc(p,size);}
inline void heap_caps_free(void* p){std::free(p);}
