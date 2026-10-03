#!/usr/bin/env python3
"""Stage the frozen X4 SD image on an explicitly named, offline macOS SD card.

Default is read-only preflight. --apply requires --card-offline and a fresh,
explicit --backup-dir on an internal host filesystem. Remove the card from the
powered-off X4 and close apps using it before running this helper. This is NOT
an on-device package installer, firmware flasher or hardware qualification.

Only the 50 pinned inventory paths are published. Existing matching files and
all unrelated contents are preserved. Conflicts are durably backed up before
ANY card write. Payloads precede package manifests; board/boot profiles are
last. Writes touch only declared files and their necessary parent directories;
there are no temporary SD files. Publication is NOT atomic: interruption can
leave a partial file and mixed generation. Do not boot that card until repaired. No automatic rollback, deletion, formatting, unmount or raw device I/O.

Progress is JSON on stderr; preflight/final receipt is JSON on stdout. Apply
also writes a durable journal/receipt in the requested host backup directory.
Bounds: 64 KiB chunks, <=50 files, <=8 MiB artifact, <=4 MiB SD expansion,
<=128 MiB old files, <=8192 directory entries, 15 min overall, 30 s per I/O,
no retries. Python 3.8+ must support descriptor-relative filesystem APIs.
Directory locking/scanning/fsync and macOS F_FULLFSYNC are required, with no
silent durability fallback. Unsupported filesystems/readers fail closed; a
failure after a selected-file write requires recovery from the host backup.
Flush/readback is OS-reported evidence, not proof against failing hardware.
POSIX alarm interrupts cancellable host calls; an uninterruptible
kernel/storage fault may require OS recovery. SIGKILL/power loss cannot emit a
final receipt: consult the pre-mutation host journal and keep the card offline.
"""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import signal
import stat
import subprocess
import sys
import time
import unicodedata
import zipfile

EXPECTED_SOURCE_SHA = '67d0fd3e9f4012eae681e7d284d2d0bb39732dfb'
EXPECTED_ARTIFACT_SHA256 = 'ca478cf829e2c78b81f3a4f80134119522d4863ea10404d06f6b006adf180efa'
EXPECTED_MANIFEST_SHA256 = '0d8763f1c2c356d31fab8c78d5db227aefe58475cba12d49af1887504301c320'
EXPECTED_SD_SHA256 = 'f55daa10cdcc67eceba27b96e1ff17c4e9acf9c75ad55062bc72954d4332a8d8'
FILE_COUNT, PACKAGE_COUNT = 50, 8
CHUNK = 64 * 1024
MAX_ARTIFACT, MAX_SD, MAX_FILE = 8 * 1024**2, 4 * 1024**2, 1024**2
MAX_OLD_FILE, MAX_BACKUP, MAX_ENTRIES = 16 * 1024**2, 128 * 1024**2, 8192
DEADLINE_SECONDS, IO_SECONDS = 900, 30
DIRECTORY_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
READ_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
CREATE_FLAGS = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def digest(data, budget=None):
    hashed = hashlib.sha256()
    for offset in range(0, len(data), CHUNK):
        hashed.update(data[offset:offset + CHUNK])
        if budget is not None:
            budget.check(min(CHUNK, len(data) - offset))
    return {'bytes': len(data), 'sha256': hashed.hexdigest()}


def folded(name):
    return unicodedata.normalize('NFD', name).casefold()


def safe_name(name):
    require(isinstance(name, str) and 0 < len(name.encode('utf-8')) <= 255,
            'Invalid inventory path')
    require(all(32 <= ord(c) < 127 for c in name) and '\\' not in name and ':' not in name,
            'Unsafe path characters')
    require(all(p not in ('', '.', '..') and not p.endswith((' ', '.'))
                for p in name.split('/')), 'Unsafe relative path')
    return name


def absolute_path(value, *, missing_leaf=False):
    raw = os.fspath(value)
    path = Path(raw)
    require(path.is_absolute() and raw == str(path) and '..' not in path.parts,
            'An explicit normalized absolute path is required')
    current = Path(path.anchor)
    for index, part in enumerate(path.parts[1:]):
        current /= part
        try:
            st = current.lstat()
        except FileNotFoundError:
            require(missing_leaf and index == len(path.parts) - 2,
                    'Path parent does not exist')
            return path
        require(not stat.S_ISLNK(st.st_mode), 'Symlink path refused: ' + str(current))
    return path


class Budget:
    def __init__(self, progress=None, seconds=DEADLINE_SECONDS):
        self.start = self.last_yield = self.last_report = time.monotonic()
        self.deadline = self.start + seconds
        self.bytes = self.items = self.since_yield = 0
        self.stage = 'preflight'
        self.progress = progress or (lambda value: print(json.dumps(value), file=sys.stderr, flush=True))

    def check(self, count=0, *, item=False, stage=None):
        now = time.monotonic()
        require(now < self.deadline, 'Operation deadline exceeded; keep the card offline')
        self.bytes += count
        self.items += int(item)
        self.since_yield += count
        transition = stage is not None and stage != self.stage
        if stage is not None:
            self.stage = stage
        if self.since_yield >= 4 * CHUNK or now - self.last_yield >= .05 or item:
            time.sleep(0)  # Actual scheduler yield on byte/item AND elapsed checkpoints.
            self.last_yield, self.since_yield = now, 0
        if transition or now - self.last_report >= 1:
            self.progress({'stage': self.stage, 'io_bytes': self.bytes, 'items': self.items,
                           'elapsed_seconds': round(now - self.start, 2)})
            self.last_report = now

    def io(self, function, *args, **kwargs):
        self.check()
        def timeout(_signum, _frame):
            raise TimeoutError('Host I/O deadline exceeded in ' + self.stage)
        previous = signal.signal(signal.SIGALRM, timeout)
        signal.setitimer(signal.ITIMER_REAL, min(IO_SECONDS, self.deadline - time.monotonic()))
        try:
            return function(*args, **kwargs)
        finally:
            signal.setitimer(signal.ITIMER_REAL, 0)
            signal.signal(signal.SIGALRM, previous)
            self.check()


def read_fd(fd, limit, budget):
    result = bytearray()
    while True:
        block = budget.io(os.read, fd, min(CHUNK, limit + 1 - len(result)))
        if not block:
            break
        result.extend(block)
        require(len(result) <= limit, 'File exceeds byte bound')
        budget.check(len(block))
    return bytes(result)


def read_file(path, limit, budget):
    path = absolute_path(path)
    fd = budget.io(os.open, path, READ_FLAGS)
    try:
        st = os.fstat(fd)
        require(stat.S_ISREG(st.st_mode) and st.st_nlink == 1 and st.st_size <= limit,
                'Source must be a bounded, non-linked regular file')
        return read_fd(fd, limit, budget)
    finally:
        os.close(fd)


def unique_json(raw):
    require(len(raw) <= 65536, 'Manifest exceeds JSON bound')
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=pairs)


def zip_inventory(raw, max_entries, max_file, max_total, budget):
    result, names, total = {}, set(), 0
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        records = archive.infolist()
        require(len(records) <= max_entries, 'ZIP entry bound exceeded')
        for record in records:
            name = safe_name(record.filename)
            key = folded(name)
            require(key not in names, 'Duplicate/case-colliding ZIP path')
            names.add(key)
            mode = stat.S_IFMT(record.external_attr >> 16)
            require(not record.is_dir() and mode in (0, stat.S_IFREG)
                    and not record.flag_bits & 1, 'ZIP special/link/directory/encrypted entry refused')
            require(record.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED),
                    'Unsupported ZIP compression')
            require(0 <= record.file_size <= max_file, 'ZIP file exceeds bound')
            total += record.file_size
            require(total <= max_total, 'ZIP expansion exceeds bound')
            chunks, size = [], 0
            with archive.open(record) as stream:
                while True:
                    chunk = budget.io(stream.read, CHUNK)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    size += len(chunk)
                    require(size <= record.file_size, 'ZIP length mismatch')
                    budget.check(len(chunk))
            require(size == record.file_size, 'ZIP short read')
            result[name] = b''.join(chunks)
            budget.check(item=True)
    for name in names:
        require(not any(str(parent) in names for parent in PurePosixPath(name).parents
                        if str(parent) != '.'), 'ZIP file/directory collision')
    return result


def load_source(artifact, budget):
    raw = read_file(artifact, MAX_ARTIFACT, budget)
    require(digest(raw, budget)['sha256'] == EXPECTED_ARTIFACT_SHA256, 'Frozen artifact SHA-256 mismatch')
    outer = zip_inventory(raw, 13, MAX_ARTIFACT, 12 * 1024**2, budget)
    manifest_raw, sd = outer['deployment.json'], outer['sdcard.zip']
    require(digest(manifest_raw, budget)['sha256'] == EXPECTED_MANIFEST_SHA256,
            'Frozen deployment manifest SHA-256 mismatch')
    require(digest(sd, budget)['sha256'] == EXPECTED_SD_SHA256, 'Pinned sdcard.zip SHA-256 mismatch')
    manifest = unique_json(manifest_raw)
    require(type(manifest.get('schema')) is int and manifest['schema'] == 2
            and manifest.get('board') == 'xteink-x4-pro'
            and manifest.get('source_sha') == EXPECTED_SOURCE_SHA
            and manifest.get('driver_medium') == 'sd' and manifest.get('sd_root') == 'sdcard'
            and manifest.get('provisioning_authorized') is False,
            'Unexpected schema/source/board/medium or artifact authorization')
    require(manifest['sd_archive'] == {'file': 'sdcard.zip', **digest(sd, budget)},
            'SD archive metadata mismatch')
    inventory = manifest['files']
    require(isinstance(inventory, dict) and len(inventory) == FILE_COUNT,
            'Exactly 50 manifest-listed SD files required')
    decoded = zip_inventory(sd, FILE_COUNT, MAX_FILE, MAX_SD, budget)
    files = {}
    for name, expected in inventory.items():
        safe_name(name)
        require(name.startswith(('Drivers/', 'Packages/Inbox/')) or
                name in ('System/Config/board.json', 'System/Config/boot.json'),
                'Unexpected SD inventory namespace')
        require('sdcard/' + name in decoded and digest(decoded['sdcard/' + name], budget) == expected,
                'SD manifest content mismatch: ' + name)
        files[name] = decoded['sdcard/' + name]
    require(set(decoded) == {'sdcard/' + name for name in files}, 'Unexpected SD ZIP inventory')
    require(sum(name.endswith('/.package.json') for name in files) == PACKAGE_COUNT,
            'All eight hidden package manifests are required')
    return manifest, files


def disk_info(target, budget):
    done = budget.io(subprocess.run, ['/usr/sbin/diskutil', 'info', '-plist', str(target)],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10, check=False)
    require(done.returncode == 0 and len(done.stdout) <= 65536, 'diskutil info failed/oversized')
    info = plistlib.loads(done.stdout)
    require(isinstance(info, dict), 'Invalid diskutil response')
    return info


def validate_mount_info(volume, part, disk):
    require(part.get('Mounted') is True and part.get('MountPoint') == str(volume),
            'Target is not the exact mounted volume root')
    identifier, whole = part.get('DeviceIdentifier', ''), part.get('ParentWholeDisk', '')
    require(re.fullmatch(r'disk[1-9][0-9]*s[1-9][0-9]*', identifier) is not None
            and re.fullmatch(r'disk[1-9][0-9]*', whole) is not None
            and identifier.startswith(whole + 's') and disk.get('DeviceIdentifier') == whole,
            'Target must be a physical, non-system SD partition')
    require(part.get('Internal') is False and disk.get('Internal') is False
            and disk.get('VirtualOrPhysical') == 'Physical' and disk.get('Whole') is True,
            'Internal/virtual/unverified physical storage refused')
    require(disk.get('RemovableMedia') is True and disk.get('Ejectable') is True,
            'Target must be removable and ejectable')
    require(part.get('Writable') is True and part.get('VolumeReadOnly', False) is False
            and disk.get('MediaReadOnly', False) is False, 'Read-only/unverified writable media')
    # The current X4 bootstrap is FAT32; accepting exFAT here would promise an unusable card.
    require(part.get('FilesystemType') == 'msdos' and
            'FAT32' in str(part.get('FilesystemName', '')).upper(), 'X4 requires an existing FAT32 volume')
    media = ' '.join(str(disk.get(k, '')) for k in
                     ('MediaName', 'DeviceName', 'BusProtocol', 'IORegistryEntryName'))
    require(re.search(r'(?i)(secure digital|\bsd(?:hc|xc)?\b|\bmmc\b|card.?reader)', media) is not None,
            'diskutil does not positively identify SD-like media')
    require(disk.get('BusProtocol') in ('USB', 'Secure Digital'), 'Unsupported SD transport')
    require(type(part.get('TotalSize')) is int and 128 * 1024**2 <= part['TotalSize'] <= 2 * 1024**4,
            'Implausible SD volume capacity')
    require(isinstance(part.get('VolumeUUID'), str) and part['VolumeUUID'], 'Volume UUID is required')
    return {k: part[k] for k in ('DeviceIdentifier', 'ParentWholeDisk', 'VolumeUUID', 'MountPoint',
                               'TotalSize', 'FilesystemType', 'FilesystemName')}


def verify_volume(volume, budget):
    require(sys.platform == 'darwin', 'Physical SD staging is supported only on macOS')
    volume = absolute_path(volume)
    require(volume.parent == Path('/Volumes') and volume.name not in ('', 'Macintosh HD')
            and volume.is_dir() and os.path.ismount(volume),
            'Specify an actual SD mount root immediately under /Volumes')
    part = disk_info(volume, budget)
    disk = disk_info('/dev/' + str(part.get('ParentWholeDisk', 'invalid')), budget)
    return validate_mount_info(volume, part, disk)


def verify_backup(path, volume, budget):
    path = absolute_path(path, missing_leaf=True)
    require(not path.exists() and path.parent.is_dir(), 'Backup directory must be fresh with an existing parent')
    require(path != volume and volume not in path.parents and path.parent.stat().st_dev != volume.stat().st_dev,
            'Backup must be off the SD card')
    host = disk_info(path.parent, budget)
    require(host.get('Internal') is True and host.get('Writable') is True,
            'Backup parent must be a writable internal host volume')
    return path


def signature(st):
    return (st.st_dev, st.st_ino, st.st_mode, st.st_size, st.st_mtime_ns, st.st_ctime_ns)


class Card:
    """Descriptor-relative access, no followed links or recursive tree overwrite."""
    def __init__(self, root, budget):
        self.root, self.budget = Path(root), budget
        require(os.scandir in os.supports_fd, 'Python lacks descriptor-based scandir; no unsafe fallback')
        self.fd = budget.io(os.open, root, DIRECTORY_FLAGS)
        self.device = os.fstat(self.fd).st_dev
        self.scanned = 0
        self.dirs = {'': self.fd}
        try:
            budget.io(fcntl.flock, self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BaseException:
            os.close(self.fd)
            raise

    def close(self):
        errors = []
        for fd in reversed(list(self.dirs.values())):
            try:
                os.close(fd)
            except OSError as error:
                errors.append(str(error))
        self.dirs.clear()
        if errors:
            raise OSError('Directory close failed: ' + '; '.join(errors))

    def attached(self):
        now = self.budget.io(os.stat, self.root, follow_symlinks=False)
        old = os.fstat(self.fd)
        require((now.st_dev, now.st_ino) == (old.st_dev, old.st_ino) and stat.S_ISDIR(now.st_mode),
                'Target mount changed/disappeared')

    def entries(self, fd):
        result = {}
        with self.budget.io(os.scandir, fd) as entries:
            while True:
                entry = self.budget.io(next, entries, None)
                if entry is None:
                    break
                self.scanned += 1
                require(self.scanned <= MAX_ENTRIES, 'Directory scan entry bound exceeded')
                key = folded(entry.name)
                require(key not in result, 'Filesystem case/normalization collision')
                result[key] = entry.name
                self.budget.check(item=True)
        return result

    def parent(self, name, *, create=False):
        self.attached()
        parts, current, fd = safe_name(name).split('/'), '', self.fd
        for component in parts[:-1]:
            key = component if not current else current + '/' + component
            try:
                st = self.budget.io(os.stat, component, dir_fd=fd, follow_symlinks=False)
            except FileNotFoundError:
                if not create:
                    return None
                self.budget.io(os.mkdir, component, 0o755, dir_fd=fd)
                self.budget.io(os.fsync, fd)
                st = self.budget.io(os.stat, component, dir_fd=fd, follow_symlinks=False)
            require(stat.S_ISDIR(st.st_mode) and st.st_dev == self.device,
                    'Target ancestor symlink/special/file/nested mount refused: ' + key)
            if key not in self.dirs:
                child = self.budget.io(os.open, component, DIRECTORY_FLAGS, dir_fd=fd)
                actual = os.fstat(child)
                if (actual.st_dev, actual.st_ino) != (st.st_dev, st.st_ino):
                    os.close(child)
                    raise ValueError('Target directory changed')
                self.dirs[key] = child
            child = self.dirs[key]
            require((st.st_dev, st.st_ino) == (os.fstat(child).st_dev, os.fstat(child).st_ino),
                    'Target directory changed since preflight')
            current, fd = key, child
        return fd

    def check_paths(self, files):
        # Scan only the <=15 directories addressed by the fixed inventory, once.
        expected = {}
        for name in files:
            parts = name.split('/')
            for n, component in enumerate(parts):
                expected.setdefault('/'.join(parts[:n]), set()).add(component)
        for parent, children in sorted(expected.items()):
            fd = self.parent(parent + '/probe' if parent else 'probe')
            if fd is None:
                continue
            entries = self.entries(fd)
            for child in children:
                require(folded(child) not in entries or entries[folded(child)] == child,
                        'Existing filesystem case collision: ' + child)

    @contextmanager
    def opened(self, name):
        parent = self.parent(name)
        if parent is None:
            yield None
            return
        try:
            fd = self.budget.io(os.open, name.split('/')[-1], READ_FLAGS, dir_fd=parent)
        except FileNotFoundError:
            yield None
            return
        try:
            st = os.fstat(fd)
            require(stat.S_ISREG(st.st_mode) and st.st_nlink == 1 and st.st_dev == self.device,
                    'Target symlink/hardlink/special/nested mount refused: ' + name)
            yield fd
        finally:
            os.close(fd)

    def snapshot(self, name):
        with self.opened(name) as fd:
            if fd is None:
                return None
            before = os.fstat(fd)
            require(before.st_size <= MAX_OLD_FILE, 'Existing target file exceeds backup bound')
            h, length = hashlib.sha256(), 0
            while True:
                chunk = self.budget.io(os.read, fd, CHUNK)
                if not chunk:
                    break
                length += len(chunk)
                require(length <= MAX_OLD_FILE, 'Existing target grew beyond bound')
                h.update(chunk)
                self.budget.check(len(chunk))
            require(signature(before) == signature(os.fstat(fd)) and length == before.st_size,
                    'Target changed during read: ' + name)
            self.budget.check(item=True)
            return {'bytes': length, 'sha256': h.hexdigest(), 'signature': list(signature(before))}


def content(snapshot):
    return None if snapshot is None else {k: snapshot[k] for k in ('bytes', 'sha256')}


def durable(fd, budget, *, directory=False):
    try:
        budget.io(os.fsync, fd)
        if sys.platform == 'darwin' and not directory:
            budget.io(fcntl.fcntl, fd, 51)  # F_FULLFSYNC asks the device to flush its cache.
    except OSError as error:
        raise OSError('Required fsync/F_FULLFSYNC durability operation failed; no fallback: ' + str(error)) from error


def write_all(fd, data, budget):
    view = memoryview(data)
    offset = 0
    while offset < len(view):
        count = budget.io(os.write, fd, view[offset:offset + CHUNK])
        require(count > 0, 'Short/zero write')
        offset += count
        budget.check(count)


def host_write(path, data, budget):
    absolute_path(path, missing_leaf=True)
    fd = budget.io(os.open, path, CREATE_FLAGS, 0o600)
    try:
        write_all(fd, data, budget)
        durable(fd, budget)
    finally:
        os.close(fd)
    parent = budget.io(os.open, path.parent, DIRECTORY_FLAGS)
    try:
        durable(parent, budget, directory=True)
    finally:
        os.close(parent)
    require(read_file(path, max(len(data), 1), budget) == data, 'Host durable write readback failed')


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def publish_order(name):
    if name == 'System/Config/boot.json':
        return (4, name)
    if name == 'System/Config/board.json':
        return (3, name)
    if name.endswith('/.package.json'):
        return (2, name)
    if name.endswith('/manifest.json'):
        return (1, name)
    return (0, name)


def stage(artifact, volume, *, apply=False, card_offline=False, backup_dir=None, budget=None):
    budget = budget or Budget()
    card, backup, journal_fd = None, None, None
    report = {'schema': 1, 'status': 'failure', 'mode': 'apply' if apply else 'preflight',
              'card_writes_started': False, 'confirmed_published': [],
              'pending_path': None, 'hardware_qualified': False, 'source_sha': EXPECTED_SOURCE_SHA,
              'sdcard_sha256': EXPECTED_SD_SHA256, 'hardware_validation': 'failed',
              'hardware_validation_reason': 'Unavailable: no physical candidate boot or device validation performed'}
    try:
        require(not apply or card_offline, '--apply requires --card-offline: removed from powered-off X4; close card-using apps')
        require(not apply or backup_dir is not None, '--apply requires an explicit fresh --backup-dir')
        manifest, files = load_source(artifact, budget)
        volume = Path(volume)
        identity = verify_volume(volume, budget)
        card = Card(volume, budget)
        report['target'] = identity
        card.check_paths(files)
        snapshots = {name: card.snapshot(name) for name in sorted(files)}
        desired = manifest['files']
        conflicts = [name for name in files if snapshots[name] is not None and content(snapshots[name]) != desired[name]]
        changes = sorted((name for name in files if content(snapshots[name]) != desired[name]), key=publish_order)
        backup_bytes = sum(snapshots[name]['bytes'] for name in conflicts)
        require(backup_bytes <= MAX_BACKUP, 'Total conflicting files exceed backup byte bound')
        space = budget.io(os.fstatvfs, card.fd)
        require(not changes or sum(len(files[name]) for name in changes) + MAX_FILE <=
                space.f_bavail * space.f_frsize, 'Insufficient SD space for selected-file staging')
        report.update({'file_count': len(files), 'hidden_package_manifests': PACKAGE_COUNT,
                       'conflicts': conflicts, 'publication_order': changes,
                       'unchanged': sorted(set(files) - set(changes)), 'backup_bytes': backup_bytes,
                       'planned_files': {name: {'before': content(snapshots[name]), 'desired': desired[name]}
                                         for name in sorted(files)}})
        if backup_dir is not None:
            backup_dir = verify_backup(Path(backup_dir), volume, budget)
        if not apply:
            require(verify_volume(volume, budget) == identity, 'SD identity changed during preflight')
            report['status'] = 'preflight-pass'
            report['all_50_matching'] = not changes
            if not changes:
                report['readback'] = {name: content(snapshots[name]) for name in sorted(files)}
                report['notice'] = ('All 50 installed files already match the pinned archive, including eight hidden manifests. '
                                    'No copy is needed. This read-only receipt is not firmware/boot/hardware qualification.')
            else:
                report['notice'] = 'Read-only only; --apply and --card-offline are required. No card/host files were written.'
            return report
        require(verify_volume(volume, budget) == identity, 'SD identity changed after preflight')
        budget.check(stage='host-backup')
        # The supplied directory is never reused or recursively overwritten.
        require(budget.io(os.stat, backup_dir.parent).st_dev != card.device, 'Backup parent moved onto target')
        free = budget.io(os.statvfs, backup_dir.parent)
        require(free.f_bavail * free.f_frsize >= backup_bytes + MAX_SD + MAX_FILE, 'Insufficient host backup space')
        budget.io(os.mkdir, backup_dir, 0o700)
        backup = backup_dir
        report['backup_directory'] = str(backup)
        host_write(backup / 'preflight.json', json_bytes(report), budget)
        journal_fd = budget.io(os.open, backup / 'journal.jsonl', CREATE_FLAGS, 0o600)
        def journal(event, **values):
            write_all(journal_fd, json_bytes({'event': event, **values}).replace(b'\n', b' ') + b'\n', budget)
            durable(journal_fd, budget)
        backup_records = {}
        for name in conflicts:
            require(card.snapshot(name) == snapshots[name], 'Conflict changed before backup: ' + name)
            with card.opened(name) as fd:
                require(fd is not None, 'Conflict disappeared before backup')
                data = read_fd(fd, MAX_OLD_FILE, budget)
            require(digest(data, budget) == content(snapshots[name]), 'Conflict changed while backing up')
            # Flat names are collision-free and never copy untrusted directory trees.
            filename = 'original-%02d.bin' % len(backup_records)
            host_write(backup / filename, data, budget)
            backup_records[name] = {'file': filename, **content(snapshots[name]),
                                    'original_stat': snapshots[name]['signature']}
        host_write(backup / 'backup-receipt.json', json_bytes({'target': identity, 'files': backup_records}), budget)
        parent_fd = budget.io(os.open, backup.parent, DIRECTORY_FLAGS)
        try:
            durable(parent_fd, budget, directory=True)
        finally:
            os.close(parent_fd)
        # Every original is rechecked after ALL backups are durably read back.
        require(verify_volume(volume, budget) == identity, 'SD identity changed before publication')
        for name in files:
            require(card.snapshot(name) == snapshots[name], 'Target changed after preflight: ' + name)
        journal('backups-verified', files=backup_records)
        # Fail before payload writes if this FAT mount rejects directory sync.
        for directory_fd in card.dirs.values():
            durable(directory_fd, budget, directory=True)
        budget.check(stage='publish-payloads-then-manifests')
        for name in changes:
            require(card.snapshot(name) == snapshots[name], 'Target changed before publication: ' + name)
            report['pending_path'] = name
            journal('publish-intent', path=name, desired=desired[name])
            report['card_writes_started'] = True
            parent = card.parent(name, create=True)
            leaf = name.split('/')[-1]
            before = snapshots[name]
            flags = CREATE_FLAGS if before is None else (os.O_WRONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            fd = budget.io(os.open, leaf, flags, 0o644, dir_fd=parent)
            try:
                opened = os.fstat(fd)
                require(stat.S_ISREG(opened.st_mode) and opened.st_nlink == 1
                        and opened.st_dev == card.device, 'Selected output is not a safe regular file')
                require(before is None or list(signature(opened)) == before['signature'],
                        'Selected output changed before truncation')
                card.parent(name)
                named = budget.io(os.stat, leaf, dir_fd=parent, follow_symlinks=False)
                require((named.st_dev, named.st_ino) == (opened.st_dev, opened.st_ino),
                        'Selected output replaced before truncation')
                # Direct writes are intentional: never create an extra SD path. Every
                # conflicting original is already verified in the durable host backup.
                budget.io(os.ftruncate, fd, 0)
                write_all(fd, files[name], budget)
                durable(fd, budget)
            finally:
                os.close(fd)
            durable(parent, budget, directory=True)
            require(content(card.snapshot(name)) == desired[name], 'Published file readback mismatch')
            report['confirmed_published'].append(name)
            report['pending_path'] = None
            journal('published-readback', path=name, desired=desired[name])
            budget.check(item=True)
        budget.check(stage='final-readback')
        require(verify_volume(volume, budget) == identity, 'SD identity changed before final readback')
        final = {name: content(card.snapshot(name)) for name in sorted(files)}
        require(final == desired, 'Final complete 50-file readback mismatch')
        report.update({'status': 'readback-pass', 'all_50_matching': True, 'readback': final,
                       'notice': 'All 50 files read back exactly. Safely eject using macOS before reinserting into powered-off X4. No firmware/boot/hardware qualification performed.'})
        journal('complete', file_count=FILE_COUNT)
    except (Exception, KeyboardInterrupt) as error:
        report['status'] = 'failure'
        report['error'] = str(error) or type(error).__name__
        report['recovery'] = ('Keep card offline. Retain host backup and journal; a mixed generation or partial selected file may remain. '
                              'No automatic rollback/deletion was attempted. Inspect the pending path and repair before booting.'
                              if report['card_writes_started'] else 'Card payloads were not modified by this run.')
    finally:
        close_errors = []
        for cleanup in ((lambda: os.close(journal_fd)) if journal_fd is not None else None,
                        card.close if card is not None else None):
            if cleanup is not None:
                try:
                    cleanup()
                except (Exception, KeyboardInterrupt) as error:
                    close_errors.append(str(error) or type(error).__name__)
        if close_errors:
            report['status'] = 'failure'
            report['close_errors'] = close_errors
            report['recovery'] = 'Close failed. Keep the card offline; preserve host backups and inspect the journal before proceeding.'
    if backup is not None:
        try:
            # A fresh recovery budget permits a truthful failure receipt after main deadline/cancel.
            host_write(backup / 'receipt.json', json_bytes(report), Budget(progress=lambda _value: None, seconds=60))
        except (Exception, KeyboardInterrupt) as error:
            report['receipt_error'] = str(error) or type(error).__name__
            report['status'] = 'failure'
            report['recovery'] = ('Final host receipt is unverified or incomplete; use this stdout report and the host journal. '
                                  'Keep the card offline until the failure is resolved.')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--artifact', required=True, type=Path, help='Frozen x4-sd-deployment-11276691611.zip')
    parser.add_argument('--volume', required=True, type=Path, help='Exact SD mount root, e.g. /Volumes/X4CARD; never auto-selected')
    parser.add_argument('--apply', action='store_true', help='Permit the reviewed 50-path copy after durable conflict backup')
    parser.add_argument('--card-offline', action='store_true', help='Confirm card is removed from powered-off X4 and other card-using apps are closed')
    parser.add_argument('--backup-dir', type=Path, help='Fresh directory under an existing internal-host directory, outside the card')
    args = parser.parse_args(argv)
    result = stage(args.artifact, args.volume, apply=args.apply, card_offline=args.card_offline, backup_dir=args.backup_dir)
    print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return 0 if result['status'] in ('preflight-pass', 'readback-pass') else 1


if __name__ == '__main__':
    raise SystemExit(main())
