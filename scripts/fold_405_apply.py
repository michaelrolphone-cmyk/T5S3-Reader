#!/usr/bin/env python3
from pathlib import Path

def patch_cpp():
    p = Path("src/network/CrossPointWebServer.cpp")
    text = p.read_text()
    if "emittedAny" in text:
        print("emittedAny already present")
        return
    old = (
        "  char output[512];\n"
        "  constexpr size_t outputSize = sizeof(output);\n"
        "  JsonDocument doc;\n"
        "\n"
        "  for (size_t i = 0; i < servers.size(); i++) {"
    )
    new = (
        "  char output[512];\n"
        "  constexpr size_t outputSize = sizeof(output);\n"
        "  JsonDocument doc;\n"
        "  bool emittedAny = false;\n"
        "\n"
        "  for (size_t i = 0; i < servers.size(); i++) {"
    )
    if old not in text:
        raise SystemExit("CrossPointWebServer.cpp expected handleGetOpdsServers prologue not found")
    text = text.replace(old, new, 1)
    old2 = (
        '    if (i > 0) server->sendContent(",");\n'
        "    server->sendContent(output);"
    )
    new2 = (
        "    // Oversized records are skipped, so source indices do not count emitted objects.\n"
        '    if (emittedAny) server->sendContent(",");\n'
        "    server->sendContent(output);\n"
        "    emittedAny = true;"
    )
    if old2 not in text:
        raise SystemExit("CrossPointWebServer.cpp expected comma separator block not found")
    p.write_text(text.replace(old2, new2, 1))
    print("patched CrossPointWebServer.cpp")

def patch_workflow():
    p = Path(".github/workflows/platformio-build.yml")
    text = p.read_text()
    if "opds_response/response_test.py" in text:
        print("opds_response step already present")
        return
    marker = "test/opds_reload/reload_test.py"
    lines = text.splitlines(keepends=True)
    out = []
    inserted = False
    for line in lines:
        out.append(line)
        if (not inserted) and marker in line and line.lstrip().startswith("run:"):
            out.append("\n")
            out.append("      - name: Verify OPDS JSON separators with actual ArduinoJson\n")
            expr = "$" + "{{ matrix.environment }}"
            out.append(
                '        run: python3 test/opds_response/response_test.py ".pio/libdeps/'
                + expr
                + '/ArduinoJson/src" --sanitize\n'
            )
            inserted = True
    if not inserted:
        raise SystemExit("opds_reload run line not found")
    p.write_text("".join(out))
    print("inserted opds_response workflow step")

if __name__ == "__main__":
    patch_cpp()
    patch_workflow()
