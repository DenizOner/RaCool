# RaCool signed OTA — approved offline configuration, NOT yet deployed

Decision: **ESP-IDF 5.3.1, RSA-v2 app signatures without hardware Secure Boot or
eFuse changes**. No private signing key, encryption key or device identifier
may be checked in, passed in a command line log, or printed to GitHub Actions.

## Why this profile remains opt-in

The active production `sdkconfig.esp32-c6-devkitc-1` is a tested unsigned
baseline. ESP-IDF 5.3.1 verifies a new signed OTA image against the **public
key in the currently running application's first signature block**, and
assumes that starting image is trusted. Activating signed OTA verification on
a currently unsigned image **without first installing a trusted signed base**
is unsafe. Neither a successful CI build nor a header hash establishes that
a device has been migrated.

`signed-ota.kconfig` holds the intended *additional* Kconfig inputs for an
isolated, key-provisioned local build. It is not automatically loaded into
PlatformIO production configuration. The exact generated SDK config must be
read back after a fresh CMake configure. Bootloader rollback must remain ON.
When using manually signed output, keep
`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=n`; the unsigned intermediate
binary is never released as an installable firmware.

## Mandatory private preflight before any permitted device upload

1. An authorized local operator securely generates and backs up one RSA-3072
   signing key **outside the repository and any synchronized/public directory**.
   Public verification material alone can be used for offline checks.
   Never publish the private key, seed, passphrase, or absolute local path.
2. In an isolated clean candidate worktree, activate the policy without
   changing production defaults, and verify actual generated
   `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`,
   `CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y`,
   `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y`,
   `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, with hardware Secure Boot,
   flash encryption, and eFuse anti-rollback all OFF.
3. Use the pinned ESP-IDF `espsecure.py` tooling to sign the *built*
   application image with the offline private key, verify its signature
   using the expected public key, and verify the signed image fits the
   inactive 3 MiB slot **including signature block**.
4. Compare the currently installed firmware's actual app descriptor,
   partition selection, bootloader rollback support, OTA identity, and
   original NVS/Zigbee partitions. Prove a recoverable, signed base can be
   installed **without erasing any NVS, network or LittleFS data**. This is
   a physical-data-risk step requiring its own explicit approval.
5. After a *separately approved* migration/boot, verify the new running
   image really has the expected signed-image first signature block and
   trusted key, both OTA slots can recover, all critical services/relay
   and the Zigbee join work, and the firmware is production not diagnostic.
6. Only then enable a real OTA image writer with strict ZCL header + version +
   contiguous block validation and ESP-IDF signed-app verification before
   marking any slot bootable. Physical boot/power-loss and RF acceptance
   remain NOT TESTED until observed.

This mode protects against network-delivered firmware forgery but **does not
protect from an attacker able to rewrite flash or the bootloader physically**.
The ZCL outer header and `OtaImagePolicy` are an admission filter, not a
cryptographic signature verification. Signed OTA is still disabled in the
existing production image and the EP10 callback still fails closed.

## DIY/local OTA identity (no assigned manufacturer code)

**Product Owner decision, 18 November 2026:** RaCool is a personal DIY
device. A paid Zigbee/CSA manufacturer-code assignment, certification or
vendor-hosted OTA service is **not** required for this project's local
Zigbee2MQTT workflow. Do not claim an assigned/manufacturer-certified ID or
distribute public OTA images under another company's ID.

The EP10 OTA client now advertises the **shared, non-unique test-vendor code
`0xFFF1`** (`ZIGBEE_OTA_MANUFACTURER_CODE`) together with RaCool's
project-local **image-type token `0x1234`** (`ZIGBEE_OTA_IMAGE_TYPE`),
hardware version 1 and the existing monotonic `RACOOL_OTA_FILE_VERSION`.
`0xFFF1` is a shared testing namespace, **not a RaCool allocation**;
image type `0x1234` does not mean that RaCool owns manufacturer code
`0x1234`. `0xFFFF` is a Zigbee wildcard, not a safe device identity.
The old OTA manufacturer code `0x1234` is also listed for DSR Corporation
in [Wireshark's Zigbee manufacturer table](https://github.com/wireshark/wireshark/blob/82b1cb5927cd3b3562bae5345ed5040422518421/epan/dissectors/packet-zbee.h#L906).
Test IDs may be shared by other projects; **neither a local index nor
numeric identity is a security/trust boundary**. The RSA-v2 signed ESP
application check remains mandatory.

**Local-only distribution contract for future Issue #28 and #29:**

1. Keep the existing Basic `manufacturerName="RaCool"`,
   `modelId="RaCool-ESP32C6"`, endpoints, HA converter, Zigbee network
   identity and NVS unchanged. The new `0xFFF1` identifies the OTA client
   image family only, not network ownership or radio pairing.
2. Build the signed application with the separately approved, locally held
   RSA-3072 private key. Verify the signature and exact application/OTA
   container header, model, hardware range, image size, increasing file
   version and SHA-512 before it can appear in any index. No private key
   or network secret enters CI, GitHub Releases, logs or a device HTTP path.
3. Generate a **Zigbee2MQTT-local, user-maintained OTA index** and firmware
   file within the Zigbee2MQTT host/container-visible data directory.
   Entries must precisely match `manufacturerCode=65521` (`0xFFF1`),
   `imageType=4660` (`0x1234`), the signed container's actual file
   version/hash/size, and restrict targeting to `modelId="RaCool-ESP32C6"`
   and `manufacturerName=["RaCool"]`. Do not publish this shared test
   namespace to a public OTA feed or set any global index without separate
   device-safe validation.
4. Only after an authorized signed-base installation and actual device
   verification, configure the local index using Zigbee2MQTT's supported
   `ota.zigbee_ota_override_index_location` option (e.g.
   `racool-ota-index.json`) or provide an explicitly selected local file
   in an OTA update request. Start upgrades manually **one RaCool device at
   a time**, not on discovery or a timer. Preserve the default network and
   configured HA entities. The ESP32-C6 itself never uses Wi-Fi/HTTP/GitHub.
5. **Legacy unsigned clients:** The existing device image advertises
   manufacturer code `0x1234`; changing a source constant does not update
   installed firmware, and a `0xFFF1` OTA image cannot be assumed to reach
   that client. First trusted signed firmware installation/migration is a
   separate physical-data-risk step requiring explicit Product Owner approval
   and proof of backup, rollback, NVS/Zigbee continuity and boot. Do not
   silently alias the DSR code, reset pairing, erase partitions, change
   network identity or falsely mark the signed base as installed.
6. Current production image is unsigned, and the SDK callback/flash ACK
   contract is unresolved; EP10 still returns `ABORT` for
   START/RECEIVING/CHECK/APPLY/FINISH. Thus this configuration is **not**
   a working OTA delivery claim. Keep physical OTA/reboot/fan/WDT/RF
   acceptance BLOCKED/NOT TESTED until verified.

This is a **DIY local OTA workflow**, not a claim of Zigbee certification.
Public GitHub source code and ordinary CI artifacts may remain available,
but **public OTA image/index publishing is not the delivery goal**. No
subscription, certification fee or manufacturer-code purchase is required.

## Read-only local Zigbee OTA envelope inspection (Issue #28)

`python tools/ota/inspect_local_image.py LOCAL_FILE.ota` reads an **existing**
Zigbee OTA container; it does not create, sign, copy, publish or upload any
image or change Zigbee2MQTT settings. Run it from the repository root after
the official Espressif SDK image builder has produced an image, with the
private signing workflow kept entirely outside this tool.

The inspector reads the OTA manufacturer, image type and hardware revision
from `ZigbeeConfig.h`, the **candidate** OTA counter from
`resources/product-version.json`, the current source's exact full product
SemVer via `tools/product_version.py`, and the smaller OTA slot from
`partitions.csv`. It independently parses the Zigbee little-endian header
and one upgrade-image tag. It rejects wrong/wildcard/legacy identity,
a fileVersion **different from the source candidate** (not merely greater
than it), missing/wrong hardware range, an ESP app descriptor version
different from the source product identity, unsupported optional header
fields, malformed offsets/lengths/extra tags, oversized payloads, and
missing ESP application magic/descriptor/SemVer.

Use `--previous-ota-version 0x...` only with a separately verified
**prior installed/released** OTA counter. This optional input must be less
than the candidate counter. When it is absent, the inspector does **not**
claim to have verified upgrade ordering. The tool still cannot guarantee
that a candidate will install or that the current device runs that version.

The input is opened once as a regular file and read in at most
`OTA-slot-size + envelope-size + 1` bytes. It compares file metadata
before and after inspection to reject detected concurrent modifications.
This bounds allocation but **cannot guarantee an atomic snapshot or exclude
all simultaneous writes**. Generate the completed file in an isolated local
directory; do not share an actively changing path with a publisher.
Successful output reports the exact OTA container SHA-512 and metadata
as `STRUCTURE_PASS_SIGNATURE_UNVERIFIED`, with
`installation_authorized: false` and `signature_verified: false`.

**Never treat exit code zero as an install/release authorization.** An
attacker can replace a structurally valid application's contents and compute
a new hash. This tool cannot prove a genuine RSA-v2 ESP image signature,
an authorized trusted signed-base migration, a safe flash transaction,
device identity, RF delivery or real boot/rollback. The required separate
offline `espsecure.py` signature verification under the expected public
key and all physical/product gates remain open. No automatic local OTA index
activation or public feed is implemented here.

## Read-only Zigbee2MQTT local index check (Issue #28)

The index consistency checker is an **offline validator only**; it neither
generates an index nor writes/copies firmware, uploads to Zigbee2MQTT, enables
OTA, or changes the device. After a separately created candidate index and
Zigbee OTA container exist, keep both in a private **staging** directory
which is NOT the active Zigbee2MQTT data directory. Run:

    python tools/ota/check_local_index.py STAGING/racool-index.json

For a candidate with an independently verified previous **installed/released**
OTA counter, add `--previous-ota-version 0x...`. Never guess the previous
version; omitting this option leaves `upgrade_order_verified=false`.

The expected file is a JSON **array containing exactly one** RaCool entry.
Required fields are `manufacturerCode`, `imageType`, `fileVersion`,
`fileSize`, `sha512`, `hardwareVersionMin`, `hardwareVersionMax`,
`modelId`, `manufacturerName`, `url` and `fileName`. The optional
`otaHeaderString`, if present, must match the OTA header's exact ASCII bytes,
including NUL padding. No extra fields are accepted (in particular, not
`force`, downgrade overrides, or remote URLs). The model must be
`RaCool-ESP32C6` and manufacturer list exactly `["RaCool"]`. The index's
`url` and `fileName` must be the **same simple filename** ending in
`.ota` or `.zigbee`; the image must exist beside the index. The checker
intentionally rejects absolute/parent/subdirectory paths, URI schemes,
percent encoding and symbolic links, even when a broader Zigbee2MQTT version
might support them. This is a restricted RaCool-local workflow, not a
general-purpose Zigbee2MQTT index validator.

The entry's numeric identity, version, hardware range and size must match
the **actual** existing OTA file, and its SHA-512 must match a fresh bounded
read. The index is limited to 16 KiB; malformed JSON, duplicate keys,
non-integer numeric values, missing image and detected index/image mutation
fail closed. It does not lock both files into an atomic filesystem snapshot:
a separate controlled publishing procedure must stage immutable inputs and
recheck the exact bytes before any future authorized activation. Paths are
resolved relative to the index's directory for this checker; the actual
Zigbee2MQTT server/container must separately prove it resolves the file
correctly from its data directory. No automatic OTA checks/scheduling are
configured by this command. A PASS returns
`LOCAL_INDEX_MATCH_SIGNATURE_UNVERIFIED`, with both
`signature_verified=false` and `installation_authorized=false`.
**Unsigned synthetic fixtures and a consistent hash do not authorize an
OTA installation or publishing a local index.** Independently validated
RSA-v2 signed firmware, a trusted signed base, CodeQL/CI and full hardware
acceptance are still required.

The registered Python test
`test/Communication/Zigbee/test_local_ota_index.py` exercises local
positive/negative index matching with the existing offline envelope
inspector; no private keys, network connections or hardware are used.
 The registered Python helper
test `test/Communication/Zigbee/test_local_ota_image.py` covers malformed
and valid *synthetic unsigned* fixtures, including a genuine round trip
through the **unmodified official Espressif 2.0.5** image builder mirrored
at `tools/ota/vendor/esp_zigbee_sdk_2_0_5_image_builder.py` (pinned
upstream commit `e2fdf4ad8558e5beb306b0d7040072d76244c413`, Git blob
`f4c6e59f0694c60abc3470a539dd99805105a1c5`, SPDX CC0-1.0).
That mirrored builder is a test/reproducibility dependency, **not a newly
designed RaCool format, signing system or public publishing pipeline**.
The round-trip payload is synthetic/unsigned; it is **not** a real built,
RSA-verified ESP32-C6 candidate and must never be installed.

## CI preflight of the real compiled app (Issue #28)

The Linux CI job first captures the **clean source checkout identity**
into an ignored build-local file, before PlatformIO can generate any
build outputs:

`python tools/ota/check_compiled_candidate.py --capture-source-identity
.pio/racool-prebuild-identity.json`

After `pio run -e esp32-c6-devkitc-1`, Linux CI calls:

`python tools/ota/check_compiled_candidate.py --firmware
.pio/build/esp32-c6-devkitc-1/firmware.bin --source-identity
.pio/racool-prebuild-identity.json`

The snapshot records the clean Git HEAD, full app SemVer generated from
`tools/product_version.py`, manifest SHA-256 and candidate OTA version.
The checker fails if the **commit, manifest or OTA code** differs from
that pre-build snapshot. This avoids deriving an incorrect expected
`.dirty` suffix if compiling creates tracked or untracked files.
It does not skip the version match: the ESP descriptor must equal the
**recorded pre-build source identity** byte-for-byte, and the raw app must
fit the smaller OTA partition in `partitions.csv`. Snapshot capture
writes **only an ignored `.pio` build-local metadata file**; the
post-build firmware inspection remains read-only. The checker also
checks ESP image/descriptor magic and streams
SHA-256 through bounded reads on one regular file handle, and rejects
detected concurrent file changes. An absent, truncated, corrupt,
version-mismatched or oversized `firmware.bin` fails CI.

The result `COMPILED_APP_SOURCE_MATCH_SIGNATURE_UNVERIFIED` explicitly
sets `installation_authorized=false` and `signature_verified=false`.
The active production firmware is unsigned; this check does **not** add
an RSA signature block, prove that a future signed binary fits its slot,
validate every ESP segment/checksum, check an authorized previous release,
write an OTA index or touch any device. Its output SHA-256 is diagnostic,
not a trusted release digest or signature. Unit tests in
`test/Communication/Zigbee/test_local_ota_image.py` cover the read-only
accept/reject cases. CI PASS is **not** permission to flash or publish an
OTA image. Signed-artifact and physical acceptance remain blocked.

Sources (pinned version):
- ESP-IDF v5.3.1 `components/bootloader/Kconfig.projbuild`
- ESP-IDF v5.3.1 Secure Boot V2 / Signed App Verification Without Hardware
  Secure Boot documentation
- ESP Zigbee SDK 2.0.5 `examples/ota_upgrade/ota_client/main`


## ESP-IDF image-writer implementation boundary

`OtaImageWriter` now provides the real `esp_ota_begin`, `esp_ota_write`,
`esp_ota_end`, `esp_ota_set_boot_partition` and `esp_ota_abort` calls.
It compiles into a non-mutating `ESP_ERR_NOT_SUPPORTED` backend unless the
generated ESP-IDF configuration confirms software signed-app RSA verification
on update **and** bootloader rollback. A host-only test flag exercises the
signed path with fake ESP-IDF operations; those tests do not modify flash
or validate a genuine RSA signature.

The writer now requests IDF 5.3.1's `OTA_WITH_SEQUENTIAL_WRITES`
mode, rather than erasing the entire application at Begin. This mode
incrementally erases the affected 4 KiB sector(s) when `esp_ota_write`
is called. The writer retains the validated exact app size independently;
a single Write is limited to 255 bytes, so it can only trigger one new
sector erase per call. Invalid 256+ byte calls fail before any flash write
and permanently reject subsequent writes until the stream owner aborts.
The host tests assert the actual ESP-IDF Begin mode, app-size accounting
and both sides of the maximum block boundary. This is a latency-risk
reduction, NOT physical timing/WDT evidence or a safe synchronous OTA
callback solution. The signed-only gate remains unchanged.

The Zigbee progress callback deliberately does not invoke this writer.
Long partition erases and flash writes must be owned by a bounded worker
outside Zigbee SDK locks, and the callback must not acknowledge each block as
written until the actual worker operation has finished. The writer's verified
state is not Zigbee RF delivery or a completed reboot. No forced restart
is performed. Its `Verify` call delegates firmware/signature checks to
ESP-IDF's `esp_ota_end` only when signature policy is enabled.

The signed-base bootstrap, safe worker handoff, stream-to-image boundary,
last-block/power-loss tests, OTA server interoperability and actual signed
image cryptographic verification on hardware remain acceptance blockers.


## Stream-to-app boundary

`OtaImageStream` is a deterministic host-tested consumer of consecutive Zigbee
OTA blocks. It buffers only 67 bytes: 60-byte validated ZCL header, 6-byte
upgrade-image tag/length, and one ESP image magic byte. It writes the first
image byte exactly once and never includes the Zigbee wrapper in the signed
app image. Exact file offset, app size and final block are enforced before
image writer verification; a failed transfer attempts backend cleanup. This
code is **not called from the Zigbee callback** until an asynchronous worker
can return *actual completed write results* without holding SDK locks.


## SDK callback response safety (fail closed)

The pinned SDK's progress and Query Next Image callbacks consume
`out.result` synchronously on return. The Zigbee router initializes both
results to `EZB_ZCL_STATUS_ABORT` **before** acquiring the service lock:
contention, Stop, and an absent handler cannot accidentally carry a stale
`SUCCESS`. EP10 may explicitly acknowledge cancellation only; file transfer
stages remain rejected. The service rechecks `closing` after taking
its mutex: if Stop begins during lock acquisition, callbacks must not reach
any endpoint; OTA progress/query remain ABORT, attributes return FAIL, and
the acquired mutex is released. Host tests inject Stop precisely during
semaphore acquisition for cancel, query and attributes. These mocks do not
prove a live SDK flash timing or RF delivery deadline.

The official SDK 2.0.5 client example writes in the synchronous progress
callback. RaCool does **not** borrow that pattern: callback-time multi-MiB
flash erase and signature checking can hold the SDK mainloop, affect routing
and miss watchdog deadlines. A queued block alone cannot be treated as
written, and the SDK does not expose a verified deferred-ACK API in the
example. Until a bounded completion/response method is proven at the real
SDK/device boundary, no flash worker is connected or transfer enabled.


## Ambiguous boot-selection failure

ESP-IDF 5.3.1 `esp_ota_set_boot_partition` may begin rewriting `otadata`
before its final flash write returns an error. A non-OK result is therefore
**not proof that the old boot choice was preserved**. `OtaImageWriter` reads
`esp_ota_get_boot_partition()` before and after requesting a selection.
It reports success only when both the SDK call succeeds and readback equals
the validated inactive target. On any post-call error or conflicting readback,
`boot_selection_uncertain` latches; further SelectBoot/Begin/Abort are
blocked for the writer lifetime, with no automatic retry/reboot/otadata erase.
Even unchanged readback does not clear a partial-write uncertainty. The
owner/operator must investigate the physical boot state before recovery.
This is a source-level and mocked-SDK guard; it is not physical power-loss
proof.

The same boot-partition readback is a **pre-erase admission** check:
`Begin` refuses the inactive slot when it is already configured for next
boot, even though it is not the running partition. A missing boot readback
also prevents flash mutation. If that choice changes/becomes unreadable
between Begin and SelectBoot, selection and cleanup are latched as uncertain
without attempting another otadata write.


## Failed begin and cancellation ownership

ESP-IDF 5.3.1 `esp_ota_begin` performs flash erase before returning its
handle, and `esp_ota_abort` returns `ESP_ERR_NOT_FOUND` when a handle is
already invalid. Therefore a failed Begin may leave partially erased
inactive flash, and a failed Abort may have no valid handle to retry.
`OtaImageWriter` latches such failures for the session; neither failure
can be reset by blindly resending Begin or Abort. `OtaImageStream` calls
backend abort at most once after a failed transfer or explicit cancellation.
A successful cleanup may be cleared by an explicit stream Cancel; an
uncertain cleanup remains terminal, and it cannot claim transfer success.
A failed backend Begin is **also** cleanup-uncertain: ESP-IDF may already
have erased some of the inactive slot, and without a returned OTA handle
Abort cannot safely undo the operation. Stream Cancel must not clear this
state or permit another transfer. The native stream+writer test covers
that failure without performing real flash mutations.
This does not change the fail-closed EP10 production callback or establish
physical flash/NVS recovery evidence.

## Single cancellation owner between stream and ESP-IDF writer

`OtaImageStream` owns exactly one abort request after a failed block or
incomplete Verify. `OtaImageWriter_Write` and incomplete `Verify` mark
`write_failed` to reject additional data/verification, but **do not**
abort the SDK handle themselves. This avoids two abort requests when a flash
write fails and lets a successful stream-owned cleanup be distinguished from
an uncertain cleanup. A standalone writer caller must explicitly call
`OtaImageWriter_Abort` after such failure. ESP-IDF 5.3.1's
`esp_ota_end` frees a **found** OTA handle even if signature or image
validation fails; `ESP_ERR_NOT_FOUND` exits before that cleanup and
cannot be treated as a verified release of ownership. The writer
latches this exceptional result and stream cleanup remains uncertain.
Ordinary verification failures with a found handle still permit a
subsequent explicitly started transfer after the SDK release. The host integration tests
exercise the real stream and writer together, including failed writes,
successful recovery after cleanup, and lost-handle failures; no real flash
or cryptographic signature is exercised by those tests.
