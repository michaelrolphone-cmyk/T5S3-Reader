#!/usr/bin/env python3
"""Regression guards for the Driver Manager recovery/stack/UI incident."""
import json
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
resolver = (repo / 'src/runtime/packages/InstalledCapabilityResolver.cpp').read_text()
recovery = (repo / 'src/native/NativeOnlinePackageRecovery.h').read_text()
ui = (repo / 'Apps/driver_manager.c').read_text()
manifest = json.loads((repo / 'Apps/driver_manager.json').read_text())

# The real installed-byte verifier must run only while constructing a fresh
# per-query snapshot. Dependency traversal may not open SD handles, allocate a
# manifest or recursively hash the same candidate for every nested edge.
assert 'bool snapshotCandidates(' in resolver
snapshot = resolver.split('bool snapshotCandidates(', 1)[1].split('uint32_t resolveSnapshot(', 1)[0]
graph = resolver.split('uint32_t resolveSnapshot(', 1)[1]
assert snapshot.count('verifyOrdinarySdDirectory(') == 1
assert 'UINT32_MAX' in snapshot and 'parseOrdinaryManifest(' in snapshot
assert 'plan->requirements + plan->requirementCount' in snapshot
assert 'depth >= kMaxDepth' in graph and 'ancestry[depth] = index;' in graph
assert 'verifyOrdinarySdDirectory(' not in graph
assert 'Storage.open(' not in graph
assert 'snapshotCandidates(candidates)' in resolver
assert 'static std::vector<Candidate>' not in resolver

# Interrupted stages must remain untouched on OOM and retain the existing
# exact byte comparison before removal. No nested 1 KiB stack workspace.
comparison = recovery.split('inline bool stagePrefixMatches(', 1)[1].split('inline bool discardMatchingStage(', 1)[0]
assert 'new (std::nothrow) uint8_t[1024]' in comparison
assert 'if (!buffers)' in comparison
assert 'std::memcmp(lhs, rhs, n)' in comparison
assert 'uint8_t lhs[512]' not in comparison

# The initial e-paper draw must PRECEDE installed-version verification; status
# labels must never perform SD IO from a routine UI render. New app bytes must
# be distinguishable from the prior release to enable an update.
activate = ui.split('static void activate(', 1)[1].split('static void header(', 1)[0]
assert activate.index('begin_install_progress(') < activate.index('action_for(')
assert activate.index('ui->render_list(&busy, &row, 1, 0)') < activate.index('action_for(')
labels = ui.split('static const char *action_label(', 1)[1].split('static void render(', 1)[0]
assert 'release_actions[selected]' in labels
assert 'action_for(' not in labels and 'installed_version_get' not in labels
assert 'T5_DRIVER_INSTALL_METADATA ||' in ui
assert 'T5_DRIVER_INSTALL_RECOVERY ||' in ui
assert manifest['version'] == '1.0.4'
print('Driver install regression: bounded snapshot, stage recovery, immediate UI and app version PASS')
