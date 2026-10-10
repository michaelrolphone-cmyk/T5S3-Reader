# MSC 0.1.5 recovery checkpoint

The exact MSC 0.1.4 source module was restored from the 0.1.53 archive, including the bounded active-BOT pump and reset/deconfigure abort fix. A fresh investigation rebuild reproduces its original 64,500-byte ELF SHA256 d8b3406c5215d32d78a6a5ea8871fbe049b26c0ffe18c8ea1f0dcb46d12b6e43. That comparison artifact is not a final clean product-build input.

Version 0.1.5 reconstructs the later diagnostic suffix: actual TinyUSB transferred bytes/residue, command/data coordinates, persistent last nonzero sense, timeout/abort/reset/unconfigure/suspend/resume and safe-eject counters. The API copies only the caller's advertised old or new struct capacity. It does not alter SD export ownership, BOT pump policy, request retries or interpretation of suspend/deconfiguration as eject. No storage logging is introduced.

The provider, patched TinyUSB, bounded owner pump and DWC timeout fixtures pass normally and with ASan/UBSan. The corresponding app retains a bounded generic Runtime checkpoint before grant release can revoke its authority. Windows eject signaling and physical throughput remain unverified. Final source-complete product rebuild and independent review remain pending.
