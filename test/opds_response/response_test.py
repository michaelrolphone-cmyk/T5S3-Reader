#!/usr/bin/env python3
"""Run the complete production OPDS GET handler with actual ArduinoJson."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("arduino_json", type=Path)
parser.add_argument("--source-ref", help="Use a pinned original handler for a negative control")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
path = "src/network/CrossPointWebServer.cpp"
source = (subprocess.check_output(["git", "show", f"{args.source_ref}:{path}"], cwd=root, text=True)
          if args.source_ref else (root / path).read_text())
# Keep the fixture tied to the registered production endpoint and its writable fields.
assert 'server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });' in source
assert 'opdsServer.name = doc["name"] | std::string("");' in source
assert 'opdsServer.url = doc["url"] | std::string("");' in source
assert 'opdsServer.username = doc["username"] | std::string("");' in source
start = source.index("void CrossPointWebServer::handleGetOpdsServers() const {")
end = source.index("\nvoid ", start + 1)
with tempfile.TemporaryDirectory(prefix="opds-response-") as directory:
    build = Path(directory)
    (build / "handler.inc").write_text(source[start:end])
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
               "-I" + str(args.arduino_json.resolve()), "-I" + str(root / "src"),
               "-I" + str(build), str(root / "test/opds_response/response_test.cpp"),
               "-o", str(build / "test")]
    if args.sanitize:
        command[1:1] = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(build / "test")], check=True, timeout=30)
