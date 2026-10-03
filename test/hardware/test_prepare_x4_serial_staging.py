import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import prepare_x4_serial_staging as prep
import x4_serial_staging as s
from test_x4_serial_staging import archive

MAIN='''void loop() {
  // Handle incoming serial commands,
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\\n');
    if (line.startsWith("CMD:")) {
      if (line == "CMD:SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.write(display.getFrameBuffer(), bufferSize);
      }
    }
  }

  // Check for any user activity
  static unsigned long lastActivityTime = millis();
}
'''


class PreparerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)/'fixture';(self.root/'src').mkdir(parents=True)
        (self.root/'src/main.cpp').write_text(MAIN)
        (self.root/'platformio.local.ini').write_text('; local overrides (empty in CI)')
        self.packages=Path(self.tmp.name)/'packages';self.packages.mkdir()
        self.archives={n:archive(n) for n in s.IDS}
        for n,data in self.archives.items():(self.packages/f'application-{n}-1.0.8-xtensa-esp32s3.rte.zip').write_bytes(data)
        self.artifact=Path(self.tmp.name)/'artifact.zip';self.artifact.write_bytes(b'fixture')
        self.validation={'sd_archive':{'sha256':s.SD_ARCHIVE},'sd_file_count':s.SD_FILES,
                         'sd_files':{'Drivers/test/.package.json':{'bytes':2,'sha256':s.sha(b'{}')}}}
        self.patches=[patch.object(s,'ARCHIVES',{n:(len(d),s.sha(d)) for n,d in self.archives.items()}),
                      patch.object(prep.plan,'validate',return_value=self.validation),
                      patch.object(prep.subprocess,'check_output',side_effect=self.git)]
        for p in self.patches:p.start();self.addCleanup(p.stop)

    def git(self,args,**kwargs):
        if args[1]=='rev-parse':return s.SOURCE+'\n'
        if args[1]=='status':return ''
        raise AssertionError(args)

    def prepare(self):return prep.prepare(self.root,self.packages,self.artifact)

    def test_ci_builder_never_embeds_retired_providers(self):
        text=(Path(prep.__file__).parent/'x4/build_candidate.sh').read_text()
        self.assertNotIn('python scripts/embed_x4pro_providers.py',text)
        self.assertIn('test -f src/platform/SdPackageBoot.cpp',text)
        self.assertIn('python scripts/stage_x4pro_packages.py',text)

    def test_build_identity_and_single_macro_guarded_rx(self):
        result=self.prepare()
        prep.plan.validate.assert_called_once_with(b'fixture',s.SOURCE,s.DEPLOYMENT_ARCHIVE)
        self.assertEqual(result['production_base'],s.SOURCE)
        self.assertEqual(result['expected_sd_archive_sha256'],s.SD_ARCHIVE)
        self.assertFalse(result['hardware_performed'])
        self.assertIsNone(result['firmware_sha256'])
        identity=(self.root/'test/hardware/reader/StageIdentity.h').read_text()
        self.assertIn('stageSdFiles',identity);self.assertIn('Drivers/test/.package.json',identity)
        self.assertNotIn('stageStore',identity)
        main=(self.root/'src/main.cpp').read_text()
        self.assertEqual(main.count('if (readerStageTick())'),1)
        self.assertIn('#else\n  // Handle incoming serial commands,',main)
        self.assertEqual(main.count('if (readerStageActive())'),1)
        self.assertIn('extends = env:xteink-x4-pro',(self.root/'platformio.local.ini').read_text())

    def test_incomplete_sd_inventory_refuses_before_writes(self):
        self.validation['sd_file_count']=49
        with self.assertRaisesRegex(s.ProtocolError,'inventory'):self.prepare()
        self.assertEqual((self.root/'src/main.cpp').read_text(),MAIN)
        self.assertFalse((self.root/'test').exists())

    def test_wrong_source_refuses_before_writes(self):
        with patch.object(prep.subprocess,'check_output',return_value='e'*40):
            with self.assertRaisesRegex(s.ProtocolError,'base differs'):self.prepare()
        self.assertFalse((self.root/'test').exists())

    def test_raw_tree_or_unverified_artifact_is_not_accepted(self):
        self.artifact.unlink();self.artifact.mkdir()
        with self.assertRaisesRegex(s.ProtocolError,'artifact path'):self.prepare()
        prep.plan.validate.assert_not_called()

    def test_existing_local_profile_is_preserved(self):
        (self.root/'platformio.local.ini').write_text('user settings')
        with self.assertRaisesRegex(s.ProtocolError,'local profile'):self.prepare()
        self.assertEqual((self.root/'platformio.local.ini').read_text(),'user settings')
        self.assertFalse((self.root/'test').exists())

    def test_refresh_refuses_changed_instrumentation(self):
        self.prepare();(self.root/'test/hardware/reader/SerialStaging.cpp').write_text('user edit')
        with self.assertRaisesRegex(s.ProtocolError,'source changed'):
            prep.refresh(self.root,self.packages,self.artifact)
        self.assertEqual((self.root/'test/hardware/reader/SerialStaging.cpp').read_text(),'user edit')


if __name__=='__main__':unittest.main()
