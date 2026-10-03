import contextlib
import io
import json
import unittest
from unittest.mock import Mock, patch
import x4_paired_cycle as cycle


class RetiredPairTests(unittest.TestCase):
    def test_old_prepare_cannot_accept_an_artifact(self):
        artifact=Mock()
        with self.assertRaisesRegex(ValueError,'retired'):cycle.prepare(artifact)
        artifact.assert_not_called()
        self.assertEqual(artifact.mock_calls,[])

    def test_old_callable_refuses_before_any_transport(self):
        transport=Mock()
        with self.assertRaisesRegex(ValueError,'retired'):
            cycle.cycle(Mock(),transport,None,None,None,None,None,None)
        self.assertEqual(transport.mock_calls,[])

    def test_cli_approval_flag_cannot_revive_store_writes(self):
        output=io.StringIO()
        with patch('sys.argv',['x4_paired_cycle.py','--approval-sha','ec0c099']),contextlib.redirect_stdout(output):
            self.assertEqual(cycle.main(),1)
        result=json.loads(output.getvalue())
        self.assertFalse(result['hardware_access_performed'])
        self.assertFalse(result['write_attempted'])
        self.assertEqual(result['result'],'refused')


if __name__=='__main__':unittest.main()
