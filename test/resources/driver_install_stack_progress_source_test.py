#!/usr/bin/env python3
"""Source guards for installed-capability snapshot, recovery and generic driver UI."""
import json
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
resolver = (repo / 'src/runtime/packages/InstalledCapabilityResolver.cpp').read_text()
recovery = (repo / 'src/native/NativeOnlinePackageRecovery.h').read_text()
ui = (repo / 'Apps/driver_manager.c').read_text()
manifest = json.loads((repo / 'Apps/driver_manager.json').read_text())

# Dependency traversal uses one operation-scoped bounded snapshot rather than
# repeatedly opening and hashing packages at every graph edge.
assert 'bool snapshotCandidates(' in resolver
snapshot = resolver.split('bool snapshotCandidates(', 1)[1].split('uint32_t resolveSnapshot(', 1)[0]
graph = resolver.split('uint32_t resolveSnapshot(', 1)[1]
assert snapshot.count('verifyOrdinarySdDirectory(') == 1
assert 'UINT32_MAX' in snapshot and 'parseOrdinaryManifest(' in snapshot
assert 'plan->requirements + plan->requirementCount' in snapshot
assert 'depth >= kMaxDepth' in graph and 'ancestry[depth] = index;' in graph
assert 'verifyOrdinarySdDirectory(' not in graph
assert 'Storage.open(' not in graph
assert 'snapshotCandidates(snapshot->candidates)' in resolver
assert 'releaseInstalledCapabilities(snapshot)' in resolver
assert 'static std::vector<Candidate>' not in resolver

# Failed/interrupted stages are inspected with heap-bounded buffers. No file
# deletion follows an unverified partial comparison or allocation failure.
comparison = recovery.split('inline bool stagePrefixMatches(', 1)[1].split('inline bool discardMatchingStage(', 1)[0]
assert 'new (std::nothrow) uint8_t[1024]' in comparison
assert 'if (!buffers)' in comparison
assert 'std::memcmp(lhs, rhs, n)' in comparison
assert 'uint8_t lhs[512]' not in comparison

# The Driver Manager renders a busy screen BEFORE the blocking shared ZIP
# operation. Normal discovery/install uses the package API exclusively; the
# old driver ABI remains limited to explicit offline stage recovery.
activate = ui.split('static void activate(', 1)[1].split('static void header(', 1)[0]
assert activate.index('ui->render_list(&busy, &waiting, 1, 0)') < activate.index('manager->online_install(')
assert 'manager->install_archive(' in activate and 'manager->install(' in activate
assert 'manager->uninstall(T5_PACKAGE_DRIVER,' in activate
assert 'catalog_refresh(' not in ui and 'install_with_progress(' not in ui
assert 'online_refresh(' in ui and 'online_get(' in ui and 'online_install(' in ui
assert 'offsetof(t5_package_manager_api_v1, online_install)' in ui
assert 'driver->recovery_retry(' in ui and 'driver->recovery_discard(' in ui
assert 'confirm(ui, item.id, "Discard retained files")' in ui
assert 'static const char *recovery_state(' in ui
render = ui.split('static void render(', 1)[1].split('static void activate(', 1)[0]
assert 'Storage.open(' not in render and 'verifyOrdinarySdDirectory(' not in render
assert tuple(map(int, manifest['version'].split('.'))) > (1, 0, 4)
print('Driver install regression: bounded snapshot, recovery, generic catalog and UI contract PASS')
