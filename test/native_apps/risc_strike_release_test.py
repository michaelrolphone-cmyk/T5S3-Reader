"""Verify the built Risc Strike pair can be published and indexed offline."""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.build_release_record import build_record
from scripts.publish_updated_packages import discover_candidates, release_assets
from scripts.update_release_index import update_index

source = json.loads((ROOT / "Apps/risc_strike.json").read_text())
version = source["version"]
candidate = {"product": "apps", "id": "risc_strike", "version": version}
empty = {"schema": 1, "firmware": None, "apps": [], "drivers": []}
assert candidate in discover_candidates(ROOT, empty)

record = build_record("apps", "risc_strike", version, ROOT)
assets = release_assets(ROOT, "apps", "risc_strike")
assert {path.name for path in assets} == {"risc_strike.elf", "risc_strike.json"}
for key, value in source.items():
    assert record["manifest"][key] == value, key
payload = (ROOT / "dist/apps/risc_strike.elf").read_bytes()
assert record["size"] == record["manifest"]["size_bytes"] == len(payload)
assert record["sha256"] == record["manifest"]["sha256"] == hashlib.sha256(payload).hexdigest()
assert record["tag"] == f"app-risc_strike-v{version}"
indexed = update_index(empty, "apps", record)
assert indexed["apps"] == [{**record, "kind": "app"}]
assert candidate not in discover_candidates(ROOT, indexed)
assert update_index(indexed, "apps", record) == indexed
print(f"Risc Strike {version}: discovery, built assets, hashes, index insertion and retry PASS")
