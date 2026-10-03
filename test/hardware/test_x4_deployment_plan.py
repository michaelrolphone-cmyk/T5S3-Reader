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
    decoded={'System/Config/boot.json':json.dumps({'board':'System/Config/board.json','drivers':[{'manifest':'Drivers/test-driver/manifest.json'}]}).encode(),
             'System/Config/board.json':json.dumps({'board_id':board_id}).encode()}
    decoded.update({'Drivers/test-driver/'+n:d for n,d in package.items()})
    app=bytearray(128);app[0]=0xe9;app[12]=9;marker=('RISCRTE_BOARD_ID:'+board_id).encode();app[32:32+len(marker)]=marker
    table=b'nvs,data,nvs,0x9000,0x5000,\notadata,data,ota,0xe000,0x2000,\napp0,app,ota_0,0x10000,0x640000,\napp1,app,ota_1,0x650000,0x640000,\nspiffs,data,spiffs,0xc90000,0x360000,\ncoredump,data,coredump,0xff0000,0x10000,\n'
    files={'firmware.bin':bytes(app),'partitions.csv':table,'README.txt':b'fixture','packages/'+record['file']:archive}
    manifest={'schema':2,'driver_medium':'sd','sd_root':'sdcard','board':board_id,'source_sha':SHA,'provisioning_authorized':False,
              'firmware':{'file':'firmware.bin','offset':plan.APP_OFFSET,**plan.digest(files['firmware.bin'])},
                            'partition_table':plan.digest(table),'files':{n:plan.digest(d) for n,d in decoded.items()},'packages':[record]}
    decoded['Packages/Inbox/'+record['file']]=archive
    if change:change(files,manifest,decoded)
    manifest['files']={n:plan.digest(d) for n,d in decoded.items()}
    files['sdcard.zip']=zip_bytes({'sdcard/'+n:d for n,d in decoded.items()})
    manifest['sd_archive']={'file':'sdcard.zip',**plan.digest(files['sdcard.zip'])}
    files['deployment.json']=json.dumps(manifest).encode()
    return zip_bytes(files),decoded

class DeploymentPlanTests(unittest.TestCase):
    def check(self,change=None,sha=SHA):
        raw,decoded=fixture(change)
        return plan.validate(raw,sha,plan.digest(raw)['sha256'])
    def test_valid_plan_has_no_authorization_or_automatic_deployment(self):
        result=self.check();self.assertEqual(result['validation'],'pass')
        self.assertFalse(result['provisioning_authorized']);self.assertFalse(result['automatic_app_only_deployment_allowed'])
        self.assertEqual(len(result['proposed_regions']),1)
        self.assertFalse(result['store_replaces_entire_region'])
        self.assertFalse(result['physical_binding_established'])
        self.assertEqual(result['sd_file_count'],6)
        self.assertIn({'offset':0xc90000,'bytes':0x360000},result['protected_regions'])
        self.assertEqual(result['protected_regions'][1],{'offset':0x650000,'bytes':0x640000})
    def test_t5_requires_explicit_board_without_x4_hardware_binding(self):
        raw,decoded=fixture(board_id='t5s3-pro')
        with self.assertRaisesRegex(ValueError,'source/board'):
            plan.validate(raw,SHA,plan.digest(raw)['sha256'])
        result=plan.validate(raw,SHA,plan.digest(raw)['sha256'],board_id='t5s3-pro')
        self.assertIsNone(result['expected_mac']);self.assertIsNone(result['expected_partition_sha256'])
        self.assertFalse(result['physical_binding_established']);self.assertFalse(result['provisioning_authorized'])
    def test_t5_rejects_x4_and_unknown_board_profiles(self):
        raw,decoded=fixture()
        for board in ('t5s3-pro','unknown'):
            with self.subTest(board=board),self.assertRaises(ValueError):
                plan.validate(raw,SHA,plan.digest(raw)['sha256'],board_id=board)
    def test_t5_rejects_mixed_firmware_and_store_identity(self):
        for component in ('firmware','store'):
            def change(files,manifest,decoded):
                if component=='firmware':
                    files['firmware.bin']=files['firmware.bin'].replace(b't5s3-pro',b'wrong-id')
                    manifest['firmware'].update(plan.digest(files['firmware.bin']))
                else:
                    decoded['System/Config/board.json']=json.dumps({'board_id':'xteink-x4-pro'}).encode()
                    manifest['files']['System/Config/board.json']=plan.digest(decoded['System/Config/board.json'])
            raw,decoded=fixture(change,board_id='t5s3-pro')
            with self.subTest(component=component),self.assertRaisesRegex(ValueError,'[Bb]oard'):
                plan.validate(raw,SHA,plan.digest(raw)['sha256'],board_id='t5s3-pro')
    def test_wrong_source(self):
        with self.assertRaisesRegex(ValueError,'source'):self.check(sha='b'*40)
    def test_retired_internal_store_rejected(self):
        with self.assertRaisesRegex(ValueError,'schema-2'):self.check(lambda f,m,d:m.update(schema=1))
    def test_no_internal_store_extra_file(self):
        with self.assertRaisesRegex(ValueError,'inventory'):self.check(lambda f,m,d:f.update({'module-store.bin':b'old'}))
    def test_no_flash_store_fields(self):
        with self.assertRaisesRegex(ValueError,'fields'):self.check(lambda f,m,d:m.update(module_store={}))
    def test_no_app_partition_relocation(self):
        with self.assertRaisesRegex(ValueError,'firmware hash'):self.check(lambda f,m,d:m['firmware'].update(offset=0x650000))
    def test_artifact_cannot_grant_approval(self):
        with self.assertRaisesRegex(ValueError,'authorize'):self.check(lambda f,m,d:m.update(provisioning_authorized=True))
    def test_outer_traversal(self):
        with self.assertRaisesRegex(ValueError,'Unsafe'):self.check(lambda f,m,d:f.update({'../escape':b'x'}))
    def test_store_manifest_cannot_hide_package_mismatch(self):
        def change(f,m,d):
            key='Drivers/test-driver/driver.elf';d[key]+=b'altered';m['files'][key]=plan.digest(d[key])
        with self.assertRaisesRegex(ValueError,'SD/package'):self.check(change)
    def test_undeclared_store_file(self):
        def change(f,m,d):d['extra']=b'x';m['files']['extra']=plan.digest(b'x')
        with self.assertRaisesRegex(ValueError,'Unexpected/missing'):self.check(change)
    def test_sd_hidden_metadata_required(self):
        with self.assertRaisesRegex(ValueError,'SD/package'):
            self.check(lambda f,m,d:d.pop('Drivers/test-driver/.package.json'))
    def test_sd_inbox_matches_package(self):
        with self.assertRaisesRegex(ValueError,'Inbox/package'):
            self.check(lambda f,m,d:d.update({'Packages/Inbox/test-driver-1.0.0.rte.zip':b'wrong'}))
    def test_case_collision_rejected(self):
        with self.assertRaisesRegex(ValueError,'Case-colliding'):
            self.check(lambda f,m,d:d.update({'drivers/test-driver/driver.elf':b'other'}))
    def test_sd_archive_integrity_and_inventory(self):
        raw,_=fixture()
        files=plan.archive_files(raw,32,plan.APP_SIZE,plan.MAX_EXPANDED)
        m=plan.read_json(files['deployment.json'])
        for field,value in [('sha256','f'*64),('bytes',1)]:
            changed=json.loads(json.dumps(m));changed['sd_archive'][field]=value
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'SD archive hash'):
                plan.sd_files(files['sdcard.zip'],changed)
        m['files'].pop('Drivers/test-driver/.package.json')
        with self.assertRaisesRegex(ValueError,'inventory'):plan.sd_files(files['sdcard.zip'],m)
    def test_fat_path_aliases_and_control_characters_rejected(self):
        for name in ('Drivers/test/file.','Drivers/test/file ','Drivers/test/a:b','Drivers/test/ab\x01'):
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'Unsafe'):
                plan.archive_files(zip_bytes({name:b'x'}),8,1024,2048)
    def test_directory_case_alias_rejected(self):
        with self.assertRaisesRegex(ValueError,'Case-colliding'):
            plan.archive_files(zip_bytes({'Drivers/a':b'a','drivers/b':b'b'}),8,1024,2048)
    def test_nested_package_duplicate(self):
        with self.assertRaisesRegex(ValueError,'Duplicate archive'):
            raw=io.BytesIO()
            with zipfile.ZipFile(raw,'w') as z:z.writestr('file',b'a');z.writestr('file',b'b')
            plan.archive_files(raw.getvalue(),17,1024,2048)

if __name__=='__main__':unittest.main()
