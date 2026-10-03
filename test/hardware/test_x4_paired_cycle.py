import json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import x4_paired_cycle as cycle

class ExternalBootTests(unittest.TestCase):
    def sample(self):
        expected={'driver':{'version':'1.2.3','bytes':52,'origin':'/bootfs/Drivers/driver/driver.elf'}}
        lines=['[1] [INF] [BOOTFS] registered origin=/bootfs/Drivers/driver/driver.elf id=driver version=1.2.3 bytes=52']
        lines += [f'[{i+2}] [INF] [PROV] PROVREF id=driver stage={phase}' for i,phase in enumerate(cycle.PHASES)]
        lines += ['[8] [INF] [X4] input.touch ready=1','[9] [INF] [X4] heartbeat ready=1']
        return expected,lines
    def test_external_origins_and_device_phase_durations(self):
        expected,lines=self.sample();r=cycle.assess(lines,expected,lambda _: {'home_present':True})
        self.assertEqual(r['provider_phase_timing']['driver']['relocate_ms'],1)
        self.assertEqual(r['external_providers']['driver']['origin'],expected['driver']['origin'])
    def test_wrong_origin_version_missing_phase_and_regression_fail(self):
        for kind in ('origin','version','phase','readiness','failure'):
            e,lines=self.sample()
            if kind=='origin':lines[0]=lines[0].replace('/bootfs/','/embedded/')
            if kind=='version':lines[0]=lines[0].replace('version=1.2.3','version=1.2.4')
            if kind=='phase':lines.pop(2)
            if kind=='readiness':lines.append('[10] [INF] [X4] heartbeat ready=0')
            if kind=='failure':lines.append('[10] [ERR] [PROV] PROVREF id=driver failure=hardware-start code=-1')
            with self.subTest(kind=kind),self.assertRaises(ValueError):cycle.assess(lines,e,lambda _: {})

class FakePolicy:
    def check_layout(self,prefix,target,length):
        assert prefix==b'prefix' and target=='x4' and length==5951488
    def save(self,path,value):path.write_text(json.dumps(value))
    def healthy(self,lines,target):
        assert lines==['baseline'] and target=='x4'
        return {'count':5}

class FakeTransport:
    def __init__(self,mode):self.mode=mode;self.commands=[];self.timings=[];self.boots=[];self.cleanup=False
    def identity(self):pass
    def read(self,label,address,length):
        if label=='baseline-before':return b'heartbeat'
        if label=='store-before':return b'changed' if self.mode=='changed-store' else b'backup'
        if label=='store-after':return b'partial' if self.mode=='failed-write' else b'store'
        if label=='protected-pre-cleanup' and self.mode=='changed-prefix':return b'changed'
        return b'prefix'
    def command(self,label,*args,**kwargs):
        self.commands.append((label,args))
        if self.mode=='failed-write' and label=='paired-write':raise RuntimeError('write failed')
    def write(self,label,path):
        self.commands.append((label,str(path)));self.cleanup=True
        if self.mode=='cleanup-failed':raise RuntimeError('cleanup failed')
    def boot(self,seconds):
        self.boots.append(seconds)
        return ['baseline'] if seconds==10 else ['candidate']

class PairedCleanupTests(unittest.TestCase):
    def run_cycle(self,mode):
        with tempfile.TemporaryDirectory() as root,patch.object(cycle,'digest',lambda data:data.decode()),patch.multiple(cycle,PREFIX='prefix',HEARTBEAT='heartbeat',BACKUP='backup',STORE='store'):
            def assess(*_):
                if mode=='candidate-failed':raise ValueError('Home missing')
                return {'shared_home':True}
            with patch.object(cycle,'assess',assess):
                t=FakeTransport(mode);r=cycle.cycle(FakePolicy(),t,Path(root),Path('firmware.bin'),Path('module-store.bin'),Path('heartbeat.bin'),{},lambda _: {})
                return t,r
    def test_success_retains_store_and_only_restores_heartbeat(self):
        t,r=self.run_cycle('success');self.assertEqual(r['result'],'pass')
        self.assertEqual(r['retained_store_sha256'],'store');self.assertTrue(r['heartbeat_restored'])
        write=t.commands[0];self.assertEqual(write[0],'paired-write')
        self.assertIn('0x10000',write[1]);self.assertIn('0xc90000',write[1])
        self.assertFalse(any('original-store' in str(c) for c in t.commands))
        self.assertEqual(t.boots,[90,10,10])
    def test_changed_store_has_no_writes_and_returns_known_baseline(self):
        t,r=self.run_cycle('changed-store');self.assertEqual(r['result'],'failed')
        self.assertEqual(t.commands,[]);self.assertEqual(t.boots,[10])
    def test_candidate_failure_still_cleans_up(self):
        t,r=self.run_cycle('candidate-failed');self.assertEqual(r['result'],'failed')
        self.assertTrue(r['heartbeat_restored']);self.assertEqual(r['retained_store_sha256'],'store')
    def test_partial_pair_restores_heartbeat_but_reports_recovery_needed(self):
        t,r=self.run_cycle('failed-write');self.assertEqual(r['result'],'failed')
        self.assertTrue(r['heartbeat_restored']);self.assertIn('incomplete',r['cleanup_error'])
        self.assertNotIn(90,t.boots);self.assertFalse(any('original-store' in str(c) for c in t.commands))
    def test_changed_protected_prefix_refuses_cleanup_write(self):
        t,r=self.run_cycle('changed-prefix');self.assertEqual(r['result'],'failed')
        self.assertFalse(t.cleanup);self.assertIn('cleanup write refused',r['cleanup_error'])
    def test_cleanup_transport_failure_never_claims_restoration(self):
        t,r=self.run_cycle('cleanup-failed');self.assertEqual(r['result'],'failed')
        self.assertNotIn('heartbeat_restored',r);self.assertIn('cleanup failed',r['cleanup_error'])

if __name__=='__main__':unittest.main()
