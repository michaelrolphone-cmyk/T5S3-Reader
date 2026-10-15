"""Offline positive/negative tests of the one-promotion release boundary."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import check_source_only_promotion as guard


class PromotionTests(unittest.TestCase):
    def run_git(self, *arguments):
        return subprocess.check_output(
            ['git', '-c', 'user.name=fixture', '-c', 'user.email=fixture@localhost',
             '-C', str(self.root), *arguments], text=True, stderr=subprocess.DEVNULL).strip()

    def put(self, path, data):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(data)

    def commit(self, amend=False):
        self.run_git('add', '-A')
        self.run_git('commit', '-q', '-m', 'fixture', *(['--amend'] if amend else []))
        return self.run_git('rev-parse', 'HEAD')

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.run_git('init', '-q')
        self.put('base.txt', 'base\n')
        before = self.commit()
        self.put('source.txt', 'approved source\n')
        source = self.commit()
        self.source_tree = self.run_git('rev-parse', 'HEAD^{tree}')
        for name, value in [('BEFORE', before), ('SOURCE', source)]:
            patcher = mock.patch.object(guard, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        for path in guard.GUARD_PATHS:
            self.put(path, '# reviewed guard fixture\n')
        self.put(guard.MANIFEST, json.dumps(guard.expected_manifest()) + '\n')
        after = self.commit()
        self.env = {'GITHUB_REPOSITORY': guard.REPOSITORY, 'GITHUB_EVENT_NAME': 'push',
                    'GITHUB_REF': 'refs/heads/master', 'PROMOTION_BEFORE': before,
                    'GITHUB_SHA': after}

    def amend(self):
        self.env['GITHUB_SHA'] = self.commit(amend=True)

    def test_exact_successor_emits_auditable_source_only_result(self):
        result = guard.evaluate(self.root, self.env)
        self.assertIs(result['source_only'], True)
        self.assertEqual(result['source_sha'], guard.SOURCE)
        self.assertEqual(result['changed_paths'], sorted(guard.GUARD_PATHS))
        self.assertEqual(result['manifest_sha256'],
                         hashlib.sha256((self.root / guard.MANIFEST).read_bytes()).hexdigest())
        audit, plan, output = [self.root / name for name in ('audit.json', 'plan.json', 'output')]
        guard.write_result(result, audit, plan, output)
        self.assertEqual(json.loads(plan.read_text()), [])
        self.assertEqual(json.loads(audit.read_text()), result)
        self.assertEqual(output.read_text(), 'source_only=true\n')

    def test_later_push_keeps_ordinary_behavior_without_consulting_old_intent(self):
        env = dict(self.env, PROMOTION_BEFORE='f' * 40)
        with mock.patch.object(guard, 'git', side_effect=AssertionError('must not inspect old range')):
            result = guard.evaluate(self.root, env)
        self.assertIs(result['source_only'], False)
        audit, plan, output = [self.root / name for name in ('audit.json', 'plan.json', 'output')]
        plan.write_text('[{"product":"firmware"}]\n')
        guard.write_result(result, audit, plan, output)
        self.assertEqual(plan.read_text(), '[{"product":"firmware"}]\n')
        self.assertEqual(output.read_text(), 'source_only=false\n')

    def test_manual_dispatch_preserves_ordinary_behavior(self):
        env = dict(self.env, GITHUB_EVENT_NAME='workflow_dispatch', PROMOTION_BEFORE='')
        with mock.patch.object(guard, 'git', side_effect=AssertionError('manual path is unchanged')):
            self.assertIs(guard.evaluate(self.root, env)['source_only'], False)

    def test_wrong_repository_event_branch_or_commit_identity_rejected(self):
        for key, value in [('GITHUB_REPOSITORY', 'other/repo'),
                           ('GITHUB_EVENT_NAME', 'pull_request'),
                           ('GITHUB_REF', 'refs/heads/main'),
                           ('GITHUB_REF', 'refs/heads/work/native-idf-6.1'),
                           ('PROMOTION_BEFORE', ''), ('GITHUB_SHA', 'not-a-sha')]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                guard.evaluate(self.root, dict(self.env, **{key: value}))

    def test_checkout_must_match_event_after(self):
        with self.assertRaisesRegex(ValueError, 'checkout differs'):
            guard.evaluate(self.root, dict(self.env, GITHUB_SHA=guard.SOURCE))

    def test_dirty_tracked_checkout_rejected(self):
        self.put('source.txt', 'unreviewed source\n')
        with self.assertRaisesRegex(ValueError, 'tracked modifications'):
            guard.evaluate(self.root, self.env)

    def test_extra_successor_rejected(self):
        self.run_git('commit', '--allow-empty', '-q', '-m', 'extra successor')
        self.env['GITHUB_SHA'] = self.run_git('rev-parse', 'HEAD')
        with self.assertRaisesRegex(ValueError, 'direct single-parent'):
            guard.evaluate(self.root, self.env)

    def test_merge_commit_rejected(self):
        tree = self.run_git('rev-parse', 'HEAD^{tree}')
        merge = self.run_git('commit-tree', tree, '-p', guard.SOURCE, '-p', guard.BEFORE, '-m', 'merge')
        self.run_git('reset', '--hard', merge)
        self.env['GITHUB_SHA'] = merge
        with self.assertRaisesRegex(ValueError, 'direct single-parent'):
            guard.evaluate(self.root, self.env)

    def test_source_must_preserve_master_ancestry(self):
        orphan = self.run_git('commit-tree', self.source_tree, '-m', 'orphan')
        tree = self.run_git('rev-parse', 'HEAD^{tree}')
        after = self.run_git('commit-tree', tree, '-p', orphan, '-m', 'guard')
        self.run_git('reset', '--hard', after)
        self.env['GITHUB_SHA'] = after
        with mock.patch.object(guard, 'SOURCE', orphan), \
                self.assertRaises((ValueError, subprocess.CalledProcessError)):
            guard.evaluate(self.root, self.env)

    def test_unexpected_source_change_rejected(self):
        self.put('source.txt', 'new unqualified production source\n')
        self.amend()
        with self.assertRaisesRegex(ValueError, 'unexpected or missing guard paths'):
            guard.evaluate(self.root, self.env)

    def test_missing_guard_path_rejected(self):
        (self.root / 'docs/NATIVE_SOURCE_PROMOTION.md').unlink()
        self.amend()
        with self.assertRaisesRegex(ValueError, 'unexpected or missing guard paths'):
            guard.evaluate(self.root, self.env)

    def test_symlink_guard_input_rejected(self):
        path = self.root / 'docs/NATIVE_SOURCE_PROMOTION.md'
        path.unlink()
        path.symlink_to('../source.txt')
        self.amend()
        with self.assertRaisesRegex(ValueError, 'regular tracked file'):
            guard.evaluate(self.root, self.env)

    def test_manifest_fields_types_extras_and_duplicates_rejected(self):
        valid = guard.expected_manifest()
        for key in valid:
            invalid = copy.deepcopy(valid)
            invalid[key] = True if key == 'schema' else 'wrong'
            with self.subTest(field=key), self.assertRaises(ValueError):
                guard.parse_manifest(json.dumps(invalid))
            invalid = copy.deepcopy(valid)
            del invalid[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                guard.parse_manifest(json.dumps(invalid))
        for raw in ('[]', '{}', '{bad', json.dumps(dict(valid, enabled=True)),
                    json.dumps(valid)[:-1] + ', "schema": 1}'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                guard.parse_manifest(raw)

    def test_committed_manifest_mismatch_rejected(self):
        value = guard.expected_manifest()
        value['source_sha'] = 'f' * 40
        self.put(guard.MANIFEST, json.dumps(value))
        self.amend()
        with self.assertRaisesRegex(ValueError, 'manifest identity differs'):
            guard.evaluate(self.root, self.env)


class CheckedInRoutingTests(unittest.TestCase):
    def test_manifest_binds_reviewed_public_source_and_old_master(self):
        self.assertEqual(guard.parse_manifest((ROOT / guard.MANIFEST).read_bytes()),
                         guard.expected_manifest())

    def test_workflow_guards_planning_and_publication_and_saves_audit(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertIn('source_only: ${{ steps.intent.outputs.source_only }}', workflow)
        self.assertIn("if: steps.intent.outputs.source_only != 'true'", workflow)
        self.assertIn("needs.plan.outputs.source_only != 'true'", workflow)
        self.assertIn('PROMOTION_BEFORE: ${{ github.event.before }}', workflow)
        self.assertIn('${{ runner.temp }}/source-promotion.json', workflow)
        self.assertLess(workflow.index('scripts/check_source_only_promotion.py'),
                        workflow.index('scripts/publish_updated_packages.py --plan-only'))
        self.assertIn('test/source_only_promotion_test.py',
                      (ROOT / '.github/workflows/shared-sdk61.yml').read_text())


if __name__ == '__main__':
    unittest.main()
