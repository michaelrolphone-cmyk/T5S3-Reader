"""Host-only SD stager tests. No diskutil, mounts, serial ports or device writes."""
import contextlib
import io
import shlex
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import stage_x4_sd_card as s


def zipped(files):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return output.getvalue()


def fixture():
    files = {}
    for number in range(8):
        identity = 'driver-%d' % number
        for name in ('.package.json', 'manifest.json', 'driver.elf', 'provider-abi.v1', 'privileged-imports.v1'):
            files['Drivers/' + identity + '/' + name] = ('payload ' + identity + name).encode()
        files['Packages/Inbox/' + identity + '.rte.zip'] = b'package ' + identity.encode()
    files['System/Config/board.json'] = b'{"board_id":"xteink-x4-pro"}'
    files['System/Config/boot.json'] = b'{"board":"board.json"}'
    sd = zipped({'sdcard/' + name: data for name, data in files.items()})
    manifest = {'schema': 2, 'board': 'xteink-x4-pro', 'source_sha': s.EXPECTED_SOURCE_SHA,
                'provisioning_authorized': False, 'driver_medium': 'sd', 'sd_root': 'sdcard',
                'sd_archive': {'file': 'sdcard.zip', **s.digest(sd)},
                'files': {name: s.digest(data) for name, data in files.items()}}
    manifest_raw = json.dumps(manifest).encode()
    outer = zipped({'deployment.json': manifest_raw, 'sdcard.zip': sd})
    return files, manifest_raw, sd, outer


def media():
    part = {'Mounted': True, 'MountPoint': '/Volumes/X4CARD', 'DeviceIdentifier': 'disk4s1',
            'ParentWholeDisk': 'disk4', 'Internal': False, 'Writable': True,
            'VolumeReadOnly': False, 'FilesystemType': 'msdos', 'FilesystemName': 'MS-DOS FAT32',
            'TotalSize': 32 * 1024**3, 'VolumeUUID': '1234-5678'}
    disk = {'DeviceIdentifier': 'disk4', 'Internal': False, 'VirtualOrPhysical': 'Physical',
            'Whole': True, 'RemovableMedia': True, 'Ejectable': True, 'MediaReadOnly': False,
            'MediaName': 'SD Card Reader', 'BusProtocol': 'USB'}
    return part, disk


class MountValidationTest(unittest.TestCase):
    def test_valid_physical_fat32_card(self):
        part, disk = media()
        self.assertEqual(s.validate_mount_info(Path('/Volumes/X4CARD'), part, disk)['DeviceIdentifier'], 'disk4s1')

    def test_internal_virtual_system_nonremovable_and_wrong_filesystems_fail(self):
        cases = [('part', 'Internal', True), ('disk', 'Internal', True),
                 ('disk', 'VirtualOrPhysical', 'Virtual'), ('disk', 'Whole', False),
                 ('disk', 'RemovableMedia', False), ('disk', 'Ejectable', False),
                 ('part', 'Mounted', False), ('part', 'MountPoint', '/'),
                 ('part', 'Writable', False), ('part', 'VolumeReadOnly', True),
                 ('disk', 'MediaReadOnly', True), ('part', 'FilesystemType', 'exfat'),
                 ('part', 'FilesystemName', 'MS-DOS FAT16'), ('part', 'VolumeUUID', ''),
                 ('part', 'DeviceIdentifier', 'disk0s1'), ('disk', 'MediaName', 'USB Hard Disk'),
                 ('disk', 'BusProtocol', 'Apple Fabric'), ('part', 'TotalSize', 4096)]
        for side, key, value in cases:
            with self.subTest(side=side, key=key, value=value):
                part, disk = media()
                (part if side == 'part' else disk)[key] = value
                with self.assertRaises(ValueError):
                    s.validate_mount_info(Path('/Volumes/X4CARD'), part, disk)

    def test_mac_volume_root_and_real_mount_required(self):
        budget = s.Budget(progress=lambda _: None)
        with patch.object(s.sys, 'platform', 'darwin'), patch.object(s, 'absolute_path', side_effect=lambda p: Path(p)), \
                patch.object(Path, 'is_dir', return_value=True), patch.object(s.os.path, 'ismount', return_value=True), \
                patch.object(s, 'disk_info') as info:
            for path in ('/', '/Users/me', '/Volumes', '/Volumes/X4CARD/subdirectory', '/Volumes/Macintosh HD'):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    s.verify_volume(Path(path), budget)
            info.assert_not_called()
            with patch.object(s.os.path, 'ismount', return_value=False), self.assertRaises(ValueError):
                s.verify_volume(Path('/Volumes/X4CARD'), budget)

    def test_linux_has_no_mount_bypass(self):
        with patch.object(s.sys, 'platform', 'linux'), patch.object(s, 'disk_info') as info:
            with self.assertRaisesRegex(ValueError, 'only on macOS'):
                s.verify_volume(Path('/Volumes/X4CARD'), s.Budget(progress=lambda _: None))
            info.assert_not_called()

    def test_symlink_and_dot_traversal_paths_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'real').mkdir()
            (root / 'alias').symlink_to(root / 'real', target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'Symlink'):
                s.absolute_path(root / 'alias')
            for path in ('relative', str(root) + '/../escape'):
                with self.assertRaises(ValueError):
                    s.absolute_path(path)

    def test_on_card_and_symlink_backup_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'off the SD'):
                s.verify_backup(root / 'backup', root, s.Budget(progress=lambda _: None))
            (root / 'link').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'Symlink'):
                s.verify_backup(root / 'link' / 'backup', root, s.Budget(progress=lambda _: None))


class StageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.card = self.root / 'card'
        self.card.mkdir()
        self.backup = self.root / 'backup'
        self.artifact = self.root / 'artifact.zip'
        self.files, self.manifest_raw, self.sd, outer = fixture()
        self.artifact.write_bytes(outer)
        self.events = []
        self.budget = s.Budget(progress=self.events.append)
        for name, value in [('EXPECTED_ARTIFACT_SHA256', s.digest(outer)['sha256']),
                            ('EXPECTED_MANIFEST_SHA256', s.digest(self.manifest_raw)['sha256']),
                            ('EXPECTED_SD_SHA256', s.digest(self.sd)['sha256'])]:
            patcher = patch.object(s, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.identity = {'DeviceIdentifier': 'disk4s1', 'VolumeUUID': '1234-5678', 'MountPoint': str(self.card)}
        for name, mock in [('verify_volume', lambda *_: self.identity),
                           ('verify_backup', lambda path, *_: path)]:
            patcher = patch.object(s, name, mock)
            patcher.start()
            self.addCleanup(patcher.stop)
        # Temp dirs share a Linux test filesystem; production requires off-card storage.
        self.real_stat = s.os.stat
        self.real_host_write = s.host_write

    def invoke(self, apply=False, **kwargs):
        if apply:
            kwargs.setdefault('card_offline', True)
            kwargs.setdefault('backup_dir', self.backup)
        # Only bypass the explicit host-vs-card st_dev check for synthetic fixtures.
        # The check uses parent.stat; return a distinct device for that one call while
        # preserving real st_dev checks in descriptor-relative card operations.
        def mocked_stat(path, *args, **kwargs):
            result = self.real_stat(path, *args, **kwargs)
            if Path(path) == self.backup.parent and not args and not kwargs:
                values = list(result)
                values[2] = result.st_dev + 1
                return os.stat_result(values)
            return result
        with patch.object(s.os, 'stat', side_effect=mocked_stat):
            return s.stage(self.artifact, self.card, apply=apply, budget=self.budget, **kwargs)

    def existing(self, name, data):
        path = self.card / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def assert_50(self):
        for name, data in self.files.items():
            self.assertEqual((self.card / name).read_bytes(), data, name)

    def test_documented_read_only_command_accepts_its_literal_artifact_path(self):
        readme=(Path(__file__).parent/'X4_MANUAL_SD_STAGING.txt').read_text()
        commands=[line for line in readme.splitlines() if line.startswith('python3 stage_x4_sd_card.py ')]
        self.assertEqual(len(commands),2)
        named=self.root/'x4-sd-deployment-11276691611.zip'
        named.write_bytes(self.artifact.read_bytes())
        with patch.dict(os.environ,{'PWD':str(self.root)}):
            parsed=[[os.path.expandvars(arg) for arg in shlex.split(line)[2:]] for line in commands]
        for args in parsed:
            self.assertEqual(s.absolute_path(args[args.index('--artifact')+1]),named)
        args=parsed[0]
        args[args.index('--volume')+1]=str(self.card)  # The documented user substitution.
        output=io.StringIO()
        with contextlib.redirect_stdout(output),contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(s.main(args),0)
        result=json.loads(output.getvalue())
        self.assertEqual(result['status'],'preflight-pass')
        self.assertFalse(result['card_writes_started'])
        self.assertFalse(self.backup.exists())

    def test_default_is_read_only_preflight(self):
        name = next(iter(self.files))
        self.existing(name, b'old')
        result = self.invoke()
        self.assertEqual(result['status'], 'preflight-pass', result)
        self.assertEqual(result['file_count'], 50)
        self.assertEqual(result['hidden_package_manifests'], 8)
        self.assertEqual(result['conflicts'], [name])
        self.assertEqual((self.card / name).read_bytes(), b'old')
        self.assertFalse(self.backup.exists())
        self.assertFalse(result['card_writes_started'])

    def test_explicit_apply_offline_and_backup_are_required(self):
        for options in ({'apply': True}, {'apply': True, 'card_offline': True},
                        {'apply': True, 'backup_dir': self.backup}):
            with self.subTest(options=options):
                result = s.stage(self.artifact, self.card, budget=self.budget, **options)
                self.assertEqual(result['status'], 'failure')
                self.assertEqual(list(self.card.iterdir()), [])
                self.assertFalse(self.backup.exists())

    def test_all_50_readback_hidden_files_unrelated_content_preserved(self):
        self.existing('Books/a.txt', b'user book')
        self.existing('Drivers/driver-0/custom.txt', b'user custom data')
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'readback-pass', result)
        self.assert_50()
        self.assertEqual(len(result['readback']), 50)
        self.assertEqual((self.card / 'Books/a.txt').read_bytes(), b'user book')
        self.assertEqual((self.card / 'Drivers/driver-0/custom.txt').read_bytes(), b'user custom data')
        self.assertEqual(result['confirmed_published'][-2:], ['System/Config/board.json', 'System/Config/boot.json'])
        self.assertFalse(any(path.name.startswith('.x4-stage-') for path in self.card.rglob('*')))
        self.assertEqual(json.loads((self.backup / 'receipt.json').read_text())['status'], 'readback-pass')
        self.assertTrue(self.events)

    def test_conflicts_are_all_durably_backed_up_before_first_mutation(self):
        names = [next(iter(self.files)), 'System/Config/boot.json']
        for i, name in enumerate(names):
            self.existing(name, ('original%d' % i).encode())
        actual_truncate = s.os.ftruncate
        def truncate(*args, **kwargs):
            saved = json.loads((self.backup / 'backup-receipt.json').read_text())['files']
            self.assertEqual(set(saved), set(names))
            for i, name in enumerate(names):
                self.assertEqual((self.backup / saved[name]['file']).read_bytes(), ('original%d' % i).encode())
            return actual_truncate(*args, **kwargs)
        with patch.object(s.os, 'ftruncate', side_effect=truncate):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'readback-pass', result)
        self.assert_50()

    def test_backup_failure_never_changes_card(self):
        name = next(iter(self.files))
        self.existing(name, b'original')
        def fail(path, data, budget):
            if path.name.startswith('original-'):
                raise OSError('simulated host backup failure')
            return self.real_host_write(path, data, budget)
        with patch.object(s, 'host_write', side_effect=fail):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertFalse(result['card_writes_started'])
        self.assertEqual((self.card / name).read_bytes(), b'original')
        self.assertEqual(len(list(self.card.rglob('*'))), 3)

    def test_interrupted_write_preserves_backup_and_reports_partial_selected_file(self):
        name = sorted(self.files, key=s.publish_order)[0]
        self.existing(name, b'old payload')
        real_write = s.write_all
        def interrupt(fd, data, budget):
            if bytes(data) == self.files[name]:
                os.write(fd, bytes(data)[:2])
                raise KeyboardInterrupt()
            return real_write(fd, data, budget)
        with patch.object(s, 'write_all', side_effect=interrupt):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure', result)
        self.assertEqual((self.card / name).read_bytes(), self.files[name][:2])
        self.assertEqual(result['pending_path'], name)
        saved = json.loads((self.backup / 'backup-receipt.json').read_text())['files'][name]
        self.assertEqual((self.backup / saved['file']).read_bytes(), b'old payload')
        self.assertEqual({p.relative_to(self.card).as_posix() for p in self.card.rglob('*') if p.is_file()}, {name})
        self.assertIn('Keep card offline', result['recovery'])
        self.assertTrue((self.backup / 'receipt.json').is_file())

    def test_interrupted_truncate_truthfully_reports_partial_publication(self):
        actual = s.os.ftruncate
        def interrupt(*args, **kwargs):
            actual(*args, **kwargs)
            raise KeyboardInterrupt()
        with patch.object(s.os, 'ftruncate', side_effect=interrupt):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure', result)
        self.assertTrue(result['card_writes_started'])
        self.assertIsNotNone(result['pending_path'])
        self.assertEqual(result['confirmed_published'], [])
        name = result['pending_path']
        self.assertEqual((self.card / name).read_bytes(), b'')
        self.assertFalse((self.card / 'System/Config/boot.json').exists())
        self.assertEqual({p.relative_to(self.card).as_posix() for p in self.card.rglob('*') if p.is_file()}, {name})
        events = [json.loads(line) for line in (self.backup / 'journal.jsonl').read_text().splitlines()]
        self.assertEqual(events[-1]['event'], 'publish-intent')

    def test_corrupt_archive_and_manifest_rejected_before_mutation(self):
        for data in (self.artifact.read_bytes() + b'corruption', b'not a zip'):
            self.artifact.write_bytes(data)
            result = self.invoke(apply=True)
            self.assertEqual(result['status'], 'failure')
            self.assertIn('SHA-256 mismatch', result['error'])
            self.assertFalse(self.backup.exists())
            self.assertEqual(list(self.card.iterdir()), [])

    def test_sd_pin_is_independently_verified(self):
        outer = zipped({'deployment.json': self.manifest_raw, 'sdcard.zip': self.sd + b'changed'})
        self.artifact.write_bytes(outer)
        with patch.object(s, 'EXPECTED_ARTIFACT_SHA256', s.digest(outer)['sha256']):
            result = self.invoke()
        self.assertEqual(result['status'], 'failure')
        self.assertIn('sdcard.zip SHA-256 mismatch', result['error'])

    def test_schema2_is_required_even_with_other_pins_matched(self):
        manifest = json.loads(self.manifest_raw)
        manifest['schema'] = 1
        raw = json.dumps(manifest).encode()
        outer = zipped({'deployment.json': raw, 'sdcard.zip': self.sd})
        self.artifact.write_bytes(outer)
        with patch.object(s, 'EXPECTED_ARTIFACT_SHA256', s.digest(outer)['sha256']), \
                patch.object(s, 'EXPECTED_MANIFEST_SHA256', s.digest(raw)['sha256']):
            result = self.invoke()
        self.assertEqual(result['status'], 'failure')
        self.assertIn('Unexpected schema', result['error'])

    def test_symlink_file_and_parent_rejected(self):
        outside = self.root / 'outside'
        outside.mkdir()
        (self.card / 'Drivers').symlink_to(outside, target_is_directory=True)
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure', result)
        self.assertEqual(list(outside.iterdir()), [])
        self.assertFalse(self.backup.exists())

    def test_symlink_target_file_rejected(self):
        name = next(iter(self.files))
        path = self.card / name
        path.parent.mkdir(parents=True)
        outside = self.root / 'untouched'
        outside.write_bytes(b'original')
        path.symlink_to(outside)
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertEqual(outside.read_bytes(), b'original')
        self.assertFalse(self.backup.exists())

    def test_hardlink_target_rejected(self):
        name = next(iter(self.files))
        path = self.existing(name, b'old')
        os.link(path, self.root / 'hardlink')
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertIn('hardlink', result['error'])

    def test_case_and_file_directory_collisions_rejected(self):
        (self.card / 'drivers').mkdir()
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertIn('case collision', result['error'])
        (self.card / 'drivers').rmdir()
        (self.card / 'Drivers').write_bytes(b'not a directory')
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertIn('ancestor', result['error'])
        self.assertEqual((self.card / 'Drivers').read_bytes(), b'not a directory')

    def test_existing_directory_at_file_path_rejected(self):
        path = self.card / next(iter(self.files))
        path.mkdir(parents=True)
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertTrue(path.is_dir())
        self.assertFalse(self.backup.exists())

    def test_idempotent_existing_payloads_are_never_rewritten(self):
        for name, data in self.files.items():
            self.existing(name, data)
        inode = (self.card / next(iter(self.files))).stat().st_ino
        with patch.object(s.os, 'ftruncate', side_effect=AssertionError('No write expected')):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'readback-pass', result)
        self.assertEqual(len(result['unchanged']), 50)
        self.assertFalse(result['card_writes_started'])
        self.assertEqual((self.card / next(iter(self.files))).stat().st_ino, inode)

    def test_matching_preflight_is_complete_read_only_receipt(self):
        for name, data in self.files.items():
            self.existing(name, data)
        result = self.invoke()
        self.assertEqual(result['status'], 'preflight-pass', result)
        self.assertTrue(result['all_50_matching'])
        self.assertEqual(len(result['readback']), 50)
        self.assertEqual(result['publication_order'], [])
        self.assertIn('No copy is needed', result['notice'])
        self.assertFalse(self.backup.exists())

    def test_mount_identity_change_stops_before_card_writes(self):
        with patch.object(s, 'verify_volume', side_effect=[self.identity, {'DeviceIdentifier': 'disk8s1'}]):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertFalse(result['card_writes_started'])
        self.assertFalse(self.backup.exists())

    def test_deadline_terminates_before_writes(self):
        self.budget = s.Budget(progress=lambda _: None, seconds=-1)
        result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertIn('deadline', result['error'])
        self.assertEqual(list(self.card.iterdir()), [])

    def test_final_readback_corruption_never_reports_success(self):
        actual = s.Card.snapshot
        def corrupted(card, name):
            result = actual(card, name)
            if card.budget.stage == 'final-readback' and name == 'System/Config/boot.json':
                result['sha256'] = '0' * 64
            return result
        with patch.object(s.Card, 'snapshot', corrupted):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure', result)
        self.assertIn('Final complete 50-file readback mismatch', result['error'])
        self.assertEqual(json.loads((self.backup / 'receipt.json').read_text())['status'], 'failure')

    def test_backup_fullsync_failure_prevents_card_mutation(self):
        name = next(iter(self.files))
        self.existing(name, b'original')
        actual = s.durable
        def fail(fd, budget, **kwargs):
            if budget.stage == 'host-backup':
                raise OSError('durability unsupported')
            return actual(fd, budget, **kwargs)
        with patch.object(s, 'durable', fail):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertFalse(result['card_writes_started'])
        self.assertEqual((self.card / name).read_bytes(), b'original')

    def test_scan_count_bound_fails_without_writes(self):
        self.existing('unrelated', b'book')
        with patch.object(s, 'MAX_ENTRIES', 0):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertIn('entry bound', result['error'])
        self.assertFalse(self.backup.exists())

    def test_case_normalization_collision_in_touched_directory_fails(self):
        self.existing('Book', b'one')
        self.existing('book', b'two')
        result = self.invoke()
        self.assertEqual(result['status'], 'failure')
        self.assertIn('normalization collision', result['error'])

    def test_close_error_cannot_escape_truthful_receipt(self):
        actual = s.Card.close
        def close(card):
            actual(card)
            raise OSError('simulated close failure')
        with patch.object(s.Card, 'close', close):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure', result)
        self.assertEqual(result['close_errors'], ['simulated close failure'])
        self.assertEqual(json.loads((self.backup / 'receipt.json').read_text())['status'], 'failure')

    def test_unavailable_descriptor_scan_fails_read_only(self):
        with patch.object(s.os, 'supports_fd', set()):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertIn('descriptor-based scandir', result['error'])
        self.assertEqual(list(self.card.iterdir()), [])

    def test_directory_lock_failure_is_read_only(self):
        with patch.object(s.fcntl, 'flock', side_effect=OSError('filesystem does not support directory locks')):
            result = self.invoke(apply=True)
        self.assertEqual(result['status'], 'failure')
        self.assertFalse(result['card_writes_started'])
        self.assertFalse(self.backup.exists())

    def test_mac_fullfsync_failure_is_not_silently_ignored(self):
        path = self.root / 'sync-test'
        fd = os.open(path, os.O_CREAT | os.O_WRONLY, 0o600)
        try:
            with patch.object(s.sys, 'platform', 'darwin'), \
                    patch.object(s.fcntl, 'fcntl', side_effect=OSError('not supported')) as fullsync:
                with self.assertRaisesRegex(OSError, 'no fallback'):
                    s.durable(fd, self.budget)
                fullsync.assert_called_once_with(fd, 51)
        finally:
            os.close(fd)

    def test_source_entry_path_collisions_and_symlinks_refused(self):
        for files in ({'sdcard/A': b'a', 'sdcard/a': b'b'}, {'sdcard/a': b'a', 'sdcard/a/b': b'b'},
                      {'../escape': b'bad'}, {'sdcard/a\\b': b'bad'}):
            with self.subTest(files=files), self.assertRaises(ValueError):
                s.zip_inventory(zipped(files), 50, s.MAX_FILE, s.MAX_SD, self.budget)
        output = io.BytesIO()
        with zipfile.ZipFile(output, 'w') as archive:
            item = zipfile.ZipInfo('sdcard/link')
            item.external_attr = 0o120777 << 16
            archive.writestr(item, b'outside')
        with self.assertRaisesRegex(ValueError, 'special/link'):
            s.zip_inventory(output.getvalue(), 50, s.MAX_FILE, s.MAX_SD, self.budget)

    def test_expected_sha_bound_manifest_inventory_is_exact(self):
        manifest = json.loads(self.manifest_raw)
        manifest['files'].pop(next(iter(manifest['files'])))
        raw = json.dumps(manifest).encode()
        outer = zipped({'deployment.json': raw, 'sdcard.zip': self.sd})
        self.artifact.write_bytes(outer)
        with patch.object(s, 'EXPECTED_ARTIFACT_SHA256', s.digest(outer)['sha256']), \
                patch.object(s, 'EXPECTED_MANIFEST_SHA256', s.digest(raw)['sha256']):
            result = self.invoke()
        self.assertEqual(result['status'], 'failure')
        self.assertIn('Exactly 50', result['error'])


if __name__ == '__main__':
    unittest.main()
