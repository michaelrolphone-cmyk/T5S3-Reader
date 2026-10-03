import io,json,tempfile,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
import runtime_cam_adapter as adapter

class RuntimeAdapterTests(unittest.TestCase):
    def archive(self,alter=None):
        app=bytearray(256);app[0]=0xe9;app[12]=9
        marker=('RTE_SOURCE='+'a'*40).encode();app[128:128+len(marker)]=marker
        values={'default.elf':b'\x7fELFtest','board.json':b'{"target":"cam-nosd"}','boot.json':b'{"app":"default.elf"}'}
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
            self.assertEqual((dest/'candidate.bin').stat().st_size,256)
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
