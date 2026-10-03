"""Board-specific app-only CI transaction and verified idle-heartbeat cleanup.

Install a reviewed pinned copy outside a candidate checkout. Credentials never
enter this child. No partition writes, filesystem access, or board discovery by
ordinal. External application changes are expected; no prior application image
is backed up or restored. Return the board to the owner's heartbeat policy.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys
import time
import zlib

from device_locks import locks

BOARDS = {
    # Requalified after the owner installed MicroPython on 2026-10-03:
    # same verified chip, factory app at 0x10000; preserve its new table.
    'cam-sd': ('28:84:85:4b:57:98', 'a9c077430aa2b37e4c6ecd3d1265cdf38391d76d8e959fd8f2e3404174b850cc', 0x1f0000, 0x1a86, 0x7523),
    'cam-nosd': ('28:84:85:4b:a1:1c', 'a9c077430aa2b37e4c6ecd3d1265cdf38391d76d8e959fd8f2e3404174b850cc', 0x1f0000, 0x1a86, 0x7523),
    'x4': ('84:c7:bb:79:e2:ac', '9af3af2b74e944337ba85f2b0027ee80df160579a1ab746ba0f95853f618cd60', 0x640000, 0x303a, 0x1001),
}

def require(ok, why):
    if not ok:
        raise RuntimeError(why)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def save(path, data):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(data, indent=2) + '\n')
    os.chmod(tmp, 0o600)
    tmp.replace(path)

def image_bytes(path, expected, target, heartbeat=False):
    require(path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= BOARDS[target][2], 'Image size/path invalid')
    data = path.read_bytes()
    require(re.fullmatch('[0-9a-f]{64}', expected) and digest(data) == expected, 'Image digest mismatch')
    require(len(data) > 24 and data[0] == 0xe9 and struct.unpack_from('<H', data, 12)[0] == 9, 'Image is not ESP32-S3 app')
    if heartbeat:
        require(b'RTE_HEARTBEAT version=' in data and target.encode() + b'\0' in data, 'Wrong heartbeat target')
    return data

def check_layout(prefix, target, length):
    require(len(prefix) == 0x10000, 'Protected-region read truncated')
    require(digest(prefix[0x8000:0x9000]) == BOARDS[target][1], 'Partition layout changed; review required')
    require(0 < length <= BOARDS[target][2], 'Erase range exceeds qualified app')
    if target == 'x4':
        entries = []
        for offset in (0xe000, 0xf000):
            seq = struct.unpack_from('<I', prefix, offset)[0]
            state, crc = struct.unpack_from('<II', prefix, offset + 24)
            if seq != 0xffffffff and state not in (3, 4) and crc == zlib.crc32(struct.pack('<I', seq), 0xffffffff):
                entries.append(seq)
        # Erased OTA metadata defaults to app0; valid selection must also be app0.
        require((not entries and prefix[0xe000:0x10000] == b'\xff' * 8192)
                or (entries and (max(entries)-1) % 2 == 0), 'Active OTA slot is not app0')

def healthy(lines, target):
    pattern = re.compile(r'RTE_HEARTBEAT version=1\.0\.0 target=' + re.escape(target)
        + r' mac=' + re.escape(BOARDS[target][0])
        + r' sequence=(\d+) uptime_ms=(\d+) heap=(\d+) app=0x10000$')
    values = [tuple(map(int, m.groups())) for line in lines if (m := pattern.fullmatch(line))]
    require(len(values) >= 3, 'Missing three target-specific healthy heartbeats')
    require(all(b[0] > a[0] and b[1] > a[1] for a,b in zip(values, values[1:])) and all(x[2] > 0 for x in values),
            'Heartbeat restarted, stalled, or has invalid heap')
    return {'count': len(values), 'last_sequence': values[-1][0], 'last_uptime_ms': values[-1][1]}

def x4_diagnostics(lines):
    """Persist enums/numeric boot facts only, never arbitrary serial strings."""
    providers = ('platform-clock-v1','x4pro-panel','x4pro-buttons','x4pro-frontlight',
                 'x4pro-sd','x4pro-i2c','x4pro-gt911')
    phases = ('elf-relocate-begin','elf-relocation-failed','elf-relocated',
              'hardware-start-begin','hardware-started','elf-interface-or-identity',
              'elf-entry-symbol-missing','invalid-provider-import')
    facts = {'loaded': [], 'failed': [], 'provider_phases': [],
             'storage_mounted': [], 'ready': [], 'home_present': []}
    for provider in providers:
        if any('[X4] loaded '+provider+' ' in row for row in lines): facts['loaded'].append(provider)
        if any('[X4] '+provider+' failed:' in row for row in lines): facts['failed'].append(provider)
        for phase in phases:
            if any('PROVREF id='+provider+' '+kind+'='+phase in row
                   for row in lines for kind in ('stage','failure')):
                facts['provider_phases'].append([provider,phase])
    for name, prefix in [('storage_mounted','storage.volume mounted='),
                         ('ready','heartbeat ready='),('home_present','home present=')]:
        facts[name] = [int(m.group(1)) for row in lines
                       if (m := re.search(r'\[X4\] '+re.escape(prefix)+r'([01])(?:\s|$)',row))]
    reasons = ('CMD0 send failed','CMD8 no response','CMD8 response invalid',
               'ACMD41 failed','CMD2 failed','CMD3 failed','CMD7 select failed',
               'CMD16 block size failed','card idle','FAT32 volume absent',
               'sector 0 read failed','sector 0 signature invalid','FAT32 boot invalid')
    facts['storage_errors'] = [reason for reason in reasons
                              if any('reason='+reason in row for row in lines)]
    return facts

def candidate_result(lines, target):
    require(not any(x in line for line in lines for x in ('Guru Meditation','Backtrace:','abort()','Task watchdog','task_wdt')), 'Candidate panic/watchdog')
    if target == 'cam-nosd':
        attempts = [line for line in lines if line.startswith('RTE_NOSD ')]
        require(len(attempts) == 1 and re.fullmatch(r'RTE_NOSD mount_attempted=1 mounted=0 error=-?\d+', attempts[0]), 'Real absent-card mount not proved')
        idle = [line for line in lines if 'heartbeat state=Idle' in line and 'handles=0 has_grants=0' in line]
        require(len(idle) >= 3 and not any('state=Running' in line for line in lines), 'No-storage runtime failed to remain safely Idle')
        return {'mount_attempted': True, 'storage_absent': True, 'idle_heartbeats': len(idle)}
    if target == 'cam-sd':
        sys.path.insert(0, str(Path(__file__).parent / 'cam'))
        from ci_device import validate
        return validate(lines)
    sys.path.insert(0, str(Path(__file__).parent / 'x4'))
    from x4_ci_device import validate_boot
    result = validate_boot(lines)
    facts = x4_diagnostics(lines)
    require(len(facts['loaded']) == 7 and not facts['failed'], 'X4 required provider startup missing')
    require(sum('input.touch ready=1' in row for row in lines) == 1, 'X4 touch provider startup missing')
    ready = facts['ready']
    require(1 in ready and all(value == 1 for value in ready[ready.index(1):]), 'X4 readiness regressed')
    return dict(result, required_providers=7, touch_provider_ready=True)

class Transport:
    def __init__(self, binding, target, folder):
        self.binding, self.target, self.folder = binding, target, folder
        self.mac = BOARDS[target][0]
        self.serial_bytes = 0

    def port(self):
        from serial.tools import list_ports
        vid, pid = BOARDS[self.target][3:]
        matches = [p for p in list_ports.comports() if (p.vid,p.pid,p.location) == (vid,pid,self.binding['location'])
                   and (self.target != 'x4' or (p.serial_number or '').lower() == self.mac)]
        require(len(matches) == 1, 'Mapped USB identity absent or ambiguous')
        if self.target != 'x4':
            require(matches[0].device == self.binding['port'], 'CAM physical port changed')
        return matches[0].device

    def command(self, label, *args, timeout=180, reset=False):
        port = self.port()
        command = [sys.executable, str(Path(__file__).with_name('esptool_transport.py')), '--expected-mac', self.mac, '--chip', 'esp32s3', '--port', port,
                   '--baud', '115200', '--connect-attempts', '1', '--before', 'default-reset',
                   '--after', 'hard-reset' if reset else 'no-reset', *map(str,args)]
        print(label, flush=True)
        done = subprocess.run(command, capture_output=True, timeout=timeout)
        require(len(done.stdout) + len(done.stderr) <= 2_000_000, 'Tool output bound exceeded')
        if done.returncode:
            # esptool diagnostics only, never running-firmware serial content.
            (self.folder / (label + '.log')).write_bytes(done.stdout + done.stderr)
            raise RuntimeError(f'{label}: esptool exit {done.returncode}; see private diagnostic log')
        return done.stdout.decode('utf-8', 'replace')

    def identity(self):
        output = self.command('identify', 'read-mac')
        require(self.mac in output.lower(), 'Chip MAC mismatch; no writes')

    def read(self, label, address, length):
        path = self.folder / (label + '.bin')
        require(not path.exists(), 'Evidence filename already used')
        self.command(label, 'read-flash', hex(address), hex(length), path, timeout=600)
        data = path.read_bytes()
        require(len(data) == length, 'Flash read truncated')
        path.unlink()
        return data

    def write(self, label, path):
        self.identity()
        self.command(label, 'write-flash', '--flash-mode', 'keep', '--flash-freq', 'keep', '--flash-size', 'keep', '0x10000', path, timeout=600)
        self.command(label + '-verify', 'verify-flash', '0x10000', path, timeout=600)

    def boot(self, seconds):
        self.command('boot', 'read-mac', reset=True)
        deadline = time.monotonic() + 12
        while True:
            try:
                port = self.port()
                break
            except RuntimeError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.2)
        return self.observe(seconds)

    def observe(self, seconds):
        import serial
        port = self.port()
        pending, lines, count = bytearray(), [], 0
        handle = serial.Serial(port=None, baudrate=115200, timeout=.5, write_timeout=5, exclusive=True)
        handle.dtr = handle.rts = False
        handle.port = port
        with handle:
            # macOS may take several seconds to open a re-enumerated CDC
            # interface. Give the requested sampling window to actual reads.
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                require(self.port() == port, 'USB changed during boot observation')
                chunk = handle.read(2048)
                count += len(chunk)
                require(count <= 1_048_576, 'Boot output exceeds byte bound')
                pending.extend(chunk)
                while b'\n' in pending:
                    row, _, pending = pending.partition(b'\n')
                    row = row.decode('utf-8', 'replace').strip()
                    require(len(row) <= 2048 and len(lines) < 2000, 'Boot output exceeds line bound')
                    if any(s in row for s in ('RTE_HEARTBEAT ', 'RTE_BOOT ', 'RTE_NOSD ', 'RUNTIME BOOT ', 'RUNTIME APP ', 'CAMERA_APP saved=', '[X4]', 'PROVREF ', 'Guru Meditation','Backtrace:','abort()','Task watchdog','task_wdt')):
                        lines.append(row)
                require(len(pending) <= 2048, 'Unterminated boot line exceeds bound')
        self.serial_bytes += count
        return lines

def transaction(target, binding, heartbeat, heartbeat_sha, folder, candidate=None, candidate_sha=None, candidate_profile="reader"):
    require(candidate_profile in ('reader','runtime-heartbeat') and (candidate_profile == 'reader' or target in ('cam-nosd','x4')), 'Unknown candidate profile')
    require(target in BOARDS and binding['mac'].lower() == BOARDS[target][0], 'Unqualified target identity')
    hb = image_bytes(heartbeat, heartbeat_sha, target, True)
    app = image_bytes(candidate, candidate_sha, target) if candidate else None
    folder.mkdir(mode=0o700, parents=True, exist_ok=False)
    frozen = folder / 'heartbeat.bin'; frozen.write_bytes(hb)
    frozen_candidate = folder / 'candidate.bin'
    if app: frozen_candidate.write_bytes(app)
    result = {'target': target, 'mac': BOARDS[target][0], 'heartbeat_sha256': heartbeat_sha,
              'candidate_sha256': candidate_sha, 'result': 'failed', 'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
    transport = Transport(binding,target,folder)
    # A disconnected target cannot be selected by falling back to another port.
    port = transport.port()
    cleanup_allowed = False
    try:
        with locks(port, BOARDS[target][0]):
            try:
                transport.identity()
                prefix = transport.read('protected-before',0,0x10000)
                length = (max(len(hb),len(app or b'')) + 4095) // 4096 * 4096
                check_layout(prefix,target,length)
                result['protected_sha256'] = digest(prefix)
                cleanup_allowed = True
                save(folder/'result.json',result)
                if app:
                    transport.write('candidate', frozen_candidate)
                    result['candidate_readback_equal'] = True
                    boot_lines = transport.boot(180 if target=='cam-sd' else 45)
                    if target == 'x4': result['boot_diagnostics'] = x4_diagnostics(boot_lines)
                    if candidate_profile == 'runtime-heartbeat':
                        require(not any(token in line for line in boot_lines for token in ('Guru Meditation','Backtrace:','abort()','Task watchdog','task_wdt')), 'Runtime panic/watchdog')
                        result['candidate_checks'] = healthy(boot_lines,target)
                    else:
                        result['candidate_checks'] = candidate_result(boot_lines,target)
                result['result'] = 'pass'
            except BaseException as error:
                result['error'] = f'{type(error).__name__}: {error}'[:400]
                result['result'] = 'failed'
            finally:
                if cleanup_allowed:
                    try:
                        # Cleanup is mandatory even when a candidate assertion fails.
                        transport.write('heartbeat-cleanup', frozen)
                        after = transport.read('protected-after',0,0x10000)
                        require(after == prefix,'Protected boot/table/NVS/OTA region changed')
                        result['protected_equal'] = True
                        result['heartbeat_readback_equal'] = True
                        result['heartbeat_health'] = healthy(transport.boot(10),target)
                        result['heartbeat_restored'] = True
                    except BaseException as error:
                        result['cleanup_error'] = f'{type(error).__name__}: {error}'[:400]
                        result['result'] = 'failed'
    finally:
        result['serial_bytes'] = transport.serial_bytes
        result['finished_utc'] = time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        save(folder/'result.json',result)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',choices=BOARDS,required=True)
    p.add_argument('--binding',type=Path,required=True)
    p.add_argument('--heartbeat',type=Path,required=True)
    p.add_argument('--heartbeat-sha256',required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--candidate',type=Path)
    p.add_argument('--candidate-sha256')
    a=p.parse_args()
    require(bool(a.candidate)==bool(a.candidate_sha256),'Candidate and hash required together')
    require(a.binding.is_file() and not a.binding.is_symlink() and a.binding.stat().st_size<2048 and not a.binding.stat().st_mode&0o077,'Binding must be private regular JSON')
    result=transaction(a.target,json.loads(a.binding.read_text()),a.heartbeat,a.heartbeat_sha256,a.out,a.candidate,a.candidate_sha256)
    print(json.dumps(result,indent=2))
    return 0 if result['result']=='pass' and result.get('heartbeat_restored') else 1

if __name__=='__main__':
    sys.exit(main())
