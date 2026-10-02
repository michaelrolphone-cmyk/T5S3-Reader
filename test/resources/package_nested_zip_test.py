"""Real schema-2 nested packer bounds and immutable manifest/index contract."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import io
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scripts.pack_rte_zip import pack_directory
from scripts.update_release_index import validate_bundle_manifest

class NestedZipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.files = {'sample.elf': b'\x7fELF'+bytes(60), 'sample.json': b'{}',
                      'assets/fonts/body.bin': b'font resource',
                      'assets/images/logo.bin': b'image resource'}
        self.manifest = dict(schema=2, kind='application', id='sample', version='1.0.0',
            architecture='xtensa-esp32s3', artifact='sample.elf', min_runtime_api=2,
            requires=[], entries=[dict(name=name, size_bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(), executable=name=='sample.elf')
                for name,data in self.files.items()])
        for name,data in self.files.items():
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        self.write_manifest()
    def tearDown(self):
        self.temp.cleanup()
    def write_manifest(self):
        (self.root/'.package.json').write_text(json.dumps(self.manifest,separators=(',',':')))
    def test_deterministic_nested_tree_and_manifest(self):
        archive=pack_directory(self.root)
        self.assertEqual(archive,pack_directory(self.root))
        with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
            self.assertEqual(set(zipped.namelist()),set(self.files)|{'.package.json'})
            for name,data in self.files.items():self.assertEqual(zipped.read(name),data)
        validate_bundle_manifest(self.manifest,'sample','1.0.0','xtensa-esp32s3','application')
    def test_schema_one_does_not_gain_nested_semantics(self):
        self.manifest['schema']=1;self.write_manifest()
        with self.assertRaises(ValueError):pack_directory(self.root)
        with self.assertRaises(ValueError):validate_bundle_manifest(self.manifest,'sample','1.0.0','xtensa-esp32s3','application')
    def test_unknown_tree_preserved_and_rejected(self):
        for name in ['owner.txt','assets/fonts/owner.txt','private/owner.txt']:
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'owner')
            with self.assertRaises(ValueError):pack_directory(self.root)
            self.assertEqual(path.read_bytes(),b'owner');path.unlink()
        with self.assertRaises(ValueError):pack_directory(self.root) # unknown empty private directory
    def test_parent_link_refused(self):
        folder=self.root/'assets/fonts';(folder/'body.bin').unlink();folder.rmdir()
        with tempfile.TemporaryDirectory() as target:
            (Path(target)/'body.bin').write_bytes(self.files['assets/fonts/body.bin'])
            folder.symlink_to(target,target_is_directory=True)
            with self.assertRaises(ValueError):pack_directory(self.root)
    def test_paths_roles_and_prefix_conflicts(self):
        original=self.manifest['entries'][2]['name']
        for bad in ['assets//x','assets/../x','/assets/x','Assets/x','assets./x',
                    'a/b/c/d/e/f/g/h/i.bin','sample.elf/x','assets/other.elf']:
            self.manifest['entries'][2]['name']=bad
            with self.subTest(path=bad),self.assertRaises(ValueError):
                validate_bundle_manifest(self.manifest,'sample','1.0.0','xtensa-esp32s3','application')
        self.manifest['entries'][2]['name']=original
    def test_missing_and_corrupt_file(self):
        path=self.root/'assets/fonts/body.bin';path.write_bytes(b'wrong')
        with self.assertRaises(ValueError):pack_directory(self.root)
        path.unlink()
        with self.assertRaises(ValueError):pack_directory(self.root)

if __name__=='__main__':unittest.main()
