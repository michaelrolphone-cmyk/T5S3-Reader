#!/usr/bin/env python3
"""Stage exact v4.4.7 private bounded steps; never edit the SDK source cache.

The original API behavior stays intact; added private fields track event and
halt custody. Unknown upstream input fails closed before staging.
"""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADAPTATION = ROOT / 'Drivers/usb_controller_esp32s3/idf'
PINNED = {
    'usb_host.c': 'ecd49ace784afb4bfeed353c001e721ef36a0bec',
    'hcd_dwc.c': '1aeb3a1a2f6ed1b2cf00f98513f17607af4dd33d',
    'usbh.c': '76942870194134dcb5b90269d22291e37db38b8b',
}


def blob_id(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Pinned HCD adaptation anchor changed: ' + before[:80])
    return source.replace(before, after, 1)


def stage(path, output):
    data = Path(path).read_bytes()
    name = Path(path).name
    if name not in PINNED or blob_id(data) != PINNED[name]:
        raise ValueError('Unexpected ESP-IDF v4.4.7 USB source: ' + str(path))
    source = data.decode('utf-8')
    if name == 'usb_host.c':
        source = replace_once(source,
            'static host_lib_t *p_host_lib_obj = NULL;',
            'static host_lib_t *p_host_lib_obj = NULL;\n'
            'static bool risc_native_interface_owned(interface_t *);')
        source = replace_once(source,
            '    //Check that all endpoints in the interface are in a state to be freed',
            '    if (risc_native_interface_owned(intf_obj)) return ESP_ERR_INVALID_STATE;\n\n'
            '    //Check that all endpoints in the interface are in a state to be freed')
        source = replace_once(source,
            '                uint32_t reserved31:31;',
            '                uint32_t bounded_owned:1;\n'
            '                uint32_t reserved30:30;')
        source = replace_once(source, '        uint32_t num_done_ctrl_xfer;',
            '        uint32_t num_done_ctrl_xfer;\n        uint32_t bounded_num_messages;')
        source = replace_once(source,
            '        if (xQueueSend(client_obj->constant.event_msg_queue, event_msg, 0) == pdTRUE) {\n            HOST_ENTER_CRITICAL();',
            '        if (xQueueSend(client_obj->constant.event_msg_queue, event_msg, 0) == pdTRUE) {\n'
            '            HOST_ENTER_CRITICAL();\n            client_obj->dynamic.bounded_num_messages++;')
        source = replace_once(source, '            assert(queue_ret == pdTRUE);',
            '            assert(queue_ret == pdTRUE);\n'
            '            HOST_ENTER_CRITICAL();\n'
            '            assert(client_obj->dynamic.bounded_num_messages > 0);\n'
            '            client_obj->dynamic.bounded_num_messages--;\n'
            '            HOST_EXIT_CRITICAL();')
        source = replace_once(source,
            '    bool yield = _unblock_client(client_obj, in_isr);',
            '    bool yield = false;\n'
            '    if (ep_obj->dynamic.flags.bounded_owned) {\n'
            '        client_obj->dynamic.flags.events_pending = 1;\n'
            '    } else {\n'
            '        yield = _unblock_client(client_obj, in_isr);\n'
            '    }')
        source = replace_once(source,
            '    ep_obj->dynamic.num_urb_inflight++;',
            '    ep_obj->dynamic.flags.bounded_owned = 0;\n'
            '    ep_obj->dynamic.num_urb_inflight++;')
    if name == 'hcd_dwc.c':
        source = replace_once(source,
            '    //Pipe callback and context\n',
            '    uint32_t bounded_dispatch; // Decoded private callback custody, saturating\n'
            '    //Pipe callback and context\n')
        source = replace_once(source,
            'static void intr_hdlr_main(void *arg)\n{',
            'static void risc_hcd_dispatch_callback(pipe_t *, hcd_pipe_event_t, bool *);\n\n'
            'static void intr_hdlr_main(void *arg)\n{')
        source = replace_once(source,
            '                HCD_EXIT_CRITICAL_ISR();\n'
            '                yield |= pipe->callback((hcd_pipe_handle_t)pipe, event, pipe->callback_arg, true);\n'
            '                HCD_ENTER_CRITICAL_ISR();',
            '                risc_hcd_dispatch_callback(pipe, event, &yield);')
        source = replace_once(source,
            'esp_err_t hcd_pipe_free(hcd_pipe_handle_t pipe_hdl)\n{',
            'static bool risc_hcd_pool_contains(hcd_pipe_handle_t);\n\n'
            'esp_err_t hcd_pipe_free(hcd_pipe_handle_t pipe_hdl)\n{')
        source = replace_once(source,
            '            uint32_t reserved27: 27;',
            '            uint32_t bounded_halt: 1; // Private try/poll halt custody\n'
            '            uint32_t bounded_control: 1; // Exclusive private EP0 custody\n'
            '            uint32_t reserved25: 25;')
        source = replace_once(source,
            '            *yield |= _internal_pipe_event_notify(pipe, true);',
            '            if (pipe->cs_flags.bounded_halt) {\n'
            '                pipe->cs_flags.waiting_halt = 0;\n'
            '            } else {\n'
            '                *yield |= _internal_pipe_event_notify(pipe, true);\n'
            '            }')
        source = replace_once(source,
            '            pipe->state = HCD_PIPE_STATE_HALTED;\n            //Mark the buffer as done with an error',
            '            pipe->state = HCD_PIPE_STATE_HALTED;\n'
            '            if (pipe->cs_flags.bounded_halt) {\n'
            '                // HAL error decoding requires CHHLTD and clears active.\n'
            '                pipe->cs_flags.waiting_halt = 0;\n'
            '                chan_obj->flags.halt_requested = 0;\n'
            '            }\n'
            '            //Mark the buffer as done with an error')
        source = replace_once(source,
            '    if (pipe->cs_flags.reset_lock) {',
            '    if (pipe->cs_flags.reset_lock || pipe->cs_flags.bounded_halt || pipe->cs_flags.bounded_control) {')
        source = replace_once(source,
            '    HCD_CHECK_FROM_CRIT(!pipe->multi_buffer_control.buffer_is_executing\n',
            '    HCD_CHECK_FROM_CRIT(!risc_hcd_pool_contains(pipe_hdl)\n'
            '                        && !pipe->cs_flags.bounded_halt\n'
            '                        && !pipe->cs_flags.bounded_control\n'
            '                        && !pipe->multi_buffer_control.buffer_is_executing\n')
        # Preserve legacy error handling; a private control BNA is a real
        # halted-channel error, never an asserted/fabricated successful URB.
        source = replace_once(source,
            '            _buffer_done(pipe, stop_idx, pipe->last_event, false);\n            //Parse the buffer',
            '            hcd_pipe_event_t parsed_event = pipe->last_event;\n'
            '            if (pipe->cs_flags.bounded_control && parsed_event == HCD_PIPE_EVENT_ERROR_URB_NOT_AVAIL)\n'
            '                parsed_event = HCD_PIPE_EVENT_ERROR_XFER;\n'
            '            _buffer_done(pipe, stop_idx, parsed_event, false);\n            //Parse the buffer')
        anchor = '    HCD_CHECK_FROM_CRIT(!pipe->cs_flags.pipe_cmd_processing &&\n'
        if source.count(anchor) != 4:
            raise ValueError('Pinned HCD pipe-update guards changed')
        source = source.replace(anchor,
            '    HCD_CHECK_FROM_CRIT(!pipe->cs_flags.bounded_control &&\n'
            '                        !pipe->cs_flags.pipe_cmd_processing &&\n')
    if name == 'usbh.c':
        source = replace_once(source, 'static usbh_t *p_usbh_obj = NULL;',
            'static usbh_t *p_usbh_obj = NULL;\n'
            'static bool risc_usbh_control_owns(usb_device_handle_t);')
        source = replace_once(source,
            '    //Increment the control transfer count first',
            '    USBH_CHECK_FROM_CRIT(!risc_usbh_control_owns(dev_hdl), ESP_ERR_INVALID_STATE);\n'
            '    //Increment the control transfer count first')
    suffixes = {'usb_host.c': ('host', 'host_admission', 'host_control'),
                'hcd_dwc.c': ('hcd', 'hcd_pool', 'hcd_control'),
                'usbh.c': ('usbh_admission', 'usbh_control')}[name]
    for suffix in suffixes:
        source += '\n' + (ADAPTATION / f'bounded_{suffix}.inc').read_text()
    Path(output).write_text(source)
    return Path(output)
