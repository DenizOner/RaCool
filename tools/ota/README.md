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
stages remain rejected. Test cases in `test_Zigbee.c` cover lock contention,
closing lifecycle, absent handler, query response and explicit cancel.

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
