#!/usr/bin/env python3
"""Prevent resident BQ25896 access from returning to the U1 board paths."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
board = (root / 'lib/Board_T5S3/BoardT5S3.cpp').read_text(encoding='utf-8')
port_h = (root / 'lib/Board_T5S3/BoardPowerPort.h').read_text(encoding='utf-8')
port = (root / 'src/native/NativeBoardPowerPort.cpp').read_text(encoding='utf-8')
readiness = (root / 'src/native/NativeBoardPowerReadiness.cpp').read_text(encoding='utf-8')
bridge = (root / 'src/native/NativeBatteryBridge.cpp').read_text(encoding='utf-8')
display = (root / 'lib/hal/HalDisplay.cpp').read_text(encoding='utf-8')
owner = (root / 'Drivers/bq25896/driver.c').read_text(encoding='utf-8')

# Neither the legacy board nor the app-facing battery shim may own, reset,
# read or write chip registers. The fuel gauge stays a separate I2C chip.
for prohibited in ('#include <bq25896.h>', '#include <bq25896_hal_esp_idf.h>',
                   'bq25896_init(', 'bq25896_reset(', 'bq25896_shutdown(',
                   'bq25896_read_status(', 'T5S3_BQ25896_ADDR'):
    assert prohibited not in board, prohibited
    assert prohibited not in bridge, prohibited
assert 'BQ27220 bq27220;' in board
assert 'configureBq27220()' in board
assert 'BoardPowerPort::configure()' in board
assert 'BoardPowerPort::read(state)' in board
assert 'BoardPowerPort::shutdown()' in board
assert 'BoardPowerPort::externalPower(&external)' in board
assert 'bool readBQ25896Reg8(uint8_t, uint8_t*) {' in board
assert 'return false;' in board.split('bool readBQ25896Reg8(', 1)[1].split('\n}', 1)[0]

# Early power/GPIO boot occurs before SD mount. After mount, the first GPIO
# poll attempts an owner-only charge profile. Busy/missing ELF never causes
# resident fallback, and repeated polls do not remap/hash the ELF every loop.
management = board.split('bool beginBatteryManagement()', 1)[1].split('\n}', 1)[0]
assert 'BoardPowerPort::readyForActivation()' in management
assert 'chargerConfigured = BoardPowerPort::configure()' in management
usb = board.split('bool isUsbConnected()', 1)[1].split('\n}', 1)[0]
for expected in ('BoardPowerPort::readyForActivation()',
                 'chargerConfigured = BoardPowerPort::configure()',
                 'now - lastChargeAttemptMs >= 30000UL',
                 'now - lastSampleMs < 1000UL',
                 'BoardPowerPort::externalPower(&external)'):
    assert expected in usb, expected
assert 'return Storage.ready();' in readiness

# The actual power-off sequence sleeps the display before requesting BATFET.
# Pin the already-verified owner before Board::deinitForSleep drops SD_CS, or
# the loader would have to access an unavailable package store after sleep.
sleep = board.split('void deinitForSleep()', 1)[1].split('\n}', 1)[0]
assert sleep.index('BoardPowerPort::prepareShutdown()') < sleep.index('pinMode(T5S3_SD_CS, INPUT)')
assert 'Board::deinitForSleep();' in display.split('void HalDisplay::deepSleep()', 1)[1].split('\n}', 1)[0]
for required in ('bool prepareShutdown();', 'bool shutdown();',
                 'bool readyForActivation();'):
    assert required in port_h, required
for required in ('RuntimeInstalledProviders::nextProvider("board.power.vbus"',
                 'sizeof(risc_usb_vbus_charger_api_v1)',
                 'preparedShutdownGrant = session.grant;',
                 'session.grant = preparedShutdownGrant;',
                 'retainedShutdownGrant = session.grant;',
                 'shutdownPending = true;',
                 'RuntimeInstalledProviders::release(&session.grant)'):
    assert required in port, required
prepare = port.split('bool prepareShutdown()', 1)[1].split('\n}', 1)[0]
shutdown = port.split('bool shutdown()', 1)[1].split('\n}', 1)[0]
assert prepare.index('shutdownPreparationAttempted = true;') < prepare.index('if (!acquire(session))')
assert 'if (shutdownPreparationAttempted || !acquire(session)) return false;' in shutdown
for prohibited in ('#include <Wire.h>', 'T5S3_BQ25896_ADDR', 'bq25896_reset('):
    assert prohibited not in port, prohibited
assert 'risc_bq_request_shutdown(&io)' in owner
assert 'risc_bq_apply_charge_profile(&io, &uncertain)' in owner
assert 'bus->claim_device(bus->context, BQ_ADDRESS' in owner
print('BQ single-owner board cutover, post-mount configuration and sleep reservation source guards: PASS')
