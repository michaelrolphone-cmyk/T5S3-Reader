"""Shared lab locks. No device access."""
from contextlib import contextmanager
import fcntl
import hashlib
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent / "cam"))
import ci_device as cam_device


@contextmanager
def locks(port, mac, directory=None):
    """Use existing lab directory/hash keys, including both macOS aliases.

    Acquires all keys before any caller I/O, nonblocking, releasing partial
    acquisition on contention. Never unlink a lock file while peers may use it.
    """
    directory = cam_device.LOCK_DIR if directory is None else directory
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    aliases = {port, port.replace("/dev/tty.", "/dev/cu."), port.replace("/dev/cu.", "/dev/tty.")}
    handles = []
    try:
        for key in sorted({"mac:" + mac.lower()} | {"port:" + p for p in aliases}):
            handle = (directory / (hashlib.sha256(key.encode()).hexdigest() + ".lock")).open("a+")
            handles.append(handle)
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        for handle in reversed(handles):
            handle.close()

