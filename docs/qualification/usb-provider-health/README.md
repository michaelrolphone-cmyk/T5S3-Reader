# USB provider health qualification

Public source `068450d2b971ca77d653f2ab1d9117cff7335288` preserves the exact qualified tree `efb8396f2574d69f116179542381a53349a97234` of local Reader source `1214cb1b90fee7aa8a3ef9f21e1f9f2f8358e48c`. Its public parent `c334919a05d393b7b3a536fceb117cfbb573a4ef` has the same tree as original parent `bf997f3d777e644e98c2f885a9c9f4c81e1a0e48`. This successor adds only qualification evidence.

[Exact original qualification receipt](receipt.json), [default-byte comparison](default-identity.json), and [publication mapping](publication-mapping.json) retain the original source and ELF identities. Absolute paths in the immutable receipt name the original local build inputs/artifacts. Its unpublished status records the time of qualification; this mapping records the later source publication. The local baseline object is required by the original default comparator. Public parent/tree mapping supplies the equivalent source reference.

All 44 real-stack/controller/facade/deadline cases passed normal and ASan/UBSan runs. All three production-host/HID Runtime .94 integration cases passed both. The eight target profiles passed strict descriptor/import/relocation audits, and 16 malformed target controls were rejected. All nine complete default ELFs reproduce the original bytes; all 215 existing Driver/SDK files remain unchanged. Logs and target audits are preserved in this directory. Remote CI results belong to their exact public head and are reported separately.

The actual raw-HID close returning true while host_v2 retains a closing claim is covered as RETAINED. VBUS/PHY failed cleanup tails, off-state partial-resource precedence and post-open/post-claim exhausted ownership records are covered through actual controller control flow with repeated no-I/O observation.

| Selected profile | Original ELF SHA-256 | Bytes |
| --- | --- | --- |
| usb-controller-esp32s3 0.1.25 | `9451ede799d85e8e8671de8c70fefae50788c39f05511fee91c94294ce883d5f` | 964508 |
| usb-host-v2 0.1.7 | `48c426cf5c16c8a01d7f1f54f579df2a109fd271681f39430a25a1dbad7af40c` | 16676 |
| usb-hid 0.1.3 | `d4140a64dce51ccaa3ae755f4a1923ee8ef12d87b5d672c27ed98f5cec6fa99f` | 7540 |
| usb-hid-mouse 0.1.1 | `8adf30b2e6937de887831ab1ac89cc55de642ec48f5a1237eeecbfd7a0176359` | 19936 |
| usb-hid-keyboard 0.1.2 | `8f38404ecb3ae2300294d2273af53bd25ef7811f54ee07f26df90df36421131b` | 9116 |
| usb-hid-gamepad 0.1.5 | `64c82ff6b82729ec170d364e9d3fd56da6879ff281a003e44b868cf55cbfeaa4` | 15360 |
| usb-xinput-gamepad 0.1.4 | `b2d63f2f1dd3bb09bbf37e171f79dc46b111a4d9893b98ebdd72c87b9d8f719d` | 11208 |
| usb-hid-text-input 0.1.1 | `447fbeab894241885227e632632bf4b5f6cf88e562e84a246455c1d81dac4425` | 9364 |

The fixed health header is from Runtime `51a9a09e15789be4587f59295f89c3e3b5be0c66`, SHA-256 `0d36bbac73631765462c79de368469c29cac1b4e85c68cb9c7844e3611804c1f`. Runtime source/history is not bundled here.

This does not qualify physical X4 VBUS or powered-host operation. A qualified VBUS source/sense root, health descriptors for the complete actual active component (including clock, power/I2C/profile dependencies and active reverse consumers), exact surviving grants, Runtime import/admission binding and an external bounded owner/consumer pump remain required. No product adopts these opt-in profiles.
