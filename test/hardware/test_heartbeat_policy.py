import contextlib
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import heartbeat_policy as policy

def image(target):
    data=bytearray(32); data[0]=0xe9; data[12]=9
    return bytes(data)+b'RTE_HEARTBEAT version='+target.encode()+b'\0'

def lines(target='cam-nosd'):
    return [f'RTE_HEARTBEAT version=1.0.0 target={target} mac={policy.BOARDS[target][0]} sequence={i} uptime_ms={i*2000} heap=1000 app=0x10000' for i in range(1,5)]

class PolicyTests(unittest.TestCase):
    def test_heartbeat_requires_target_mac_and_progress(self):
        self.assertEqual(policy.healthy(lines(),'cam-nosd')['count'],4)
        for bad in [lines()[:2],lines()[::-1],lines()+lines(),lines('cam-sd')]:
            with self.assertRaises(RuntimeError): policy.healthy(bad,'cam-nosd')

    def test_no_storage_requires_real_mount_and_no_grants(self):
        boot=['RTE_NOSD mount_attempted=1 mounted=0 error=263']+['RUNTIME BOOT heartbeat state=Idle heap=12 handles=0 has_grants=0']*3
        self.assertTrue(policy.candidate_result(boot,'cam-nosd')['storage_absent'])
        for bad in [boot[1:], [boot[0].replace('mounted=0','mounted=1')]+boot[1:],boot+['state=Running'],boot+['Guru Meditation']]:
            with self.assertRaises(RuntimeError): policy.candidate_result(bad,'cam-nosd')

    def test_image_rejects_wrong_target_digest_and_chip(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'app.bin'; data=image('cam-sd'); path.write_bytes(data)
            policy.image_bytes(path,policy.digest(data),'cam-sd',True)
            with self.assertRaises(RuntimeError): policy.image_bytes(path,policy.digest(data),'cam-nosd',True)
            with self.assertRaises(RuntimeError): policy.image_bytes(path,'0'*64,'cam-sd',True)
            wrong=bytearray(data); wrong[12]=0; path.write_bytes(wrong)
            with self.assertRaises(RuntimeError): policy.image_bytes(path,policy.digest(wrong),'cam-sd',True)

    def exercise(self, candidate_fail=False, cleanup_fail=False, metadata_fail=False):
        events=[]
        class FakeTransport:
            serial_bytes=0
            def __init__(self,*args): pass
            def port(self): return '/dev/cu.usbserial-2320'
            def identity(self): events.append('identity')
            def read(self,label,*args):
                events.append(label)
                return b'changed' if metadata_fail and label=='protected-after' else b'protected'
            def write(self,label,*args):
                events.append(label)
                if cleanup_fail and label=='heartbeat-cleanup': raise RuntimeError('USB disappeared')
            def boot(self,*args): return lines()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); path=root/'hb.bin'; data=image('cam-nosd'); path.write_bytes(data)
            with patch.object(policy,'Transport',FakeTransport), patch.object(policy,'locks',lambda *args:contextlib.nullcontext()), patch.object(policy,'check_layout'), patch.object(policy,'candidate_result',return_value={'boot':True},side_effect=RuntimeError('missing storage evidence') if candidate_fail else None):
                result=policy.transaction('cam-nosd',{'mac':policy.BOARDS['cam-nosd'][0]},path,policy.digest(data),root/'result',path,policy.digest(data))
            self.assertFalse((root/'result/prewrite-app-backup.bin').exists())
            self.assertTrue((root/'result/result.json').is_file())
        return result,events

    def test_failure_still_leaves_heartbeat(self):
        result,events=self.exercise(candidate_fail=True)
        self.assertEqual(result['result'],'failed')
        self.assertTrue(result['heartbeat_restored'])
        self.assertIn('heartbeat-cleanup',events)

    def test_cleanup_failure_is_not_success(self):
        result,_=self.exercise(cleanup_fail=True)
        self.assertEqual(result['result'],'failed')
        self.assertNotIn('heartbeat_restored',result)

    def test_protected_region_change_fails(self):
        result,_=self.exercise(metadata_fail=True)
        self.assertEqual(result['result'],'failed')
        self.assertNotIn('heartbeat_restored',result)

if __name__=='__main__': unittest.main()
