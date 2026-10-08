#!/usr/bin/env python3
"""Compile production request + exact adapted SDK steps with simulated OS/HAL.

Require the same immutable source as the target builder; run no networking.
Buffer fill/execution, register events and OS contention are simulated. All
retirement, halt ISR, flush, callback and request transition bodies are real.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from stage_usb_bounded import stage


def function(source, signature):
    start = source.index(signature + '\n{')
    end = source.index('\n}', start) + 2
    return source[start:end] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-source', type=Path, required=True)
    parser.add_argument('--admission', action='store_true')
    parser.add_argument('--control', action='store_true')
    args = parser.parse_args()
    args.admission = args.admission or args.control
    usb = args.idf_source / 'components/usb'
    tests = ROOT / 'test/drivers'
    includes = ['-I' + str(tests / 'stub_usb_bounded'), '-I' + str(tests),
                '-I' + str(ROOT / 'Drivers/usb_controller_esp32s3'),
                '-I' + str(ROOT / 'sdk/driver')]
    with tempfile.TemporaryDirectory(prefix='usb-bounded-') as temp:
        temp = Path(temp)
        sources = {name: stage(usb / name, temp / name).read_text()
                   for name in (('usb_host.c', 'hcd_dwc.c', 'usbh.c') if args.admission else ('usb_host.c', 'hcd_dwc.c'))}
        # Source drift must never silently produce a partial patch.
        bad = temp / 'bad' / 'usb_host.c'
        bad.parent.mkdir()
        bad.write_text((usb / 'usb_host.c').read_text() + '\n')
        try:
            stage(bad, temp / 'unexpected.c')
        except ValueError:
            assert not (temp / 'unexpected.c').exists()
        else:
            raise AssertionError('Altered pinned input was accepted')
        host, hcd = sources['usb_host.c'], sources['hcd_dwc.c']
        private = (usb / 'private_include/usb_private.h').read_text()
        urb = private[private.index('struct urb_s{'):private.index('typedef struct urb_s urb_t;') + len('typedef struct urb_s urb_t;')]
        host_types = host[host.index('typedef struct endpoint_s'):host.index('static host_lib_t *p_host_lib_obj = NULL;') + len('static host_lib_t *p_host_lib_obj = NULL;')]
        hcd_types = hcd[hcd.index('typedef struct pipe_obj'):hcd.index('/**\n * @brief Object representing a port in the HCD layer')]
        body = '#include "usb_bounded_sdk_preamble.h"\n' + urb + '\n' + host_types + '\n' + hcd_types
        if args.admission:
            usbh = sources['usbh.c']
            begin = usbh.index('typedef struct device_s device_t;')
            end = usbh.index('\n};', begin) + 3
            body += usbh[begin:end]
            body += '\n#include "usb_admission_sdk_port.inc"\n'
        body += '\n#include "usb_bounded_sdk_port.inc"\n'
        if args.control:
            # Official control parser compares int remainder with sizeof's
            # unsigned result. Keep its bytes exact; scope the host-only warning.
            body += '\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'
            for signature in ('static inline void _buffer_fill_ctrl(dma_buffer_block_t *buffer, usb_transfer_t *transfer)',
                              'static void _buffer_exec_cont(pipe_t *pipe)',
                              'static inline void _buffer_parse_ctrl(dma_buffer_block_t *buffer)'):
                body += function(hcd, signature)
            body += '\n#pragma GCC diagnostic pop\n'
        for signature in (
            'static inline bool _buffer_can_fill(pipe_t *pipe)',
            'static inline bool _buffer_can_exec(pipe_t *pipe)',
            'static inline bool _buffer_check_done(pipe_t *pipe)',
            'static inline void _buffer_done(pipe_t *pipe, int stop_idx, hcd_pipe_event_t pipe_event, bool canceled)',
            'static inline void _buffer_parse_bulk(dma_buffer_block_t *buffer)',
            'static inline void _buffer_parse_error(dma_buffer_block_t *buffer)',
            'static void _buffer_parse(pipe_t *pipe)',
            'static bool _buffer_flush_all(pipe_t *pipe, bool canceled)',
            'static esp_err_t _pipe_cmd_flush(pipe_t *pipe)',
            'static esp_err_t _pipe_cmd_clear(pipe_t *pipe)',
            'esp_err_t hcd_pipe_command(hcd_pipe_handle_t pipe_hdl, hcd_pipe_cmd_t command)',
            'esp_err_t hcd_pipe_free(hcd_pipe_handle_t pipe_hdl)',
            'esp_err_t hcd_urb_enqueue(hcd_pipe_handle_t pipe_hdl, urb_t *urb)',
            'urb_t *hcd_urb_dequeue(hcd_pipe_handle_t pipe_hdl)',
            'static inline hcd_pipe_event_t pipe_decode_error_event(usb_dwc_hal_chan_error_t chan_error)',
            'static hcd_pipe_event_t _intr_hdlr_chan(pipe_t *pipe, usb_dwc_hal_chan_t *chan_obj, bool *yield)',
        ):
            body += function(hcd, signature)
        body += function(host, 'static bool pipe_callback(hcd_pipe_handle_t pipe_hdl, hcd_pipe_event_t pipe_event, void *user_arg, bool in_isr)')
        body += function(host, 'static inline bool _check_client_opened_device(client_t *client_obj, uint8_t dev_addr)')
        for name in ('hcd', 'host'):
            body += (ROOT / f'Drivers/usb_controller_esp32s3/idf/bounded_{name}.inc').read_text()
        if args.admission:
            body += function(host, 'static inline void _clear_client_opened_device(client_t *client_obj, uint8_t dev_addr)')
            body += function(hcd, 'static void pipe_set_ep_char(const hcd_pipe_config_t *pipe_config, usb_transfer_type_t type, bool is_default_pipe, int pipe_idx, usb_speed_t port_speed, usb_dwc_hal_ep_char_t *ep_char)')
            body += function(usbh, 'static bool _dev_set_actions(device_t *dev_obj, uint32_t action_flags)')
            for name in ('hcd_pool', 'usbh_admission', 'host_admission'):
                body += (ROOT / f'Drivers/usb_controller_esp32s3/idf/bounded_{name}.inc').read_text()
            body += function(host, 'static esp_err_t interface_release(client_t *client_obj, usb_device_handle_t dev_hdl, uint8_t bInterfaceNumber)')
        if args.control:
            body += '\n#include "usb_control_sdk_port.inc"\n'
            body += function(usbh, 'static bool default_pipe_callback(hcd_pipe_handle_t pipe_hdl, hcd_pipe_event_t pipe_event, void *user_arg, bool in_isr)')
            body += '\n#undef ESP_LOGE\n#undef ESP_EARLY_LOGE\n'
            for name in ('hcd_control', 'usbh_control', 'host_control'):
                body += (ROOT / f'Drivers/usb_controller_esp32s3/idf/bounded_{name}.inc').read_text()
            body += function(hcd, 'hcd_pipe_state_t hcd_pipe_get_state(hcd_pipe_handle_t pipe_hdl)')
            for signature in ('esp_err_t hcd_pipe_update_mps(hcd_pipe_handle_t pipe_hdl, int mps)',
                              'esp_err_t hcd_pipe_update_dev_addr(hcd_pipe_handle_t pipe_hdl, uint8_t dev_addr)',
                              'esp_err_t hcd_pipe_update_callback(hcd_pipe_handle_t pipe_hdl, hcd_pipe_callback_t callback, void *user_arg)',
                              'esp_err_t hcd_pipe_set_persist_reset(hcd_pipe_handle_t pipe_hdl)'):
                body += function(hcd, signature)
            body += function(usbh, 'esp_err_t usbh_dev_submit_ctrl_urb(usb_device_handle_t dev_hdl, urb_t *urb)')
        body += '\n#include "usb_bounded_legacy_messages.inc"\n'
        body += function(host, 'static void send_event_msg_to_clients(const usb_host_client_event_msg_t *event_msg, bool send_to_all, uint8_t opened_dev_addr)')
        body += function(host, 'esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client_hdl, TickType_t timeout_ticks)')
        body += '\n#include "usb_bounded_sdk_rig.inc"\n'
        if args.admission:
            body += '\n#include "usb_admission_sdk_rig.inc"\n'
        if args.control:
            body += '\n#include "usb_control_sdk_rig.inc"\n'
        generated = temp / 'rig.c'
        generated.write_text(body)
        if args.admission:
            controller = (ROOT / 'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text()
            start = controller.index('__attribute__((used)) int32_t take_owned_admission(')
            end = controller.index('\n}', start) + 2
            (temp / 'admission_wrapper.inc').write_text(controller[start:end])
            includes += ['-I' + str(temp)]
            if args.control:
                start = controller.index('struct OwnedControlPort {')
                end = controller.index('/* Allocation is an explicit', start)
                wrapper = controller[controller.index('struct Device {'):controller.index('struct Claim {')]
                for signature in ('bool native_admission_guard()', 'uint64_t token()', 'Device *device(uint64_t id)'):
                    position = controller.index(signature + ' {')
                    wrapper += controller[position:controller.index('\n}', position) + 2] + '\n'
                (temp / 'control_wrapper.inc').write_text(wrapper + controller[start:end])
        flags = ['-Wall', '-Wextra', '-Werror', '-g', '-O1']
        if args.admission:
            flags += ['-DRISC_USB_ADMISSION_TEST']
        if args.control:
            flags += ['-DRISC_USB_CONTROL_TEST']
        if os.environ.get('SANITIZE') == '1':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie']
        subprocess.run(['cc', '-std=gnu11', *flags, '-Wno-unused-parameter', *includes, '-c', str(generated), '-o', str(temp / 'rig.o')], check=True)
        subprocess.run(['c++', '-std=c++17', *flags, *includes,
                        str(tests / ('usb_controller_control_test.cpp' if args.control else 'usb_controller_admission_test.cpp' if args.admission else 'usb_controller_bounded_test.cpp')), str(temp / 'rig.o'),
                        *(['-no-pie'] if os.environ.get('SANITIZE') == '1' else []),
                        '-o', str(temp / 'test')], check=True)
        environment = os.environ.copy()
        if os.environ.get('SANITIZE') == '1':
            environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        cases = ['']
        if args.admission:
            cases = ['prepare', 'unprepared', 'config', 'success', 'zero', 'exhaust',
                     'partial', 'cancel_clean', 'cancel_partial', 'deadline_clean', 'deadline_partial',
                     'close', 'close_gone', 'close_waiting', 'close_pending', 'close_ctrl',
                     'close_claimed', 'stale_callback', 'late_callback', 'scope', 'malformed',
                     'retained_release', 'clock', 'legacy_release', 'deferred_claim',
                     'bulk_claim', 'take_stale', 'claim_commit_lock', 'release_commit_lock',
                     'release_close', 'owned_descriptors', 'endpoint_reject']
            cases += ['lock_' + str(i) for i in range(3)]
            cases += ['hcd_' + str(i) for i in range(11)]
            cases += ['host_' + str(i) for i in range(3)]
        if args.control:
            cases = ['in', 'out', 'zero', 'short', 'maximum', 'cancel_unsubmitted',
                     'deadline_unsubmitted', 'cancel_completed', 'cancel_retired', 'timeout_ack',
                     'missing_halt', 'late_halt', 'detach_before', 'detach_after', 'detached_clear',
                     'deadline_retired', 'deadline_submit', 'deadline_poll', 'deadline_restore',
                     'deadline_cancel', 'scope', 'stale_callback', 'duplicate_callback',
                     'retired_callback', 'finished_callback', 'reuse_callback', 'missing_library',
                     'deferred_library', 'deferred_client', 'legacy_guard', 'close_guard', 'clock',
                     'overflow', 'invalid', 'pending_cancel', 'deferred_callback', 'cookie_exhaustion',
                     'malformed_setup', 'wrapper', 'wrapper_retained', 'wrapper_tokens', 'reuse',
                     'dispatch_duplicate', 'dispatch_error', 'dispatch_retired', 'dispatch_deadline',
                     'dispatch_exhaustion', 'pool_dispatch']
            cases += ['cancel_stage_' + str(i) for i in range(3)]
            cases += ['error_' + str(i) for i in range(1,5)]
            cases += ['cancel_error_' + str(i) for i in range(1,5)]
            cases += ['lock_' + str(i) for i in range(3)]
            cases += ['restore_fault_' + str(i) for i in range(7)]
            cases += ['retire_fault_' + str(i) for i in (0,1,2,6)]
            cases += ['acquire_fault_' + str(i) for i in (0,1,2,6,7,8,9,10,11,12,14,15)]
        for case in cases:
            subprocess.run([str(temp / 'test'), *([case] if case else [])], check=True, timeout=15, env=environment)


if __name__ == '__main__':
    main()
