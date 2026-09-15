# T5S3 Security Architecture and Signed Driver Specification

## Status

Security architecture specification for production T5S3 devices using dynamically loaded ELF applications and hardware drivers.

This specification extends the runtime and driver architecture with a cryptographic chain of trust. The goal is to permit application bundles, driver packages, and other resources to reside on removable/untrusted storage while ensuring that native code executes only when authorized by device policy.

> **Core invariant:** Storage is not trusted. Native executable code is authenticated before execution. Trust begins in hardware and is extended by verified firmware to signed executable modules.

---

## 1. Security goals

A production device SHOULD be capable of enforcing the following properties:

1. Only authorized firmware boots.
2. Production firmware cannot be silently replaced by unsigned firmware.
3. Native driver ELFs execute only after cryptographic authentication.
4. Driver manifests and executable content are authenticated as one logical package.
5. An attacker who can modify or replace files on the SD card cannot introduce executable native code.
6. Driver identity, version, ABI, capabilities, hardware compatibility, and executable digest are covered by the signature.
7. Driver signing can be delegated to authorized vendor keys without disclosing the firmware/root signing key.
8. Applications access hardware through framework capabilities rather than directly binding to hardware implementations.
9. Development devices may deliberately allow unsigned code without weakening the policy of production devices.
10. Production devices can enforce rollback protection and key revocation policy.
11. Package corruption and package authenticity are treated as separate concerns.
12. The security model SHALL NOT claim process isolation that the ESP32-S3 does not provide.

---

## 2. Threat model

The architecture assumes an attacker may be able to:

- remove, read, copy, and modify the SD card;
- replace application bundles or driver packages;
- modify manifests, hashes, resources, and ELF files on removable storage;
- supply malformed ELF binaries or package metadata;
- attempt path traversal or package substitution;
- install an older legitimately signed but vulnerable driver;
- trigger malformed driver inputs;
- possess normal application-level access to framework APIs.

The production architecture MUST NOT rely on SD-card confidentiality or filesystem permissions as a trust boundary.

The architecture does not by itself protect against physical attacks capable of defeating ESP32-S3 eFuse security, invasive semiconductor attacks, compromise of vendor signing keys, vulnerabilities in trusted firmware, or memory corruption inside already trusted native code.

---

## 3. Hardware root of trust

Production hardware SHOULD use ESP32-S3 Secure Boot v2 as the root of executable trust.

Conceptually:

```text
ESP32-S3 eFuse trust state
          |
          v
Secure Boot verification
          |
          v
Signed T5 firmware
          |
          v
T5 module verifier
          |
          v
Signed driver/application policy
```

Secure Boot establishes that the firmware responsible for module verification is itself authorized.

Production provisioning SHOULD additionally evaluate and configure:

- flash encryption;
- secure boot key provisioning;
- anti-rollback support where applicable;
- JTAG/debug restrictions;
- UART/download-mode restrictions appropriate to product servicing requirements;
- protection of security-relevant eFuses from later modification.

Exact eFuse policy SHALL be documented separately as a production provisioning profile because irreversible settings must not be enabled casually during development.

---

## 4. Trust domains

The runtime SHALL distinguish at least these trust domains:

```text
UNTRUSTED STORAGE / INPUT

SD card
application bundles
driver packages
package manifests
resources
network downloads
external peripherals
        |
        v
+----------------------------------+
|       Verification boundary      |
| signature | digest | schema | ABI|
+----------------------------------+
        |
        v
TRUSTED NATIVE EXECUTION

verified firmware
verified driver ELF
framework/kernel services
```

A file being present under `/Drivers/` or `/Apps/` does not make it trusted.

A SHA-256 digest establishes integrity relative to a known digest. A digest stored beside an ELF on the same untrusted medium does **not** establish authenticity. Authenticity requires a signature rooted in a trusted key.

---

## 5. Driver trust chain

The minimum production driver chain is:

```text
Hardware root
    |
    v
Signed firmware
    |
    v
Trusted driver public key
    |
    v
Signed driver manifest/package
    |
    v
Verified driver ELF digest
    |
    v
ELF loader
    |
    v
Driver capability ABI
```

The ELF loader SHALL NOT be used as the authenticity verifier. Package authentication and policy validation SHALL occur before executable mapping or symbol resolution.

---

## 6. Driver package

The existing `.t5driver` package format SHOULD evolve into a signed package without changing the driver ABI itself.

Conceptual installed layout:

```text
/Drivers/gps-nmea/
    manifest.json
    driver.elf
    signature.bin
```

The distributable artifact may remain a ZIP or another container convenient for copying to removable media. Container compression is not a security boundary.

The signed manifest SHALL identify at minimum:

- package format/schema version;
- module type (`driver`);
- stable driver ID;
- driver version;
- driver ABI version;
- minimum/maximum compatible firmware or framework ABI as required;
- target architecture;
- supported hardware/device identifiers where applicable;
- capabilities provided;
- privileged host interfaces requested;
- ELF size;
- cryptographic digest algorithm;
- ELF cryptographic digest;
- signing key/certificate identifier;
- anti-rollback/security version when enabled.

Security-sensitive metadata MUST be covered by the signature. An attacker SHALL NOT be able to modify capability declarations, compatibility requirements, versions, hashes, or module identity without invalidating authentication.

---

## 7. Canonical signed representation

Signature verification SHALL operate over an unambiguous canonical representation.

The implementation SHALL NOT sign arbitrary pretty-printed JSON bytes unless canonical JSON rules are explicitly defined and tested. Acceptable approaches include:

1. a canonical deterministic encoding of selected manifest fields;
2. a compact versioned binary signed-manifest structure;
3. a formally specified canonical JSON serialization.

The package may continue to expose human-readable `manifest.json`, but the bytes authenticated by the signature must be deterministic.

A future binary manifest is permitted without requiring changes to the driver ABI.

---

## 8. Cryptographic algorithms

The initial production implementation SHOULD use algorithms supported efficiently and safely by ESP-IDF/mbedTLS on ESP32-S3.

The package format SHALL identify the signature and digest algorithm by versioned identifiers rather than assuming one algorithm forever.

SHA-256 is suitable as the initial executable digest.

The signing algorithm SHOULD be selected based on ESP-IDF support, verification cost, key-management requirements, and compatibility with the production signing infrastructure. The architecture SHALL permit algorithm migration.

Private signing keys SHALL never be embedded in firmware, application bundles, driver packages, SD-card images, or public build artifacts.

Devices require only public verification material.

---

## 9. Verification pipeline

Before a native driver ELF may execute, the runtime SHALL perform a sequence equivalent to:

```text
locate package
      |
      v
parse bounded manifest
      |
      v
validate package/module type
      |
      v
validate paths and file sizes
      |
      v
resolve trusted signing identity
      |
      v
verify manifest/package signature
      |
      v
hash driver.elf
      |
      v
compare authenticated ELF digest
      |
      v
validate version / rollback policy
      |
      v
validate target architecture
      |
      v
validate firmware + driver ABI
      |
      v
validate hardware compatibility
      |
      v
validate declared capabilities/policy
      |
      v
ELF structural validation
      |
      v
load/map ELF
      |
      v
resolve driver entry point
      |
      v
inject permitted host ABI
      |
      v
start driver
```

Failure at any verification stage SHALL prevent execution.

Verification errors SHOULD distinguish corruption, unsupported format, incompatible ABI, unknown signer, invalid signature, revoked signer, rollback violation, policy denial, and ELF-loader failure for diagnostics without weakening enforcement.

---

## 10. Verification before `dlopen`

The runtime MUST authenticate an ELF before passing it to the native ELF loading path.

This ordering is normative:

```text
UNTRUSTED BYTES
      |
      v
AUTHENTICATE
      |
      v
AUTHORIZE
      |
      v
LOAD
      |
      v
EXECUTE
```

It SHALL NOT be:

```text
load -> inspect -> authenticate
```

No constructors, entry points, callbacks, relocations with executable side effects, or module-controlled code may execute before authentication and authorization complete.

---

## 11. Driver authorization and capabilities

Authentication answers:

> Is this package signed by a trusted identity and unmodified?

Authorization separately answers:

> Is this trusted module permitted to provide or access these capabilities on this device?

A valid signature MUST NOT automatically imply unrestricted hardware access.

The framework SHOULD maintain policy describing which driver identities/signers may provide capabilities such as:

```text
position.gnss
serial.host
network.wifi
storage.removable
sensor.environment
```

Drivers SHOULD receive a narrow, versioned host ABI containing only the kernel primitives required by their approved function.

For example:

```text
GPS application scene
        |
        | position.gnss
        v
Capability resolver
        |
        v
Verified gps-nmea driver ELF
        |
        | permitted host ABI
        v
kernel UART + power services
```

Application code SHALL NOT need to know the concrete driver filename, UART number, GPIO wiring, or shared power-rail implementation.

---

## 12. Delegated vendor signing

The architecture SHOULD permit delegated driver signing.

Conceptually:

```text
T5 Root Trust
     |
     +---- T5 System Driver Key
     |
     +---- Authorized Vendor Certificate
                |
                +---- Vendor GNSS driver
                +---- Vendor sensor driver
```

A vendor authorization certificate SHOULD constrain at least:

- vendor identity;
- vendor public key;
- allowed module type;
- allowed capability namespaces;
- validity/security-policy version;
- optional device/product family restrictions;
- optional expiration where reliable time semantics exist;
- certificate/key identifier.

This allows a hardware vendor to sign its own driver releases without receiving the T5 root or firmware private key.

A vendor authorized for `position.gnss` SHOULD NOT automatically be authorized to publish a storage, network, or unrestricted system driver.

---

## 13. Key hierarchy

A scalable key hierarchy SHOULD separate responsibilities:

```text
Offline T5 Root
      |
      +---- Firmware signing authority
      |
      +---- System driver signing authority
      |
      +---- Application signing authority (future/optional)
      |
      +---- Vendor authorization authority
```

The offline root SHOULD not be used for routine release signing.

Compromise of a delegated key should therefore have a smaller blast radius than compromise of the root.

The exact PKI representation may be custom and compact; a full general-purpose X.509 stack is not required if a smaller versioned certificate structure satisfies the trust requirements.

---

## 14. Key rotation and revocation

The design SHALL anticipate key compromise before production deployment.

Firmware SHOULD support multiple trusted key identifiers and a mechanism for security updates to add, retire, or revoke delegated signing identities.

Revocation state MUST itself be protected by the firmware trust chain or authenticated persistent security state.

A package signed by a revoked key SHALL fail authorization even if its cryptographic signature remains mathematically valid.

Root-key replacement is substantially more sensitive and SHALL be coordinated with the ESP32-S3 Secure Boot/provisioning design.

---

## 15. Anti-rollback

A signed old version is still authentic but may contain a known vulnerability.

Production policy SHOULD therefore support a monotonic security version independent of user-facing semantic version strings.

Conceptually:

```text
version:          1.7.3
security_version: 12
```

The runtime MAY allow any semantic version whose `security_version` is at least the minimum accepted security version for that driver/product.

Security-version state SHALL be stored so an attacker cannot defeat rollback protection merely by modifying the SD card.

Firmware rollback and driver rollback are separate policies and SHOULD both be considered.

---

## 16. Development, signed, and production policies

The same package/runtime architecture SHOULD support multiple explicit enforcement modes.

### Development

```text
unsigned modules: allowed
signed modules:   allowed
signature errors: visible
rollback:         optional
```

This mode is intended for developer hardware and local iteration.

### Signed

```text
unsigned drivers: rejected
unknown signers:  rejected
signed drivers:   verified
rollback:         policy controlled
```

Useful for pre-production and integration testing.

### Production locked

```text
Secure Boot:             required
flash encryption:        production policy
unsigned native drivers: rejected
unknown/revoked signers: rejected
rollback policy:         enforced
production debug access: restricted
```

The enforcement mode SHOULD be derived from trusted device/firmware configuration, not from an editable file on the SD card.

A production device SHALL NOT become a development device because a configuration file is modified or absent.

---

## 17. Native-code isolation limitation

A cryptographic signature proves authorization and integrity. It does **not** provide process isolation.

ESP32-S3 dynamically loaded ELF code executes native code within the firmware's address-space/security constraints. A signed driver containing a memory-safety bug may corrupt system state. A malicious driver signed by a trusted privileged key may be capable of violating intended API boundaries.

Therefore:

- driver signing keys are highly privileged;
- driver code SHOULD be kept small;
- the injected host ABI SHOULD be narrow;
- direct firmware-internal symbol exposure SHOULD be minimized;
- pointer and buffer contracts SHALL be validated;
- driver callbacks SHALL have explicit lifetime rules;
- driver resources SHALL be framework-owned or tracked;
- ELF unload SHALL invalidate module callbacks/pointers;
- fuzzing/static analysis SHOULD be used for parsers and external input paths;
- signing review SHOULD treat a native driver as trusted system code.

The capability ABI improves architecture and reduces accidental coupling, but it SHALL NOT be documented as a hardware-enforced sandbox unless such isolation is actually implemented.

---

## 18. Application trust

Application and driver trust SHOULD remain distinct.

A driver is trusted native system code capable of interacting with kernel-owned hardware primitives through its approved host ABI.

An application consumes framework services and hardware capabilities:

```text
Application bundle
       |
       | request capability
       v
T5 Framework
       |
       | policy + resolution
       v
Verified Driver
       |
       v
Hardware
```

Application signing may be introduced separately. Signing an application SHALL NOT implicitly grant driver-level privileges.

The framework SHOULD eventually define permissions/capabilities independently from package authenticity.

---

## 19. Removable SD-card model

The SD card SHALL be treated as untrusted removable storage.

This permits a simple operational model:

```text
copy package to SD
        |
        v
insert SD into device
        |
        v
device discovers package
        |
        v
device authenticates package
        |
        +---- invalid -> refuse
        |
        +---- valid --> register/load when needed
```

The SD card does not need to be cryptographically trusted as a device merely to host authenticated executable packages.

Users may continue to copy signed packages with an ordinary card reader. A future Driver Manager can automate installation without changing the underlying trust model.

---

## 20. Installation versus execution verification

A Driver Manager MAY verify a package during installation, but installation-time verification alone is insufficient when installed files remain on mutable removable storage.

Production policy SHOULD authenticate the executable at load time, or use an authenticated installation record that securely binds the exact bytes subsequently loaded.

For the initial SD-card architecture, verification immediately before loading is preferred because it makes fewer assumptions about filesystem integrity.

The runtime MAY cache successful verification metadata for performance only when it can securely detect file replacement/modification. Security correctness SHALL NOT depend on an unauthenticated cache stored beside the driver.

---

## 21. Package parser requirements

Package and manifest parsing occurs before trust is established and SHALL therefore be treated as hostile-input parsing.

The implementation SHALL:

- enforce maximum manifest size;
- enforce maximum path and identifier lengths;
- reject duplicate security-critical fields;
- reject unknown required schema versions;
- reject integer overflow/underflow;
- reject paths escaping the package directory;
- enforce maximum ELF/package sizes;
- bound certificate-chain depth;
- bound signature and certificate sizes;
- avoid unbounded allocation from manifest-controlled lengths;
- fail closed on malformed security metadata.

Where practical, security parsing SHOULD avoid complex dynamic object graphs and unnecessary heap allocation.

---

## 22. TOCTOU protection

The runtime SHALL consider time-of-check/time-of-use attacks on mutable SD storage.

The bytes authenticated must be the bytes executed.

Acceptable strategies include hashing while copying the verified ELF into controlled storage/executable mapping, retaining an exclusive file handle where filesystem semantics are sufficient, or otherwise preventing replacement between digest verification and loading.

The implementation SHALL NOT knowingly verify one pathname, close it, and later reopen an attacker-replaceable file for execution without revalidation.

---

## 23. Secrets and confidentiality

Code signing primarily provides authenticity and integrity, not confidentiality.

A signed ELF stored on SD may remain readable. If proprietary driver confidentiality is required, that is a separate design problem from signature verification.

ESP32 flash encryption protects appropriate data stored in device flash but does not automatically encrypt arbitrary removable SD-card content.

No private signing key SHALL be stored on the device merely to verify packages.

Device-specific secrets, credentials, and private keys SHOULD use ESP-IDF facilities appropriate for encrypted/protected storage rather than ordinary SD files.

---

## 24. Failure behavior

Security verification SHALL fail closed.

A rejected driver SHALL NOT partially initialize hardware or expose its declared capability.

The framework SHOULD present diagnostics such as:

```text
Driver rejected: invalid signature
Driver rejected: unknown signer
Driver rejected: signer revoked
Driver rejected: executable digest mismatch
Driver rejected: rollback violation
Driver rejected: incompatible ABI
Driver rejected: unauthorized capability
```

Detailed cryptographic diagnostics MAY be restricted in consumer UI while remaining available through development logging.

A missing optional driver should make its capability unavailable rather than destabilize unrelated framework functions.

---

## 25. Logging and audit

The framework SHOULD record security-relevant module events including:

- package discovered;
- verification success/failure;
- signer/key ID;
- driver ID/version/security version;
- capability registration;
- module load/unload;
- rollback rejection;
- revoked signer rejection;
- malformed package rejection.

Logs SHALL NOT contain private keys, secret material, or sensitive credential contents.

---

## 26. Build and signing pipeline

The driver build process SHOULD remain usable without signing for development while supporting a production signing stage.

Conceptually:

```text
source
  |
  v
build driver.elf
  |
  v
compute ELF SHA-256
  |
  v
generate canonical manifest
  |
  +---------------------------+
  | development               | production/release
  v                           v
unsigned package          signing service
                              |
                              v
                         signature.bin
                              |
                              v
                         signed .t5driver
```

Production private keys SHOULD reside in controlled signing infrastructure rather than ordinary CI repository secrets where practical.

CI may build unsigned artifacts and submit their authenticated digest/manifest to a signing service.

The release artifact SHOULD be reproducibly associated with source revision, build configuration, driver ID, and version.

---

## 27. Relationship to existing GPS driver

`gps-nmea` SHOULD serve as the reference signed-driver implementation.

The existing SHA-256 package validation is the first integrity layer. The security evolution should preserve that digest but authenticate it through the signed manifest.

Target progression:

```text
Current
    SHA-256 integrity validation

Next
    signed driver manifest/package

Production
    Secure Boot
       -> authenticated firmware
          -> trusted driver verifier
             -> signed gps-nmea package
                -> capability authorization
                   -> ELF load
```

The GPS driver ABI does not need to change merely because package authentication is added.

---

## 28. Relationship to scene/application runtime

The scene runtime and security architecture are orthogonal but complementary.

Application bundles define application identity, scene topology, navigation state, resources, and optional scene-controller ELFs.

The security layer determines whether executable modules are permitted to execute.

Eventually the same trust infrastructure MAY authenticate application bundles, but driver signing SHALL be implementable independently because native hardware drivers have a higher trust requirement.

---

## 29. Production provisioning profile

Before shipping locked hardware, the project SHALL define a separate, explicit provisioning procedure covering:

1. secure boot key generation and custody;
2. firmware signing;
3. flash-encryption configuration;
4. eFuse programming order;
5. debug/JTAG policy;
6. download/UART recovery policy;
7. device recovery/service procedure;
8. firmware anti-rollback policy;
9. driver trust-store initialization;
10. driver rollback state;
11. key rotation/revocation recovery;
12. manufacturing test transition into production-lock state.

Because some ESP32-S3 security settings are irreversible, production provisioning SHALL be tested on sacrificial/development hardware before manufacturing use.

---

## 30. Security test requirements

At minimum, automated/integration testing SHOULD prove that:

1. a valid signed driver loads;
2. an unsigned driver is rejected under signed/production policy;
3. a one-byte ELF modification invalidates the package;
4. a manifest modification invalidates the signature;
5. replacing both ELF and its unhashed sidecar digest does not bypass signature verification;
6. an unknown signing key is rejected;
7. a revoked signing key is rejected;
8. an unauthorized vendor capability is rejected;
9. an older security version is rejected when rollback protection is active;
10. malformed manifests fail closed;
11. oversized fields and files are rejected before dangerous allocation;
12. path traversal is rejected;
13. driver/application module-type confusion is rejected;
14. authentication occurs before ELF execution;
15. the exact authenticated ELF bytes are the bytes loaded;
16. failed drivers do not leave hardware/resource ownership behind;
17. development mode can deliberately load unsigned test drivers;
18. production policy cannot be disabled by editing the SD card.

---

## 31. Normative security rules

1. **The SD card is untrusted.**
2. **Secure Boot establishes trust in the firmware that performs module verification.**
3. **Native production drivers must be authenticated before loading or execution.**
4. **SHA-256 alone detects modification; a trusted signature establishes authenticity.**
5. **Security-critical manifest metadata and the ELF digest must be covered by the signature.**
6. **Authentication and authorization are separate decisions.**
7. **A valid signature does not grant unrestricted capabilities.**
8. **Driver signing authority is privileged because native ELF execution is not process-isolated.**
9. **The bytes authenticated must be the bytes executed.**
10. **Production enforcement policy must not be controlled by mutable SD-card configuration.**
11. **Rollback protection must account for authentic but vulnerable old packages.**
12. **Delegated vendor keys should be constrained to approved module/capability classes.**
13. **Private signing keys never belong on production devices.**
14. **Security verification fails closed.**
15. **Applications consume capabilities; hardware drivers implement them.**
16. **Driver package authentication must not require redesign of the driver ABI.**

---

## 32. Implementation sequence

Recommended implementation order:

1. version the current `.t5driver` manifest so security fields can evolve cleanly;
2. define the canonical signed-manifest representation;
3. add signing-key IDs and a firmware trust store;
4. add signature generation to the driver packaging/release tooling;
5. verify signature + ELF SHA-256 before the existing ELF loader path;
6. add explicit development/signed/production enforcement policies;
7. make `gps-nmea` the reference signed package;
8. add capability authorization associated with signer/driver identity;
9. add security-version anti-rollback support;
10. add delegated vendor certificates/keys if third-party drivers are required;
11. implement key revocation/rotation support;
12. define and test the production ESP32-S3 eFuse/Secure Boot/flash-encryption provisioning profile;
13. only then lock production hardware.

This sequence establishes signed removable-media drivers before irreversible hardware lockdown and preserves the existing driver/runtime architecture while extending it into a hardware-rooted chain of trust.
