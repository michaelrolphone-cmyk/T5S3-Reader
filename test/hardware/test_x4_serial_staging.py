import base64
import io
import json
import unittest
from unittest.mock import patch
import zipfile
import x4_serial_staging as s


def archive(name):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as z:
        for file, data in {'.package.json': b'{}', name+'.elf': b'elf'*500, name+'.json': b'{}'}.items():
            z.writestr(file, data)
    return out.getvalue()


class Endpoint:
    """Protocol peer for host failure tests; NOT a filesystem/device model."""
    def __init__(self):
        self.requests=[]; self.output=bytearray(); self.pins=b'books.elf\r\n'; self.uploads={}; self.installed={}
        self.drop=None; self.foreign=False; self.partial=False; self.conflict=False; self.pin_race=False
        self.now=0
    def inventory(self):
        return {'snapshot':'b'*32,'apps':{n:({'state':'matching','files':s.package_inventory(self.installed[n],n)} if n in self.installed else
                    {'state':'conflict' if self.conflict else 'absent','files':{}}) for n in s.IDS},
                'pins':{'bytes':len(self.pins),'sha256':s.sha(self.pins)},'pins_exists':True}
    def write(self, raw):
        r=s.decode(raw);self.requests.append(r);op=r['op'];a=r['args'];ok=True
        if self.partial:return len(raw)-1
        if op=='hello': result={**a,'protocol':1,'sd_verified_files':s.SD_FILES,'scope':list(s.IDS)}
        elif op=='inventory':result=self.inventory()
        elif op=='read':
            if a['selector']=='pins': data=self.pins
            else:
                name,file=a['selector'].split('/')
                with zipfile.ZipFile(io.BytesIO(self.installed[name])) as z:data=z.read(file)
            result={'data':base64.b64encode(data[a['offset']:a['offset']+a['count']]).decode()}
        elif op=='begin':self.uploads[a['id']]=bytearray();result={'offset':0}
        elif op=='chunk':
            data=base64.b64decode(a['data']);assert s.sha(data)==a['sha256'];assert len(self.uploads[a['id']])==a['offset']
            self.uploads[a['id']].extend(data);result={'offset':len(self.uploads[a['id']])}
        elif op=='install':
            data=bytes(self.uploads[a['id']]);self.installed[a['id']]=data
            result={'state':'installed','archive_sha256':s.sha(data)}
        elif op=='pins':
            if self.pin_race:self.pins+=b'concurrent.elf\n'
            if s.sha(self.pins)!=a['before_sha256']:ok=False;result={'error':'changed'}
            else:self.pins=s.merge_pins(self.pins);result={'bytes':len(self.pins),'sha256':s.sha(self.pins)}
        else:raise AssertionError(op)
        if op!=self.drop:
            self.output.extend(b'[X4] heartbeat ready=1\n'+s.encode({'session':'f'*32 if self.foreign else r['session'],
                'sequence':r['sequence'],'ok':ok,'result':result}))
        return len(raw)
    def read(self, count):
        self.now+=.2
        data=bytes(self.output[:min(count,37)]);del self.output[:len(data)];return data
    def client(self):return s.Client(self,'a'*64,clock=lambda:self.now)


class StagingTest(unittest.TestCase):
    def setUp(self):
        self.archives={n:archive(n) for n in s.IDS}
        self.pin=patch.object(s,'ARCHIVES',{n:(len(d),s.sha(d)) for n,d in self.archives.items()});self.pin.start();self.addCleanup(self.pin.stop)
    def stage(self,e,save=lambda *_:True):
        c=e.client();c.hello();i=c.inventory();b=c.backup(i);return c.stage(i,b,self.archives,save)
    def test_corrected_sd_handshake_and_explicit_verified_count(self):
        e=Endpoint();c=e.client();c.hello()
        self.assertEqual(e.requests[0]['args'],{'source_sha':s.SOURCE,'sd_archive_sha256':s.SD_ARCHIVE,'utility_id':'a'*64})
        self.assertNotIn('store_sha256',e.requests[0]['args'])
    def test_old_unverified_handshake_refused(self):
        e=Endpoint();write=e.write
        def old(raw):
            count=write(raw)
            e.output=e.output.replace(b'"sd_verified_files":50,',b'')
            return count
        e.write=old
        with self.assertRaisesRegex(s.ProtocolError,'SD verification'):e.client().hello()
    def test_install_readback_preserve_and_idempotent(self):
        e=Endpoint();saved=[]
        final=self.stage(e,lambda i,b:saved.append((i,b)) or True)
        self.assertEqual(saved[0][1]['pins'],b'books.elf\r\n')
        self.assertEqual(e.pins,b'books.elf\r\ndriver_manager.elf\napp_store.elf\n')
        self.assertTrue(all(v['state']=='matching' for v in final['apps'].values()))
        before=len([r for r in e.requests if r['op']=='begin']);self.stage(e)
        self.assertEqual(len([r for r in e.requests if r['op']=='begin']),before)
    def test_conflict_prevents_mutation(self):
        e=Endpoint();e.conflict=True
        with self.assertRaisesRegex(s.ProtocolError,'conflict'):self.stage(e)
        self.assertFalse(any(r['op'] in ('begin','chunk','install','pins') for r in e.requests))
    def test_backup_failure_prevents_mutation(self):
        e=Endpoint()
        with self.assertRaisesRegex(s.ProtocolError,'backup'):self.stage(e,lambda *_:False)
        self.assertFalse(any(r['op']=='begin' for r in e.requests))
    def test_bad_archive_prevents_mutation(self):
        e=Endpoint();self.archives['app_store']+=b'x'
        with self.assertRaisesRegex(s.ProtocolError,'custody'):self.stage(e)
        self.assertFalse(any(r['op']=='begin' for r in e.requests))
    def test_lost_install_ack_never_replayed(self):
        e=Endpoint();e.drop='install';c=e.client();c.hello();i=c.inventory();b=c.backup(i)
        with self.assertRaises(s.UncertainOperation):c.stage(i,b,self.archives,lambda *_:True)
        self.assertEqual(sum(r['op']=='install' for r in e.requests),1)
        self.assertIn('driver_manager',e.installed)
        with self.assertRaises(s.ProtocolError):c.inventory()
        self.assertFalse(any(r['op']=='pins' for r in e.requests))
    def test_pin_compare_and_swap_failure(self):
        e=Endpoint();e.pin_race=True
        with self.assertRaises(s.UncertainOperation):self.stage(e)
        self.assertEqual(e.pins,b'books.elf\r\nconcurrent.elf\n')
    def test_foreign_session_rejected(self):
        e=Endpoint();e.foreign=True
        with self.assertRaisesRegex(s.UncertainOperation,'foreign'):e.client().hello()
    def test_partial_write_not_retried(self):
        e=Endpoint();e.partial=True
        with self.assertRaises(s.UncertainOperation):e.client().hello()
        self.assertEqual(len(e.requests),1)
    def test_duplicate_json_keys(self):
        with self.assertRaises(s.ProtocolError):s.decode(s.PREFIX+b'{"x":1,"x":2}\n')
    def test_overlong_and_unterminated(self):
        e=Endpoint();e.output=bytearray(b'x'*(s.MAX_LINE+257))
        with self.assertRaises(s.UncertainOperation):e.client().hello()
    def test_traversal_inventory_rejected(self):
        e=Endpoint();base=e.inventory
        def bad():
            v=base();v['apps']['app_store']['files']['../secret']={'bytes':1,'sha256':'a'*64};return v
        e.inventory=bad;c=e.client();c.hello()
        with self.assertRaisesRegex(s.ProtocolError,'selector'):c.inventory()
    def test_pins_exact_names_and_bounds(self):
        before=b' driver_manager.elf\rapp_store.elf\n'
        self.assertEqual(s.merge_pins(before),before+b'driver_manager.elf\n')
        for invalid in (b'\0',b'x'*s.MAX_PINS,b'other.elf\n'*128):
            with self.assertRaises(s.ProtocolError):s.merge_pins(invalid)
    def test_wrong_package_inventory_final_rejected(self):
        e=Endpoint();base=e.inventory
        def bad():
            v=base()
            if e.installed:
                n=next(iter(e.installed));v['apps'][n]['files'][n+'.elf']['sha256']='f'*64
            return v
        e.inventory=bad
        with self.assertRaisesRegex(s.ProtocolError,'hash inventory'):self.stage(e)


if __name__=='__main__':unittest.main()
