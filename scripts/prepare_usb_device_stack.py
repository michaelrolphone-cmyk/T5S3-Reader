#!/usr/bin/env python3
"""Stage pinned TinyUSB with bounded, cooperative ESP32-S3 owner-task hooks."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
PIN='1eb6ce784ca9b8acbbe43dba9f1d9c26c2e80eb0' # upstream 0.16.0
SOURCES=('tusb.c','common/tusb_fifo.c','device/usbd.c','device/usbd_control.c',
         'class/msc/msc_device.c','portable/synopsys/dwc2/dcd_dwc2.c')
def replace(s,a,b):
    if s.count(a)!=1: raise ValueError('Pinned TinyUSB patch anchor changed: '+a[:80])
    return s.replace(a,b)
def prepare(source,output):
    source,output=Path(source),Path(output)
    if output.exists(): raise ValueError('Stack staging directory must be new')
    commit=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if commit!=PIN: raise ValueError('TinyUSB source must be pinned 0.16.0 commit '+PIN)
    if subprocess.check_output(['git','-C',str(source),'status','--porcelain','--','src'],text=True).strip():
        raise ValueError('TinyUSB source has local edits')
    shutil.copytree(source/'src',output)
    hashes={p:hashlib.sha256((source/'src'/p).read_bytes()).hexdigest() for p in SOURCES}
    p=output/'portable/synopsys/dwc2/dcd_dwc2.c';s=p.read_text()
    s=replace(s,'#include "dwc2_esp32.h"','#include "dwc2_owner.h"')
    patches={
      'while (dwc2->grstctl & GRSTCTL_CSRST) {}':'if (!risc_msc_wait(&dwc2->grstctl, GRSTCTL_CSRST, false)) return;',
      'while (!(dwc2->grstctl & GRSTCTL_AHBIDL)) {}':'if (!risc_msc_wait(&dwc2->grstctl, GRSTCTL_AHBIDL, true)) return;',
      'while (dwc2->grstctl & GRSTCTL_TXFFLSH_Msk) {}':'if (!risc_msc_wait(&dwc2->grstctl, GRSTCTL_TXFFLSH_Msk, false)) return;',
      'while (dwc2->grstctl & GRSTCTL_RXFFLSH_Msk) {}':'if (!risc_msc_wait(&dwc2->grstctl, GRSTCTL_RXFFLSH_Msk, false)) return;',
      'while ((epin[epnum].diepint & DIEPINT_INEPNE) == 0) {}':'if (!risc_msc_wait(&epin[epnum].diepint, DIEPINT_INEPNE, true)) return;',
      'while ((epin[epnum].diepint & DIEPINT_EPDISD_Msk) == 0) {}':'if (!risc_msc_wait(&epin[epnum].diepint, DIEPINT_EPDISD_Msk, true)) return;',
      'while ((dwc2->grstctl & GRSTCTL_TXFFLSH_Msk) != 0) {}':'if (!risc_msc_wait(&dwc2->grstctl, GRSTCTL_TXFFLSH_Msk, false)) return;',
      'while ((dwc2->gintsts & GINTSTS_BOUTNAKEFF_Msk) == 0) {}':'if (!risc_msc_wait(&dwc2->gintsts, GINTSTS_BOUTNAKEFF_Msk, true)) return;',
      'while ((epout[epnum].doepint & DOEPINT_EPDISD_Msk) == 0) {}':'if (!risc_msc_wait(&epout[epnum].doepint, DOEPINT_EPDISD_Msk, true)) return;',
    }
    for a,b in patches.items(): s=replace(s,a,b)
    s=replace(s,'  // Restart PHY clock','  if (!risc_msc_transport_ok()) return;\n\n  // Restart PHY clock')
    s=replace(s,'  dwc2->gahbcfg |= GAHBCFG_GINT;','  dwc2->gahbcfg &= ~(GAHBCFG_GINT | GAHBCFG_DMAEN);')
    s=replace(s,'    do {\n      handle_rxflvl_irq(rhport);\n    } while (dwc2->gotgint & GINTSTS_RXFLVL);',
                  '    for (unsigned budget=0; budget<32u && (dwc2->gintsts & GINTSTS_RXFLVL); ++budget) {\n      handle_rxflvl_irq(rhport);\n    }')
    p.write_text(s)
    p=output/'device/usbd.c';s=p.read_text()
    s=replace(s,'#include "tusb_option.h"','#include "tusb_option.h"\n#include "Transport.h"')
    s=replace(s,'  bool ret = osal_queue_send(_usbd_q, event, in_isr);','  bool ret = osal_queue_send(_usbd_q, event, in_isr);\n  if (!ret) risc_msc_transport_fault();')
    s=replace(s,'  // Loop until there is no more events in the queue\n  while (1)',
                  '  // Bounded owner-task service; no ISR can append concurrently.\n  for (unsigned budget=0; budget<1u && risc_msc_transport_ok(); ++budget)')
    s=replace(s,'    dcd_edpt_stall(rhport, ep_addr);','    risc_msc_protocol_stall();\n    dcd_edpt_stall(rhport, ep_addr);')
    s+='\n/* Called only after a successful checked DWC core reset and no ISR/task. */\nvoid risc_msc_stack_reset(void) {\n  _usbd_rhport = RHPORT_INVALID;\n  tu_fifo_clear(&_usbd_qdef.ff);\n  tu_varclr(&_usbd_dev);\n  usbd_control_reset();\n}\n'
    p.write_text(s)
    p=output/'class/msc/msc_device.c';s=p.read_text()
    s=replace(s,'#include \"tusb_option.h\"','#include \"tusb_option.h\"\n#include \"Transport.h\"\n#include <stdbool.h>\n#include <stdint.h>\nextern bool risc_msc_command_range(uint32_t,uint32_t);\nextern bool risc_msc_command_valid(const uint8_t*,uint8_t,uint32_t);')
    s=replace(s,'xferred_bytes == sizeof(msc_cbw_t) && p_cbw->signature == MSC_CBW_SIGNATURE',
                  'xferred_bytes == sizeof(msc_cbw_t) && p_cbw->signature == MSC_CBW_SIGNATURE && p_cbw->lun == 0 && p_cbw->cmd_len >= 1 && p_cbw->cmd_len <= 16 && !(p_cbw->dir & 0x7f)')
    s=replace(s,'  return status;\n}',
                  '  if (cbw->cmd_len != 10 || cbw->total_bytes != (uint32_t)block_count * 512u ||\n      (block_count && !risc_msc_command_range(rdwr10_get_lba(cbw->command), block_count)))\n    status = MSC_CSW_STATUS_PHASE_ERROR;\n  return status;\n}')
    s=replace(s,'      /*------------- Parse command and prepare DATA -------------*/',
        '      risc_msc_command_started(p_cbw->command[0], p_cbw->tag, p_cbw->total_bytes,\n        (p_cbw->command[0]==SCSI_CMD_READ_10 || p_cbw->command[0]==SCSI_CMD_WRITE_10)?rdwr10_get_lba(p_cbw->command):0,\n        (p_cbw->command[0]==SCSI_CMD_READ_10 || p_cbw->command[0]==SCSI_CMD_WRITE_10)?rdwr10_get_blockcount(p_cbw):0);\n      /*------------- Parse command and prepare DATA -------------*/')
    s=replace(s,'        switch(p_cbw->command[0])','        risc_msc_command_completed(p_csw->status);\n        switch(p_cbw->command[0])')
    for comment in ('// set sense', '// Set sense'):
        s=replace(s,comment+'\n    set_sense_medium_not_present(p_cbw->lun);',comment+'\n    if (p_msc->sense_key == 0) set_sense_medium_not_present(p_cbw->lun);')
    s=replace(s,'      // Read10 or Write10',
        '      if (!risc_msc_command_valid(p_cbw->command, p_cbw->cmd_len, p_cbw->total_bytes)) {\n        fail_scsi_op(rhport, p_msc, MSC_CSW_STATUS_FAILED);\n        break;\n      }\n      // Read10 or Write10')
    s=replace(s,'if ( tud_msc_scsi_complete_cb ) tud_msc_scsi_complete_cb(p_cbw->lun, p_cbw->command);',
        'if (p_csw->status == MSC_CSW_STATUS_PASSED && tud_msc_scsi_complete_cb) tud_msc_scsi_complete_cb(p_cbw->lun, p_cbw->command);')
    p.write_text(s)
    (output/'risc-stack-source.json').write_text(json.dumps({'repository':'https://github.com/hathach/tinyusb','commit':PIN,'source_sha256':hashes,'adaptation':'Reader scripts/prepare_usb_device_stack.py'},indent=2)+'\n')
    return output
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    a=parser.parse_args();prepare(a.source,a.output)
