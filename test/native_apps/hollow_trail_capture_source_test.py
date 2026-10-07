"""Caption identity may name a commit only when its captured bytes agree."""
import hashlib
import pathlib
import sys
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from hollow_trail_capture_source import source_identity, source_caption
files={'Apps/hollow_trail.json':hashlib.sha256((root/'Apps/hollow_trail.json').read_bytes()).hexdigest()}
identity=source_identity(root,files,'HEAD')
assert identity['source_commit'] and len(identity['source_commit'])==40
assert identity['source_commit'][:12] in source_caption({'version':'1.1.49',**identity})
wrong={'Apps/hollow_trail.json':'0'*64}
unknown=source_identity(root,wrong)
assert unknown['source_commit'] is None and 'app snapshot' in source_caption({'version':'1.1.49',**unknown})
try:
 source_identity(root,wrong,'HEAD')
except ValueError:
 pass
else:
 raise AssertionError('Incorrect requested source ref accepted')
print('Capture identity: verified commit, exact snapshot fallback and incorrect-ref rejection PASS')
