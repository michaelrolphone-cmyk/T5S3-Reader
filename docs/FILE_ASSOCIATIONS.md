# File associations and native app handoff

RiscRTE treats file associations as derived runtime state. Applications declare
what they can open; File Browser does not hard-code application names or file
extensions.

## App metadata

An app that can receive a file declares a bounded list in its JSON sidecar:

```json
{
  "supported_file_types": [".txt", ".md"]
}
```

Rules:

- extensions include the leading dot;
- extensions are lowercase ASCII `a-z0-9`;
- each extension is at most 15 bytes including the dot;
- an app may declare at most 12 extensions;
- duplicates are rejected by both release validation and firmware parsing.

The declaration is metadata only. It grants no capability and does not bypass
normal ELF/package validation.

## Generated association manifest

Firmware derives the current association table from verified installed
applications and writes:

`/System/Registry/FileAssociations.json`

The generated manifest is not user-authored package metadata. RiscRTE owns the
`/System` namespace; generated lookup/index state is grouped under
`/System/Registry`. The file-association registry records each normalized
extension and the currently available handler. Application records
include the app ID, display name, icon and launch path. Built-in Reader support
is represented as a system handler.

The registry is rebuilt:

- after App Store application install/update;
- after Package Manager application install/replace;
- after Package Manager application uninstall;
- lazily on first association lookup after boot.

The lazy rebuild repairs a missing/stale generated manifest after an interrupted
write. The manifest is written through a temporary file and rename sequence.

## Common file-open ABI

Native applications use `T5FileOpenApi.h`.

A launcher:

1. calls `handler_count(source_path)`;
2. reads handlers with `handler_get(...)`;
3. chooses a handler when more than one is available;
4. calls `open_request(source_path, app_id, cookie)`;
5. returns from `app_main` so its ELF can unload;
6. after firmware relaunches it, consumes `open_take_result(...)`.

A receiving app does not use a file-type-specific handoff API. It calls:

`source_path_get(buffer, capacity)`

at startup to receive the absolute `/sd/...` VFS path.

Image Viewer and Text Editor use this interface. Raw `.elf` files remain an
execution action rather than a file association.

## File Browser behavior

File Browser always lists files regardless of support.

- zero handlers: the file remains visible and opening reports that no app is
  registered for the type;
- one handler: it opens directly;
- multiple handlers: File Browser shows an **Open with** chooser;
- built-in Reader handlers route through the firmware reader;
- application handlers route through the common file-open ABI.
