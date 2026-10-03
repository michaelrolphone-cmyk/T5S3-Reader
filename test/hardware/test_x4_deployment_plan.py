import io,json,tempfile,unittest,zipfile
from pathlib import Path
import x4_deployment_plan as plan

SHA='a'*40

def zip_bytes(files):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,data)
    return out.getvalue()

def fixture(change=None,image=None,board_id='xteink-x4-pro'):
    elf=bytearray(52);elf[:6]=b'\x7fELF\x01\x01';elf[16]=3;elf[18]=94
    package={'driver.elf':bytes(elf),'manifest.json':json.dumps({'id':'test-driver','version':'1.0.0'}).encode()}
    meta={'schema':1,'kind':'driver','id':'test-driver','version':'1.0.0','architecture':'xtensa-esp32s3','artifact':'driver.elf',
          'entries':[{'name':n,'size_bytes':len(d),'sha256':plan.digest(d)['sha256']} for n,d in package.items()]}
    package['.package.json']=json.dumps(meta).encode();archive=zip_bytes(package)
    record={'id':'test-driver','version':'1.0.0','file':'test-driver-1.0.0.rte.zip',**plan.digest(archive)}
    decoded={'boot.json':json.dumps({'board':'board.json','drivers':[{'manifest':'Drivers/test-driver/manifest.json'}]}).encode(),
             'board.json':json.dumps({'board_id':board_id}).encode()}
    decoded.update({'Drivers/test-driver/'+n:d for n,d in package.items()})
    app=bytearray(128);app[0]=0xe9;app[12]=9;marker=('RISCRTE_BOARD_ID:'+board_id).encode();app[32:32+len(marker)]=marker
    table=b'nvs,data,nvs,0x9000,0x5000,\notadata,data,ota,0xe000,0x2000,\napp0,app,ota_0,0x10000,0x640000,\napp1,app,ota_1,0x650000,0x640000,\nspiffs,data,spiffs,0xc90000,0x360000,\ncoredump,data,coredump,0xff0000,0x10000,\n'
    files={'firmware.bin':bytes(app),'module-store.bin':image or b'X'*plan.STORE_SIZE,'partitions.csv':table,'README.txt':b'fixture','packages/'+record['file']:archive}
    manifest={'schema':1,'board':board_id,'source_sha':SHA,'provisioning_authorized':False,
              'firmware':{'file':'firmware.bin','offset':plan.APP_OFFSET,**plan.digest(files['firmware.bin'])},
              'module_store':{'file':'module-store.bin','offset':plan.STORE_OFFSET,**plan.digest(files['module-store.bin'])},
              'partition_table':plan.digest(table),'files':{n:plan.digest(d) for n,d in decoded.items()},'packages':[record]}
    if change:change(files,manifest,decoded)
    files['deployment.json']=json.dumps(manifest).encode()
    return zip_bytes(files),decoded

class DeploymentPlanTests(unittest.TestCase):
    def check(self,change=None,sha=SHA):
        raw,decoded=fixture(change)
        return plan.validate(raw,sha,plan.digest(raw)['sha256'],decoder=lambda *_:decoded)
    def test_valid_plan_has_no_authorization_or_automatic_deployment(self):
        result=self.check();self.assertEqual(result['validation'],'pass')
        self.assertFalse(result['provisioning_authorized']);self.assertFalse(result['automatic_app_only_deployment_allowed'])
        self.assertEqual(result['proposed_regions'][1]['offset'],0xc90000)
        self.assertEqual(result['protected_regions'][1],{'offset':0x650000,'bytes':0x640000})
    def test_t5_requires_explicit_board_without_x4_hardware_binding(self):
        raw,decoded=fixture(board_id='t5s3-pro')
        with self.assertRaisesRegex(ValueError,'source/board'):
            plan.validate(raw,SHA,plan.digest(raw)['sha256'],decoder=lambda *_:decoded)
        result=plan.validate(raw,SHA,plan.digest(raw)['sha256'],decoder=lambda *_:decoded,board_id='t5s3-pro')
        self.assertIsNone(result['expected_mac']);self.assertIsNone(result['expected_partition_sha256'])
        self.assertFalse(result['physical_binding_established']);self.assertFalse(result['provisioning_authorized'])
    def test_t5_rejects_x4_and_unknown_board_profiles(self):
        raw,decoded=fixture()
        for board in ('t5s3-pro','unknown'):
            with self.subTest(board=board),self.assertRaises(ValueError):
                plan.validate(raw,SHA,plan.digest(raw)['sha256'],decoder=lambda *_:decoded,board_id=board)
    def test_t5_rejects_mixed_firmware_and_store_identity(self):
        for component in ('firmware','store'):
            def change(files,manifest,decoded):
                if component=='firmware':
                    files['firmware.bin']=files['firmware.bin'].replace(b't5s3-pro',b'wrong-id')
                    manifest['firmware'].update(plan.digest(files['firmware.bin']))
                else:
                    decoded['board.json']=json.dumps({'board_id':'xteink-x4-pro'}).encode()
                    manifest['files']['board.json']=plan.digest(decoded['board.json'])
            raw,decoded=fixture(change,board_id='t5s3-pro')
            with self.subTest(component=component),self.assertRaisesRegex(ValueError,'[Bb]oard'):
                plan.validate(raw,SHA,plan.digest(raw)['sha256'],decoder=lambda *_:decoded,board_id='t5s3-pro')
    def test_wrong_source(self):
        with self.assertRaisesRegex(ValueError,'source'):self.check(sha='b'*40)
    def test_missing_store(self):
        with self.assertRaises((KeyError,ValueError)):self.check(lambda f,m,d:f.pop('module-store.bin'))
    def test_store_hash_mismatch(self):
        with self.assertRaisesRegex(ValueError,'store hash'):self.check(lambda f,m,d:m['module_store'].update(sha256='f'*64))
    def test_no_partition_relocation(self):
        with self.assertRaisesRegex(ValueError,'store hash'):self.check(lambda f,m,d:m['module_store'].update(offset=0x650000))
    def test_artifact_cannot_grant_approval(self):
        with self.assertRaisesRegex(ValueError,'authorize'):self.check(lambda f,m,d:m.update(provisioning_authorized=True))
    def test_outer_traversal(self):
        with self.assertRaisesRegex(ValueError,'Unsafe'):self.check(lambda f,m,d:f.update({'../escape':b'x'}))
    def test_store_manifest_cannot_hide_package_mismatch(self):
        def change(f,m,d):
            key='Drivers/test-driver/driver.elf';d[key]+=b'altered';m['files'][key]=plan.digest(d[key])
        with self.assertRaisesRegex(ValueError,'Store/package'):self.check(change)
    def test_undeclared_store_file(self):
        def change(f,m,d):d['extra']=b'x';m['files']['extra']=plan.digest(b'x')
        with self.assertRaisesRegex(ValueError,'Unexpected/missing'):self.check(change)
    def test_decoder_hash_is_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            tool=Path(tmp)/'tool';tool.write_bytes(b'not a decoder')
            with self.assertRaisesRegex(ValueError,'decoder hash'):plan.decode_store(b'',tool,'0'*64)
    def test_nested_package_duplicate(self):
        with self.assertRaisesRegex(ValueError,'Duplicate archive'):
            raw=io.BytesIO()
            with zipfile.ZipFile(raw,'w') as z:z.writestr('file',b'a');z.writestr('file',b'b')
            plan.archive_files(raw.getvalue(),17,1024,2048)

if __name__=='__main__':unittest.main()
