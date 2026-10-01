"""Canonical schema-2 resource paths shared by packaging and index checks."""
import re

_COMPONENT = re.compile(r'[a-z0-9](?:[a-z0-9_.-]*[a-z0-9])?\Z')
MAX_DEPTH = 8
MAX_PATH_BYTES = 127


def safe_resource_path(value):
    if not isinstance(value, str) or not 0 < len(value) <= MAX_PATH_BYTES:
        return False
    parts = value.split('/')
    return len(parts) <= MAX_DEPTH and all(
        _COMPONENT.fullmatch(part) and '..' not in part for part in parts)


def parent_paths(value):
    parts = value.split('/')
    return {'/'.join(parts[:depth]) for depth in range(1, len(parts))}


def path_conflicts(value, existing):
    return any(value == old or value.startswith(old + '/') or
               old.startswith(value + '/') for old in existing)
