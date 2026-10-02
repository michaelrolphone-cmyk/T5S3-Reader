#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RISC_READER_TYPOGRAPHY_V1 1u
#define RISC_READER_TEXT_MAX 16384u
/* Software capability: reader.typography. Caller owns the MSB-first bitmap;
 * one means black. No display refresh, device handle or font internals escape.
 * UTF-8 offsets returned by page() are valid boundaries for the next call. */
typedef struct {
    uint8_t *pixels;
    size_t capacity;
    uint16_t width,height,stride;
    const char *title,*footer,*text;
    size_t length,offset;
} risc_reader_page_request_v1;
typedef struct { size_t next_offset; uint16_t line_height,lines; } risc_reader_page_result_v1;
typedef struct {
    uint32_t api_version,struct_size;
    void *context;
    bool (*page)(void *context,const risc_reader_page_request_v1 *request,
                 risc_reader_page_result_v1 *result);
} risc_reader_typography_v1;
