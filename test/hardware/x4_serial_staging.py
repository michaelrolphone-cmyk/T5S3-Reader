"""Host half of the CI-only two-application staging protocol.

No port is opened by this module. The device owner supplies an exclusively
locked, identity-checked channel with read/write timeouts <= 1 second, and
restores heartbeat in its outer finally block. Firmware endpoint is separate.
No CLI, network listener, reset, arbitrary path, delete, or shell operation.
"""
import base64
import hashlib
import io
import json
import re
import secrets
import time
import zipfile

PREFIX = b"RTE_STAGE_V1 "
MAX_LINE = 3072
CHUNK = 512
MAX_RX = 2 * 1024 * 1024
MAX_REQUESTS = 512
MAX_SESSION_SECONDS = 180
MAX_PINS = 16384
IDS = ("driver_manager", "app_store")
SOURCE = "67d0fd3e9f4012eae681e7d284d2d0bb39732dfb"
SD_ARCHIVE = "f55daa10cdcc67eceba27b96e1ff17c4e9acf9c75ad55062bc72954d4332a8d8"
DEPLOYMENT_ARCHIVE = "ca478cf829e2c78b81f3a4f80134119522d4863ea10404d06f6b006adf180efa"
SD_FILES = 50
ARCHIVES = {
    "driver_manager": (12949, "30591aed79ba0e85cd51235fd031cb6549ac23701f7f7737c46e4f138b48460e"),
    "app_store": (10077, "bd05715b3d7e1177f9c2c399c6bdbf78367838f44e3a3455171614db63694d47"),
}
HEX = re.compile(r"[0-9a-f]{64}\Z")
TOKEN = re.compile(r"[0-9a-f]{32}\Z")


class ProtocolError(RuntimeError):
    pass


class UncertainOperation(ProtocolError):
    """Lost/invalid response: stop; reconnect with fresh inventory, never replay."""


def require(ok, reason):
    if not ok:
        raise ProtocolError(reason)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def object_pairs(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, "Duplicate JSON key")
        value[key] = item
    return value


def encode(value):
    raw = PREFIX + json.dumps(value, separators=(",", ":"), sort_keys=True,
                              allow_nan=False).encode("ascii") + b"\n"
    require(len(raw) <= MAX_LINE, "Frame exceeds bound")
    return raw


def decode(raw):
    require(len(raw) <= MAX_LINE and raw.startswith(PREFIX) and raw.endswith(b"\n"), "Bad frame")
    try:
        value = json.loads(raw[len(PREFIX):], object_pairs_hook=object_pairs,
                           parse_constant=lambda _: (_ for _ in ()).throw(ProtocolError("Nonfinite JSON")))
    except (ValueError, RecursionError, UnicodeError) as error:
        raise ProtocolError("Malformed JSON") from error
    require(isinstance(value, dict), "Expected JSON object")
    return value


def descriptor(value, limit):
    require(isinstance(value, dict) and set(value) == {"bytes", "sha256"}, "Invalid descriptor")
    require(type(value["bytes"]) is int and 0 <= value["bytes"] <= limit, "File bound exceeded")
    require(isinstance(value["sha256"], str) and HEX.fullmatch(value["sha256"]), "Invalid digest")
    return value


def merge_pins(before):
    """Preserve original bytes; append only missing exact Reader ELF basenames."""
    require(isinstance(before, bytes) and len(before) <= MAX_PINS, "Pin byte bound exceeded")
    require(b"\0" not in before, "Pin file contains NUL")
    lines = re.split(b"[\r\n]", before)
    # Reader uses exact CR/LF-delimited basenames; preserve all bytes.
    present = {line for line in lines if line}
    missing = [(name + ".elf").encode() for name in IDS if (name + ".elf").encode() not in present]
    require(sum(bool(line) for line in lines) + len(missing) <= 128, "Pin item bound exceeded")
    after = before
    if missing:
        if after and not after.endswith(b"\n"):
            after += b"\n"
        after += b"\n".join(missing) + b"\n"
    require(len(after) <= MAX_PINS, "Updated pins exceed bound")
    return after


def validate_archives(archives):
    require(set(archives) == set(IDS), "Exactly two approved archives required")
    for name, data in archives.items():
        require(isinstance(data, bytes), "Archive must be immutable bytes")
        size, digest = ARCHIVES[name]
        require(len(data) == size and sha(data) == digest, "Archive custody mismatch: " + name)


def package_inventory(data, name):
    # Call only after exact immutable archive hash verification. No extraction.
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        require(len(names) == 3 and set(names) == {".package.json", name + ".elf", name + ".json"}, "Unexpected package members")
        require(all(i.file_size <= 65536 for i in archive.infolist()), "Expanded member bound exceeded")
        return {key: {"bytes": len(value), "sha256": sha(value)}
                for key in names for value in [archive.read(key)]}


class Client:
    def __init__(self, channel, utility_id, clock=time.monotonic):
        require(isinstance(utility_id, str) and HEX.fullmatch(utility_id), "Expected utility source/build ID")
        self.channel = channel
        self.utility_id = utility_id
        self.clock = clock
        self.deadline = clock() + MAX_SESSION_SECONDS
        self.session = secrets.token_hex(16)
        self.sequence = 0
        self.rx = 0
        self.pending = bytearray()
        self.poisoned = False
        self.ready = False

    def _request(self, op, args):
        require(not self.poisoned, "Session uncertain; reconnect and inventory")
        require(self.clock() < self.deadline and self.sequence < MAX_REQUESTS, "Session bound exceeded")
        self.sequence += 1
        frame = encode({"session": self.session, "sequence": self.sequence, "op": op, "args": args})
        # Never retry a partial write or lost mutation response. Device-side
        # journals/offset hashes establish progress after a NEW inventory.
        try:
            count = self.channel.write(frame)
            require(count == len(frame), "Partial request write")
            until = min(self.deadline, self.clock() + 15)
            while self.clock() < until:
                if b"\n" not in self.pending:
                    chunk = self.channel.read(256)
                    require(isinstance(chunk, bytes) and len(chunk) <= 256, "Invalid transport read")
                    self.rx += len(chunk)
                    require(self.rx <= MAX_RX, "Session receive bound exceeded")
                    self.pending.extend(chunk)
                    require(len(self.pending) <= MAX_LINE + 256, "Unterminated/oversized serial line")
                    if not chunk:
                        time.sleep(.005)
                    continue
                line, _, rest = self.pending.partition(b"\n")
                self.pending = bytearray(rest)
                require(len(line) + 1 <= MAX_LINE, "Oversized serial line")
                if not line.startswith(PREFIX):
                    continue  # bounded one-way Reader diagnostics, never commands
                reply = decode(bytes(line) + b"\n")
                require(set(reply) == {"session", "sequence", "ok", "result"}, "Bad response fields")
                require(reply["session"] == self.session and type(reply["sequence"]) is int
                        and reply["sequence"] == self.sequence, "Stale or foreign response")
                require(type(reply["ok"]) is bool and isinstance(reply["result"], dict), "Bad response types")
                require(reply["ok"], "Device refused operation")
                return reply["result"]
            raise ProtocolError("Response deadline exceeded")
        except Exception as error:
            self.poisoned = True
            raise UncertainOperation(str(error)) from error

    def hello(self):
        value = self._request("hello", {"source_sha": SOURCE, "sd_archive_sha256": SD_ARCHIVE,
                                        "utility_id": self.utility_id})
        require(value == {"source_sha": SOURCE, "sd_archive_sha256": SD_ARCHIVE,
                          "utility_id": self.utility_id, "protocol": 1, "sd_verified_files": SD_FILES,
                          "scope": list(IDS)}, "Utility/build/SD verification mismatch")
        self.ready = True

    def inventory(self):
        require(self.ready, "Handshake required")
        value = self._request("inventory", {})
        require(set(value) == {"snapshot", "apps", "pins", "pins_exists"}, "Bad inventory")
        require(isinstance(value["snapshot"], str) and TOKEN.fullmatch(value["snapshot"]), "Bad snapshot token")
        require(type(value["pins_exists"]) is bool and set(value["apps"]) == set(IDS), "Bad inventory scope")
        descriptor(value["pins"], MAX_PINS)
        if not value["pins_exists"]:
            require(value["pins"] == {"bytes": 0, "sha256": sha(b"")}, "Absent pin mismatch")
        for name, app in value["apps"].items():
            require(set(app) == {"state", "files"} and app["state"] in ("absent", "matching", "conflict"), "Bad app state")
            allowed = {".package.json", name + ".elf", name + ".json"}
            require(isinstance(app["files"], dict) and set(app["files"]) <= allowed, "Unexpected file selector")
            for item in app["files"].values():
                descriptor(item, 65536)
            if app["state"] == "absent":
                require(not app["files"], "Absent app has files")
            if app["state"] == "matching":
                require(set(app["files"]) == allowed, "Matching app incomplete")
        return value

    def _read(self, snapshot, selector, info):
        output = bytearray()
        while len(output) < info["bytes"]:
            count = min(CHUNK, info["bytes"] - len(output))
            reply = self._request("read", {"snapshot": snapshot, "selector": selector,
                                            "offset": len(output), "count": count})
            require(set(reply) == {"data"} and isinstance(reply["data"], str), "Bad read response")
            try:
                data = base64.b64decode(reply["data"], validate=True)
            except ValueError as e:
                raise ProtocolError("Bad read encoding") from e
            require(len(data) == count, "Short/oversized read")
            output.extend(data)
        result = bytes(output)
        require(sha(result) == info["sha256"], "Read-back hash mismatch")
        return result

    def backup(self, inventory):
        """Caller must durably save returned bytes BEFORE calling stage()."""
        snapshot = inventory["snapshot"]
        result = {"pins": self._read(snapshot, "pins", inventory["pins"])}
        for name, app in inventory["apps"].items():
            for filename, info in app["files"].items():
                result[name + "/" + filename] = self._read(snapshot, name + "/" + filename, info)
        return result

    def stage(self, inventory, backup, archives, persist_backup):
        """Persist callback must durably save inventory + bytes or raise.

        Conflicts abort the entire cycle before any SD mutation. Publication is
        per package, not an all-or-nothing two-app transaction. A lost response
        stops the session; subsequent inventory discovers completed packages.
        """
        validate_archives(archives)
        expected = {name: package_inventory(data, name) for name, data in archives.items()}
        require(self.ready, "Handshake required")
        require(not any(a["state"] == "conflict" for a in inventory["apps"].values()), "Existing content conflict")
        for name, app in inventory["apps"].items():
            if app["state"] == "matching":
                require(app["files"] == expected[name], "Existing package hashes differ from approved archive")
        require(sha(backup["pins"]) == inventory["pins"]["sha256"], "Pin backup mismatch")
        for name, app in inventory["apps"].items():
            for filename, info in app["files"].items():
                data = backup[name + "/" + filename]
                require(len(data) == info["bytes"] and sha(data) == info["sha256"], "App backup mismatch")
        require(persist_backup(inventory, backup) is True, "Durable backup not confirmed")
        pins = merge_pins(backup["pins"])
        snapshot = inventory["snapshot"]
        for name in IDS:
            if inventory["apps"][name]["state"] == "matching":
                continue
            data = archives[name]
            args = {"snapshot": snapshot, "id": name, "bytes": len(data), "sha256": sha(data)}
            require(self._request("begin", args) == {"offset": 0}, "Existing transfer needs explicit reconciliation")
            for offset in range(0, len(data), CHUNK):
                chunk = data[offset:offset + CHUNK]
                reply = self._request("chunk", {"id": name, "offset": offset,
                    "data": base64.b64encode(chunk).decode(), "sha256": sha(chunk)})
                require(reply == {"offset": offset + len(chunk)}, "Transfer offset mismatch")
            require(self._request("install", {"id": name, "snapshot": snapshot}) ==
                    {"state": "installed", "archive_sha256": sha(data)}, "Install verification failed")
        require(self._request("pins", {"snapshot": snapshot, "before_sha256": sha(backup["pins"]),
                    "before_exists": inventory["pins_exists"], "after_sha256": sha(pins)}) ==
                {"sha256": sha(pins), "bytes": len(pins)}, "Pin publication failed")
        final = self.inventory()
        require(all(a["state"] == "matching" for a in final["apps"].values()), "Final package verification failed")
        require(all(a["files"] == expected[name] for name, a in final["apps"].items()), "Final package hash inventory mismatch")
        require(final["pins_exists"] and final["pins"] == {"bytes": len(pins), "sha256": sha(pins)}, "Final pins mismatch")
        require(self._read(final["snapshot"], "pins", final["pins"]) == pins, "Final pin read-back mismatch")
        for name, app in final["apps"].items():
            for filename, info in app["files"].items():
                self._read(final["snapshot"], name + "/" + filename, info)
        return final
