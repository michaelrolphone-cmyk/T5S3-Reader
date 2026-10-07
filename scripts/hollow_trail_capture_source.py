"""Verified code identity for Hollow Trail evidence captions (not release identity)."""
import hashlib
import json
import pathlib
import subprocess


def source_identity(root, source_files, source_ref=None):
    root = pathlib.Path(root)
    snapshot = hashlib.sha256(json.dumps(source_files, sort_keys=True).encode()).hexdigest()
    result = {'source_commit': None, 'source_snapshot_sha256': snapshot}
    try:
        commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', source_ref or 'HEAD'], stderr=subprocess.DEVNULL, text=True).strip()
        for path, expected in source_files.items():
            data = subprocess.check_output(['git', '-C', str(root), 'show', commit+':'+path], stderr=subprocess.DEVNULL)
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError('App snapshot differs from '+commit+' at '+path)
        result['source_commit'] = commit
    except (OSError, subprocess.CalledProcessError, ValueError):
        if source_ref:
            raise ValueError('Requested source ref does not match captured app bytes: '+source_ref)
        # Never stamp HEAD on uncommitted app changes or an unidentified archive.
    return result


def source_caption(meta):
    version = meta['version']
    if meta.get('source_commit'):
        return version+' | source '+meta['source_commit'][:12]
    if meta.get('source_snapshot_sha256'):
        return version+' | app snapshot '+meta['source_snapshot_sha256'][:12]
    return version+' | source not recorded'
