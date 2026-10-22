#pragma once
#include "RiscReaderTypographyV1.h"

class GfxRenderer;

// A synchronous borrow: the source renderer and its registered fonts must remain
// alive and unchanged until nativeReaderPage returns. No resources are retained.
// Font selection/fallback and loading belong to the caller, not the renderer.
// This private host input uses an existing registered font ID and a finite line
// compression from the caller's established typography policy.
struct NativeReaderTypographyResources {
  const GfxRenderer& renderer;
  int fontId;
  float lineCompression;
};

// Pure preflight lets the firmware adapter reject invalid input before loading
// fonts. The resource-consuming entry also validates requests independently.
bool nativeReaderPageRequestValid(const risc_reader_page_request_v1 *);
// started is the caller's millis() timestamp before font preparation, so that
// loading and rendering share the existing five-second cooperative budget.
bool nativeReaderPage(const NativeReaderTypographyResources&,
                      const risc_reader_page_request_v1 *,risc_reader_page_result_v1 *,
                      uint32_t started);

// Firmware composition adapter. Private host helper; never an ELF import. The
// public interface is leased via T5ProviderCapabilityApi and authorized by the
// app's manifest and owner.
bool nativeReaderPage(const risc_reader_page_request_v1 *,risc_reader_page_result_v1 *);
