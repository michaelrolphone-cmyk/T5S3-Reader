import io,json,tempfile,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
import runtime_cam_adapter as adapter

class RuntimeAdapterTests(unittest.TestCase):
    def archive(self,alter=None):
        app=bytearray(384);app[0]=0xe9;app[12]=9
        marker=('RTE_SOURCE='+'a'*40).encode();app[256:256+len(marker)]=marker
        values={'default.elf':b'\x7fELFtest','board.json':b'{"target":"cam-nosd","buses":[],"devices":[]}','boot.json':b'{"default_app":"default.elf","drivers":[]}'}
        items=[];offset=32
        for name,data in values.items():
            app[offset:offset+len(data)]=data
            items.append({'file':name,'bytes':len(data),'sha256':adapter.device.digest(data),'image_offset':offset});offset+=len(data)
        m={'schema':1,'target':'cam-nosd','source_sha':'a'*40,'run_id':123,'run_attempt':1,'boot_backend':'embedded-readonly','flash_bytes':16777216,'memory_type':'qio_opi',
           'firmware':{'file':'firmware.bin','bytes':len(app),'sha256':adapter.device.digest(app),'offset':65536},'payloads':items}
        if alter:alter(m)
        out=io.BytesIO()
        with zipfile.ZipFile(out,'w') as z:
            for name,data in dict(values,**{'firmware.bin':bytes(app),'manifest.json':json.dumps(m).encode()}).items():z.writestr(name,data)
        return out.getvalue()
    def test_valid_embedded_artifact(self):
        with tempfile.TemporaryDirectory() as root:
            dest=Path(root)/'artifact';adapter.unpack(self.archive(),{'id':123,'run_attempt':1},'a'*40,dest)
            self.assertEqual((dest/'candidate.bin').stat().st_size,384)
    def test_x4_explicit_target_is_separate(self):
        with tempfile.TemporaryDirectory() as root:
            raw=self.archive(lambda m:m.update(target='x4'))
            adapter.unpack(raw,{'id':123,'run_attempt':1},'a'*40,Path(root)/'x4',target='x4')
            with self.assertRaisesRegex(RuntimeError,'provenance'):
                adapter.unpack(raw,{'id':123,'run_attempt':1},'a'*40,Path(root)/'cam')
    def test_payload_offset_must_match_actual_firmware(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(RuntimeError,'Embedded payload'):
                adapter.unpack(self.archive(lambda m:m['payloads'][0].update(image_offset=33)),{'id':123,'run_attempt':1},'a'*40,Path(root)/'artifact')
    def test_manifest_cannot_redirect_flash_write(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(RuntimeError,'offset mismatch'):
                adapter.unpack(self.archive(lambda m:m['firmware'].update(offset=0)),{'id':123,'run_attempt':1},'a'*40,Path(root)/'artifact')
    def test_wrong_head_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(RuntimeError,'provenance'):
                adapter.unpack(self.archive(),{'id':123,'run_attempt':1},'b'*40,Path(root)/'artifact')
    def test_scheduled_x4_uses_distinct_context_and_runs_exact_head_once(self):
        repo={'id':adapter.REPOSITORY_ID,'full_name':adapter.REPOSITORY}
        pr={'number':1,'state':'open','user':{'login':adapter.github.OWNER},'head':{'sha':'a'*40,'repo':repo},'base':{'repo':repo}}
        raw=self.archive(lambda m:m.update(target='x4'));posts=[]
        class Client:
            repository=adapter.REPOSITORY
            def call(self,path,method='GET',body=None,**kwargs):
                if method=='POST':posts.append(body);return {'id':len(posts)}
                if path=='':return repo
                if path.startswith('/pulls?'):return [pr]
                if path=='/pulls/1':return pr
                if path=='/actions/artifacts/7/zip':return raw
                raise AssertionError(path)
        run={'id':123,'run_attempt':1,'conclusion':'success'}
        result={'result':'pass','candidate_readback_equal':True,'protected_equal':True,'heartbeat_restored':True,'heartbeat_readback_equal':True,'candidate_checks':{'count':3}}
        with tempfile.TemporaryDirectory() as tmp,patch.object(adapter.github,'GitHub',return_value=Client()),patch.object(adapter.controller,'_candidate',return_value=(run,{'id':7})) as candidate,patch.object(adapter.controller,'private_json',return_value={}),patch.object(adapter.device,'Transport'),patch.object(adapter.device,'transaction',return_value=result) as transaction:
            root=Path(tmp);job={'target':'x4','pause':str(root/'absent'),'binding':'binding','heartbeat':'heartbeat','heartbeat_sha256':'c'*64}
            for _ in range(2):adapter.scan('unused',{'enabled':True},[job],root,'x4')
            self.assertEqual(transaction.call_count,1)
            self.assertEqual(transaction.call_args.args[0],'x4')
            self.assertEqual(candidate.call_args.args[4],'x4-hardware-build.yml')
            self.assertTrue((root/'runtime-x4'/('a'*40)/'result.json').exists())
            self.assertFalse((root/'runtime-cam').exists())
        self.assertEqual([p['state'] for p in posts],['pending','success'])
        self.assertTrue(all(p['context']=='X4 hardware / runtime heartbeat cleanup' for p in posts))

    def test_waiting_build_has_terminal_deadline_without_device_access(self):
        repo={'id':adapter.REPOSITORY_ID,'full_name':adapter.REPOSITORY}
        pr={'number':1,'state':'open','user':{'login':adapter.github.OWNER},'head':{'sha':'a'*40,'repo':repo},'base':{'repo':repo}}
        posts=[]
        class Client:
            repository=adapter.REPOSITORY
            def call(self,path,method='GET',body=None):
                if method=='POST':posts.append(body);return {'id':len(posts)}
                if path=='':return repo
                if path.startswith('/pulls?'):return [pr]
                if path=='/pulls/1':return pr
                raise AssertionError(path)
        with tempfile.TemporaryDirectory() as tmp,patch.object(adapter.github,'GitHub',return_value=Client()),patch.object(adapter.controller,'_candidate',return_value=None),patch.object(adapter.controller,'private_json',return_value={}),patch.object(adapter.device,'Transport'),patch.object(adapter.device,'transaction') as transaction:
            root=Path(tmp);job={'target':'x4','pause':str(root/'absent'),'binding':'binding'}
            for clock in (1000,1601):
                with patch.object(adapter.time,'time',return_value=clock):adapter.scan('unused',{'enabled':True},[job],root,'x4')
            transaction.assert_not_called()
        self.assertEqual([p['state'] for p in posts],['pending','failure'])
        self.assertIn('bounded build wait',posts[-1]['description'])

    def test_status_network_failure_preserves_hardware_evidence(self):
        class Offline:
            def call(self,*args):raise OSError('network unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'result.json';record={'source_sha':'a'*40,'passed':True,'device':{'heartbeat_restored':True}}
            with self.assertRaises(OSError):adapter.publish(Offline(),path,record,'success','verified')
            self.assertTrue(json.loads(path.read_text())['passed'])

    def test_paused_recovery_never_accesses_device(self):
        with tempfile.TemporaryDirectory() as tmp,patch.object(adapter.device,'transaction') as transaction:
            root=Path(tmp);active=root/'runtime-cam/active.json';active.parent.mkdir();active.write_text('{}')
            pause=root/'pause';pause.touch()
            reason=adapter.recover_active({'enabled':True},{'pause':str(pause)},root)
            self.assertIn('paused',reason);transaction.assert_not_called();self.assertTrue(active.exists())

    def test_interrupted_old_head_recovered_independently_of_current_pr(self):
        with tempfile.TemporaryDirectory() as tmp,patch.object(adapter.device,'transaction',return_value={'result':'pass','heartbeat_restored':True,'protected_equal':True}),patch.object(adapter.controller,'private_json',return_value={}):
            root=Path(tmp);folder=root/'runtime-cam'/('a'*40);folder.mkdir(parents=True)
            active=folder.parent/'active.json';active.write_text(json.dumps({'target':'cam-nosd','source_sha':'a'*40}))
            job={'pause':str(root/'absent'),'binding':'binding','heartbeat':'heartbeat','heartbeat_sha256':'c'*64}
            self.assertIsNone(adapter.recover_active({'enabled':True},job,root))
            self.assertFalse(active.exists())
            record=json.loads((folder/'result.json').read_text())
            self.assertTrue(record['recovery_complete']);self.assertFalse(record['passed'])

    def test_failed_recovery_preserves_active_record(self):
        with tempfile.TemporaryDirectory() as tmp,patch.object(adapter.device,'transaction',side_effect=RuntimeError('USB absent')),patch.object(adapter.controller,'private_json',return_value={}):
            root=Path(tmp);active=root/'runtime-cam/active.json';active.parent.mkdir()
            active.write_text(json.dumps({'target':'cam-nosd','source_sha':'a'*40}))
            job={'pause':str(root/'absent'),'binding':'binding','heartbeat':'heartbeat','heartbeat_sha256':'c'*64}
            self.assertIn('USB absent',adapter.recover_active({'enabled':True},job,root))
            self.assertTrue(active.exists())

    def test_disabled_publishes_failure_without_device_access(self):
        repo={'id':adapter.REPOSITORY_ID,'full_name':adapter.REPOSITORY}
        pr={'number':1,'state':'open','user':{'login':adapter.github.OWNER},'head':{'sha':'a'*40,'repo':repo},'base':{'repo':repo}}
        posts=[]
        class Client:
            repository=adapter.REPOSITORY
            def call(self,path,method='GET',body=None):
                if method=='POST':posts.append(body);return {'id':100}
                if path=='':return repo
                if path.startswith('/pulls?'):return [pr]
                if path=='/pulls/1':return pr
                raise AssertionError(path)
        with tempfile.TemporaryDirectory() as root,patch.object(adapter.github,'GitHub',return_value=Client()),patch.object(adapter.device,'transaction') as transaction:
            for _ in range(2):adapter.scan('unused',{'enabled':False,'unavailable_reason':'USB control timeout'},[{'target':'cam-nosd'}],Path(root))
            transaction.assert_not_called()
        self.assertEqual(len(posts),1)
        self.assertEqual(posts[0]['state'],'failure')
        self.assertEqual(posts[0]['context'],adapter.CONTEXT)
        self.assertIn('USB control timeout',posts[0]['description'])

if __name__=='__main__':unittest.main()
