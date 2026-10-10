#pragma once
#include <stdbool.h>
#include <stdint.h>
bool risc_msc_transport_start(void);
bool risc_msc_transport_poll(void);
bool risc_msc_transport_stop(void);
void risc_msc_transport_fault(void);
bool risc_msc_transport_ok(void);
void risc_msc_stack_reset(void);

uint64_t risc_msc_now(void);
bool risc_msc_command_pending(void);
void risc_msc_command_aborted(void);
void risc_msc_command_abort(uint32_t reason);
void risc_msc_command_progress(uint32_t transferred,uint32_t residue);
void risc_msc_sense(uint8_t key,uint8_t asc,uint8_t ascq);
void risc_msc_timeout(uint32_t reason);
void risc_msc_command_started(uint8_t opcode,uint32_t tag,uint32_t bytes,uint32_t lba,uint32_t count);
void risc_msc_command_completed(uint8_t status);
void risc_msc_protocol_stall(void);
void risc_msc_pump_report(uint32_t passes);
