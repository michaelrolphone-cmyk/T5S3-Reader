#pragma once
#include "RiscReaderTypographyV1.h"
// Private host helpers; never ELF imports. The public interface is leased via
// T5ProviderCapabilityApi and authorized by the app's manifest and owner.
bool nativeReaderPage(const risc_reader_page_request_v1 *,risc_reader_page_result_v1 *);
