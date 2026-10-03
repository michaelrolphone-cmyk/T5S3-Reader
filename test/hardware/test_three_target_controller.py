import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from unittest.mock import Mock
import three_target_controller as controller
import heartbeat_policy as policy

class ControllerTests(unittest.TestCase):
    def test_x4_app_contract_keeps_exact_provenance_and_target(self):
        image=bytearray(32); image[0]=0xe9; image[12]=9
        image=bytes(image)+b'RISCRTE_BOARD_ID:xteink-x4-pro\0'
        info={'schema':1,'board':'xteink-x4-pro','source_sha':'a'*40,'run_id':12,'run_attempt':2,
              'firmware':{'file':'firmware.bin','offset':65536,'bytes':len(image),'sha256':policy.digest(image)}}
        stream=io.BytesIO()
        with zipfile.ZipFile(stream,'w') as archive:
            archive.writestr('firmware.bin',image);archive.writestr('manifest.json',json.dumps(info))
        raw=stream.getvalue()
        with tempfile.TemporaryDirectory() as root:
            run={'id':12,'run_attempt':2}
            result=controller.unpack_x4_app(raw,run,'a'*40,'x4',Path(root)/'ok')
            self.assertEqual(result['images']['candidate']['sha256'],policy.digest(image))
            for i,(sha,target,attempt) in enumerate([('b'*40,'x4',2),('a'*40,'cam-nosd',2),('a'*40,'x4',3)]):
                with self.assertRaises(RuntimeError):
                    controller.unpack_x4_app(raw,dict(run,run_attempt=attempt),sha,target,Path(root)/str(i))

    def test_x4_fallback_is_not_used_for_cam_or_existing_candidate(self):
        from unittest.mock import patch
        found=({'id':12,'conclusion':'success'},{'id':3})
        with patch.object(controller,'_candidate',side_effect=[None,found]) as fetch:
            self.assertEqual(controller.candidate(Mock(),350,'a'*40,'x4')[1]['_format'],'x4-app-v1')
            self.assertEqual(fetch.call_count,2)
        for target,result in [('cam-nosd',None),('x4',found)]:
            with patch.object(controller,'_candidate',return_value=result) as fetch:
                self.assertEqual(controller.candidate(Mock(),350,'a'*40,target),result)
                self.assertEqual(fetch.call_count,1)

    def archive(self,target='cam-nosd',duplicate=False,wrong_hash=False):
        image=b'firmware'; info={'file':'candidate.bin','offset':65536,'bytes':len(image),'sha256':policy.digest(image)}
        heartbeat=dict(info,file='heartbeat.bin')
        if wrong_hash: info['sha256']='0'*64
        manifest={'schema':1,'target':target,'source_sha':'a'*40,'run_id':12,'run_attempt':2,'images':{'candidate':info,'heartbeat':heartbeat}}
        stream=io.BytesIO()
        with zipfile.ZipFile(stream,'w') as z:
            z.writestr('candidate.bin',image); z.writestr('heartbeat.bin',image); z.writestr('manifest.json',json.dumps(manifest))
            if duplicate:z.writestr('candidate.bin',image)
        return stream.getvalue()

    def test_unpack_exact_target_and_reject_unsafe_or_changed(self):
        with tempfile.TemporaryDirectory() as root:
            gh=Mock(); gh.call.return_value=self.archive()
            result=controller.unpack(gh,{'id':12,'run_attempt':2},{'id':3},'a'*40,'cam-nosd',Path(root)/'good')
            self.assertEqual(result['target'],'cam-nosd')
            for index,archive in enumerate([self.archive(target='cam-sd'),self.archive(duplicate=True),self.archive(wrong_hash=True)]):
                gh.call.return_value=archive
                with self.assertRaises(RuntimeError): controller.unpack(gh,{'id':12,'run_attempt':2},{'id':3},'a'*40,'cam-nosd',Path(root)/str(index))

    def test_success_needs_candidate_and_heartbeat_and_protection(self):
        record={'result':'pass','candidate_readback_equal':True,'protected_equal':True,'heartbeat_restored':True,'heartbeat_readback_equal':True,'heartbeat_health':{'count':3}}
        with tempfile.TemporaryDirectory() as root:
            for index,missing in enumerate([None,*record]):
                value=record.copy()
                if missing:value.pop(missing)
                result={'source_sha':'a'*40,'target':'cam-nosd','device':value}
                gh=Mock(); gh.call.return_value={'id':7}
                controller.status(gh,Path(root)/f'{index}.json',result)
                state=gh.call.call_args.args[2]['state']
                self.assertEqual(state=='success',missing is None)
                controller.status(gh,Path(root)/f'{index}.json',result)
                self.assertEqual(gh.call.call_count,1)

if __name__=='__main__':unittest.main()
