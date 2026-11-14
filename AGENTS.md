# AGENTS.md — RaCool engineering directives

## Document boundaries

AGENTS.md contains durable, cross-cutting engineering and verification rules only. CODEBASE.md describes the current architecture, module responsibilities, interfaces, behavior and supported commands. Neither file is a work log: never append session summaries, completed or pending tasks, dated progress, acceptance counts or test results. Update the relevant existing section only when a lasting rule or technical contract changes.

Write Turkish or English execution evidence, results, failures and limitations to the corresponding GitHub Issue or Pull Request. Tasks, bugs, and remaining work are tracked as GitHub Issues on the repository. Put detailed module-specific procedures beside their module or test helper, and link to them rather than copying historical logs into documentation. Historical snapshots in docs/archive and legacy reports (DEV_VERIFICATION_REPORT.md, TASK_CHECKLIST.md, MODULE_AUDIT.md) are local records and are ignored from version control. This separation applies to every agent and every completion handoff.

## Product and authority

RaCool is a mains-powered ESP32-C6 revision 0.1 device with 8 MB flash, ESP-IDF/FreeRTOS, a 128×64 LVGL OLED, KY-040 encoder, MLX90614 ambient/radiator sensing, internal chip sensing, a fan relay and two RGB indicators. Native Zigbee integration with Home Assistant through Zigbee2MQTT is a primary product requirement. There is no confirmed battery, PMIC or physical power-cut circuit. Do not describe software sleep as electrical disconnection.

Follow the user's latest instructions and recorded approvals. Preserve earlier unfinished tasks when new requests arrive. Work autonomously on authorized changes and information discoverable from the repository or connected hardware. Ask for an approval only when the user explicitly requires one or an action exceeds the authorized scope. Physical color, display and encoder observations require the user; finish automatic tests first, then explain one action and expected observation and wait for the answer.

New GitHub Issue titles, acceptance criteria, PR descriptions and user-facing engineering reports are Turkish. Source identifiers, APIs, executable Agent instructions and code comments remain English; preserve exact existing technical symbols and translate legacy comments only in edited code. Product labels may be Turkish. Preserve API names, paths and exact diagnostic excerpts when documenting evidence.

Converter endpoint keys, MQTT property keys and internal identifiers must also be English and describe their responsibility. Keep Turkish user-facing labels/descriptions. Treat property renaming as an external-contract migration: inspect discovery, entities, automations, dashboards and test/helper consumers; preserve backups and validate the new routes before retiring old names.

## Autonomous development, CLI architecture and developer tools

- Classify CLI commands by purpose, including individual subcommands of the same subsystem. Keep product functions directly available in user help; group logging, instrumentation, low-level diagnostics and internal controls under consistently named Developer Tools categories. Avoid unnecessary menu depth and empty categories.
- Developer Mode defaults OFF and exposes Developer Tools only when enabled. Persist the user's preference across restarts using the configuration owner, with checked failure handling and safe schema migration. This mode manages interface complexity, not security; validate and bound state-changing tools separately, distinguish them from read-only diagnostics, and require confirmation where appropriate. Integrate contextual help and keep product commands usable in both modes.
- Inspect existing command consumers, helpers and machine-readable contracts before changing routes. Migrate and verify affected consumers alongside the CLI; do not silently break automation. Test parsing, help, visibility, access, mode transitions and product behavior with the mode both OFF and ON.
- Agents may add focused logging, metrics, counters, tracing, state inspection and diagnostic controls when needed to measure hypotheses and verify fixes. Organize reusable tools under Developer Tools; remove problem-specific temporary instrumentation after use. Keep production diagnostic flags OFF and account for RAM, flash, CPU, scheduling, timing, interrupts, watchdog and boot costs.
- Make small directly related engineering fixes autonomously within the authorized scope. Give substantial architectural or product behavior changes a concrete justification and follow recorded approval requirements for optional features.

## Quality, architecture and security

- Read actual interfaces and consumers before editing. `IService` and module interfaces are the source of truth; do not impose a new Init/DeInit/Process wrapper on every module. Pure calculation modules need no artificial lifecycle or singleton.
- Keep responsibilities focused and shared logic in reusable modules. Use injected interfaces at hardware boundaries; do not add abstraction layers or separate tasks without a concrete need.
- Keep event, ISR and timer callbacks short. Dedicated workers may block on queues and bounded peripheral operations. Use elapsed time for animation and periodic work; do not busy-poll or create one task per command. Never allocate, log through unsafe paths or take ordinary mutexes in an ISR.
- Convert positive polling sleeps to at least one RTOS tick. At the production 100 Hz tick rate, a 5 ms delay becomes zero with pdMS_TO_TICKS; vTaskDelay(0) can starve lower-priority work. Preserve intentional zero-timeout lock/queue operations as nonblocking calls.
- Specify resource ownership, lock order, callback lifetime and shutdown sequence. Never delete a task while it owns another resource lock. Copy queued payloads whose producer storage can expire. Check allocation, lock, SDK, queue and persistence failures; roll back partial initialization and never report an operation as applied before it succeeds.
- Define configurable defaults and limits in module configuration headers/sources. Protocol constants are not user configuration. Validate input before changing live settings, including length, integer overflow, finite numbers, enum bounds and cross-field constraints. Share validation across HA, CLI, OLED and NVS load paths.
- Never commit credentials, tokens, private Zigbee keys, credential-bearing configuration dumps or authentication storage. ConfigurationManager/NVS alone does not imply encryption or authenticated access. Follow Zigbee stack ownership of network credentials; do not copy them into a parallel store. The user does not want password-gated product interfaces. Address vulnerabilities through validation, memory safety, permissions, scope and sound protocol handling without adding unsolicited login flows.
- Preserve existing user changes. Do not reset or clean the workspace to simplify the job. Fix causes rather than suppressing warnings, allowing duplicate symbols, fabricating fallback sensor values or weakening assertions.
- Passing tests is evidence, not proof of absolute correctness. Record coverage and limitations honestly. Do not claim “100% quality” or all tests passed without a defined, measured basis.

## Efficient audit and task control

Maintain architectural discipline covering dependencies, memory/concurrency, security, configuration, HA/OLED/CLI behavior, and test suites. Record inspected paths and evidence once. Revisit only changed dependencies or unresolved findings; batch independent searches. Do not create parallel agents unless explicitly authorized.

Track bugs, enhancements, approved decisions and acceptance tasks in their existing GitHub Issues; create a new Issue only for a genuinely new approved scope. Use authorized feature/fix branches and linked Pull Requests. Use `Closes #<id>` only when merging the *entire* Issue acceptance is justified; for open physical, root-cause or follow-on criteria use `Refs #<id>` so the Issue remains open. GitHub Actions CI validates test layout, test inventory, helper suites, host native tests and warning-free PlatformIO firmware compilation on PRs and pushes to `main`. Do not implement optional features before approval. Reconcile task-scoped Issues, evidence and remaining physical observations; update `CODEBASE.md` for lasting interface, module, build/test command or behavior changes, never as a work log.

## Evidence-first autonomous delivery and conditional PR merge

**Product Owner standing approval, 14 November 2026:** Within an already approved RaCool delivery scope, the authorized development executor **may and should merge a PR without requesting another Product Owner approval per PR/commit once every applicable gate below has evidence-backed PASS**. This is an authorized human/Agent-executed normal GitHub PR merge, **not** an unsupervised auto-merge bot, automatic draft approval, production deployment, Issue closure or exemption from security/physical acceptance. See the scope and safeguards in [the approved decision Issue #20](https://github.com/DenizOner/RaCool/issues/20). The Product Owner's latest explicit decision and a PR-specific hold take precedence over this general permission.

### Decide from the user journey; reuse before building

1. Identify the approved end-to-end user action, its negative/failure behavior, the smallest decisive test and any existing Issue acceptance constraint. Challenge a proposed implementation if an existing service, SDK/public API or configuration already provides the desired behavior.
2. Inspect only relevant current `include/`, `src/`, `test/`, module references, SDK/IDF/LVGL version-matched contracts and actual consumers. Classify the behavior **READY AND SUFFICIENT / CONFIGURATION OR SMALL EXTENSION / GAP PROVEN / UNVERIFIED**. Prefer existing services, configuration and a small adapter to new tasks, locks, dependencies or architecture. An unsuccessful search alone does not prove absence.
3. Prove the riskiest assumption early and test both expected and negative/boundary paths, including failure, concurrency, persistence, reboot or real hardware where relevant. Reuse valid evidence from the **same exact revision**; do not relabel older HEAD hardware PASS, mock behavior, SDK admission, cached MQTT or CI as physical/RF delivery. Bound investigations to what can change safety, implementation or acceptance.
4. Record concise choices, actual failures, tested/untested status and paths in the existing Issue/PR. `AGENTS.md` is durable execution policy; `CODEBASE.md` is current architecture; neither is a dated work diary. Use **PASS / FAIL / BLOCKED / NOT TESTED** precisely. Blockers report **sorun / etki / çözüm / sorumlu**, with a short Turkish #ELI5 conclusion.

### Direct execution, exact target and single writer

- The authorized development ChatGPT directly handles reachable GitHub source/branches, issues, CI, scoped code and PR work. Delegate **only** inaccessible local/USB/serial/firmware execution or a genuinely separate independent audit, never work that can be done directly. Do not ask the Product Owner to relay an executable task unnecessarily.
- GitHub live refs are authoritative for shared commits; a local clone, worktree, cached ref or Agent message is not automatically current. Before mutable operations verify actual repository, physical worktree root and Git common-dir when local, named owner, branch/upstream, **full HEAD/base SHAs**, remote tip, working tree (including untracked/ignored and unique commits as relevant), running build/serial ownership and retained user data. Never assume a folder name identifies the correct firmware.
- **One writer per affected branch/worktree and one serial port owner**; serialize overlapping GitHub and local Agent writes. Preserve dirty/unique/unpushed work. No blind pull, reset, clean, stash, force push, branch switch or resource deletion to hide a mismatch. A clean, isolated candidate may be fast-forwarded/checked out only under its exact authorized task and after safe preflight; otherwise stop the mutating step.
- A required local Agent handoff is English and self-contained: repository/root/branch, verified HEAD/base, sole writer, allowed/forbidden resources and commands, early stop conditions, real tests, private evidence paths, restoration and Turkish report. Authorized task-owned commits must reach only their agreed review branch via non-force push, followed by remote SHA verification; local commit/test success alone is not publication. Read-only auditors do not modify refs, source, devices or files.

### Independent reviewer and evidence provenance

- After the stable change has relevant test evidence, a **genuinely separate read-only executor** (different ChatGPT conversation/Agent execution/person from the source writer) must review the **complete relevant current diff and real source**, decision/Issue, risks, security, ISR/SDK/task ownership, NVS/Zigbee/relay boundaries, negative tests and actual CI. The code writer's self-review, green CI or a GitHub account name alone **never** establishes independence.
- Place one concise review in the existing PR with **REVIEW-PROVENANCE-v1**: exact repo/PR/HEAD/base; actual writers or UNVERIFIED; reviewer execution/separation and assignment provenance (never invented session IDs); shared GitHub transport vs true author; inspected files/risks; actual test/CI/physical evidence and limits; findings; **PASS / FAIL / BLOCKED** and #ELI5. A relayed Agent report must be labeled **AGENT-REPORTED**, not falsely signed `APPROVED`. If independence or required source coverage cannot be established, merge gate is **BLOCKED**. A reviewer who changes source becomes a writer and requires a different independent reviewer for that change.
- Use only changed-risk-proportionate re-review and rerun where earlier proof still applies to the **current** HEAD/base. A new HEAD, base, scope, affected SDK version or security/physical risk requires fresh applicability checks. Never treat `SKIPPED`, canceled, wrong-SHA CI, unrelated user reports or a mock as PASS.

### Merge readiness and autonomous execution

1. **Before making a draft PR ready or merging:** confirm approved task/Issue scope, complete current changed-file inventory and all relevant source; no undisclosed mixed changes, unresolved merge-blocking review finding, unapproved architecture/security/product choice or explicit hold. Do not confuse an independently accepted **narrow patch** with full closure of unrelated root-cause, RF, binding or watchdog Issues.
2. Verify latest exact **HEAD and base**, actual required **Linux firmware/native and Windows layout/helper** CI/status checks on that source, test counts, zero compiler/linker errors **and warnings**, applicable negative/security checks, and necessary real firmware/physical acceptance with documented baseline/reboot/NVS/network/relay/restoration proof. Documentation-only changes do not require rebuilding unchanged firmware, but relevant required PR checks still must run, not be silently skipped. No physical PASS when its test was not performed.
3. Check actual PR mergeability and any enabled branch protections/rulesets; if inaccessible and compliance cannot be established, record **UNVERIFIED/BLOCKED** rather than assuming protections exist or not. Mark stable draft ready only with complete applicable acceptance and genuine independent review; verify any newly triggered CI after changing draft status.
4. **When all gates pass, perform GitHub's normal PR merge autonomously without asking again**, supplying an expected HEAD SHA where available. **Never push directly or force-push to `main`.** If HEAD/base moved, CI/review evidence is stale, a required test or acceptance is pending, or any blocker exists: **do not merge**; fix within authority or report the exact owner/next step. Do not enable unattended auto-merge as a shortcut.
5. After success, independently read actual merged/main SHA and PR/Issue state; record exact merge SHA, review provenance, CI and acceptance evidence in the existing GitHub record. **Merge is not deployment or full product acceptance**. Leave Issues with unfulfilled criteria open and keep secrets, local identities and diagnostic artifacts out of public records.

Separate Product Owner authorization is still required for **new scope, major architecture/security decisions, paid/external services (paid options are forbidden), destructive flash/NVS/network operations, mains/relay electrical risks, new physical interventions, irreversible migration and any explicit PR hold**. A merge approval does not grant hardware upload, converter deployment, restoration bypass or Issue auto-closure.

## Workstation and live access

The live helper reads non-secret connection locations from the untracked `.racool-local.json` or `RACOOL_LOCAL_CONFIG`. Copy `.racool-local.example.json` to `.racool-local.json` and fill in local paths/host, or use environment overrides. For live operations, set `deviceIEEE` in the local JSON or `RACOOL_DEVICE_IEEE` to a full IEEE address when multiple RaCool devices exist. Without a selector, only a single matching device is accepted; ambiguous, missing selected or duplicate identities fail before mutations. Environment overrides: `RACOOL_ZIGBEE_CONFIG`, `RACOOL_LIVE_CONVERTER`, `RACOOL_MQTT_HOST`, `RACOOL_MQTT_PORT`. Never put credentials in this file; the helper reads them from the existing Zigbee2MQTT configuration.

Keep workstation-specific addresses, device identifiers and mounted paths in the untracked `AGENTS.local.md`. Read it when available; never copy its values into public instructions. Run commands from the repository root. Discover ports with `pio device list`; only one process may own the selected serial port. Read MQTT credentials from the local configuration into memory only, never into logs or command arguments.

Use one consistent PlatformIO Core for IDE and command-line work. Prefer `pio` on PATH. For an IDE-managed installation on Windows, resolve its Python with `Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'`; honor `PLATFORMIO_CORE_DIR` if configured. PowerShell uses `$env:USERPROFILE`, Windows cmd uses `%USERPROFILE%`, and POSIX shells use `$HOME`. Do not hardcode a user's home directory. Avoid competing PlatformIO versions that replace shared packages. Do not run concurrent builds in the same output directory or kill an IDE-owned process. When IDE configuration runs overlap, set `PLATFORMIO_BUILD_DIR` to a separate ignored verification directory for the command-line process. CMake temporary files must not be shared by simultaneous configuration runs.

PlatformIO owns this ESP-IDF project's configure/build environment. Workspace CMake Tools settings resolve the verified PlatformIO-managed CMake under `${userHome}/.platformio/packages/tool-cmake/bin/cmake` and disable independent configure-on-open. The extension remains installed and other workspaces keep their own settings. If PLATFORMIO_CORE_DIR relocates packages, resolve its actual executable and update only the local workspace path; do not assume the default home directory. A Bad CMake executable message containing an empty string can be failed automatic discovery, not an explicit empty setting. Inspect the extension's current logs and all setting scopes before changing packages. Quote `${PROJECT_DIR}` include paths in platformio.ini: Windows backslashes otherwise become escape characters in flag parsing and produce duplicated relative include paths. Regenerate c_cpp_properties.json from PlatformIO metadata/template, never hand-maintain its generated paths.

## Verification and test layout

Mirror production module directories under test/. Module live helpers belong in diagnostic/, hardware cases in target/, and shared mocks/support/integration in Infrastructure/. The two flat test_target_* directories are PlatformIO discovery wrappers; manifests distinguish entries from case sources. Move consumers, imports, manifests, runners and documentation together.

Run the full native baseline before source modifications and complete related verification after repairs. Documentation-only edits do not require rebuilding unchanged firmware. Inventory and runners must reject zero cases, unmatched filters, missing named cases, ignored tests, compiler errors, crashes and timeouts. Do not omit failures or revive retired APIs to satisfy obsolete tests. Run negative acceptance tests when changing runner/layout rules. Use test/README.md, native_suites.json and target_suites.json as the test map.

Build/upload acceptance requires zero errors AND zero warnings in the complete captured compiler, linker, configure and upload output. A successful exit code is insufficient. Fix causes; do not suppress diagnostics or weaken acceptance. Keep intentional runtime fault-injection logs distinct from build diagnostics. Do not interrupt unrelated active work merely to start a build. After changing CMake diagnostic options, verify the generated cache and compile definitions; an incremental success or --disable-auto-clean can preserve stale options. If needed, preserve and refresh only the verified generated configuration cache, then confirm diagnostic flags are OFF and a real production boot contains no diagnostic output.

Do not combine PlatformIO `nobuild` and `upload` in this ESP-IDF project: that path omits framework flash-layout initialization and can use the default 0x10000 application offset instead of the configured partition. Use the normal upload target. If recovery requires direct SDK tooling, verify every offset against the generated flash manifest and partition table before writing, preserve NVS/network regions, and confirm a real application boot afterwards.

```powershell
node test/check_test_layout.mjs
node test/check_test_inventory.mjs
powershell.exe -ExecutionPolicy Bypass -File test/run_native_tests.ps1
node test/run_helper_tests.mjs
pio run -e esp32-c6-devkitc-1
powershell.exe -ExecutionPolicy Bypass -File test/run_target_tests.ps1 -Port $env:RACOOL_SERIAL_PORT
```

Use one serial owner. Discover ports; keep USB Serial/JTAG as the primary bidirectional console in production, diagnostics and targets. Secondary USB output alone is insufficient. Preserve disconnect evidence, reconnect within bounded deadlines and never blindly resend a mutable command after uncertain delivery. Restore production after temporary target/diagnostic firmware, checking warning-free upload and actual boot.

## Live mutations and evidence

Before mutable live tests, save fresh owner values to an evidence baseline. Restore in finally and verify restoration. Helpers that reboot must observe a new real boot and wait for actual command registration. Refuse foreign closing/recovery/menu state rather than changing another operation. Never print secrets or invoke credential-bearing commands through generic capture.

The authoritative converter is tools/racool.mjs. Edit there first, test it and deploy through bridge/request/converter/save with a backup; never edit the mapped live copy directly. Compare SHA-256 after converter revision/deployment only. External live changes require inspection and reconciliation into the source before deployment. File equality/save acknowledgment does not establish runtime activation: verify fresh bridge/devices, discovery, bounds, options and routes; use the helper's checked restart if the running model remains stale.

```powershell
node test/Communication/Zigbee/diagnostic/live_zigbee.mjs inspect
node test/Communication/Zigbee/diagnostic/live_zigbee.mjs deploy
node test/Communication/Zigbee/diagnostic/live_zigbee.mjs configure
node test/Communication/Zigbee/diagnostic/live_zigbee.mjs verify
```

Distinguish admission, completed worker application, correlated ZCL readback, fresh non-retained MQTT state, SDK output and physical observation. Merged MQTT state can contain cached fields. Reports require correlated reporting configuration and bounded actual traffic evidence, including restoration of log level. Do not GET a power command after it disables the radio. Network tables show routing relationships, not measured transit traffic.

Native mocks are not hardware evidence. Real SDK loopback is not external cable throughput. Check the UART pin route before external traffic or wiring. Software sleep is not electrical disconnection; OLED wake, chip wake and timer wake are distinct. Worker LED targets are not optical output. Passing tests cannot establish absolute correctness.

## OLED inspection and physical sessions

Capture actual successful SSD1306 page payloads with test/Hardware/OLED/diagnostic/capture_frame.py. Require complete ordered records, sequence and checksum; retain power/inversion/orientation metadata. Stored RAM pixels are not a lit display. Review live pixel output before user acceptance; one frame does not cover every animation phase. Inspect integer-scaled monochrome previews at original resolution without resampling. Corroborate claims of missing pixels with raw frame or region comparisons; a resized preview alone is insufficient.

Inspect and revise only one screen at a time against its Stitch RaCool reference, then validate transmitted pixels before moving on. Autonomous menu navigation is authorized. Do not confirm destructive actions to capture a page: a first click may enter a default-No confirmation, never send its action click. Preserve configuration and locale.

For the current user-authorized physical observation session, keep the screen in always-on mode across turns. Keep menu auto-exit disabled with `menu auto-exit off`; its factory default is enabled and the setting persists across restart. Save its original automatic mode and timeout, verify the applied owner state and track restoration explicitly in the active GitHub Issue or PR checklist. Restore automatic mode, run `menu auto-exit on`, and verify the original timeout plus the enabled auto-exit value only when physical observations end, or the user requests restoration. Other temporary tests retain their checked finally restoration. Do not repeat accepted physical observations without identifying a relevant change or missing coverage. Give one clear action/expected observation and wait for the user.

## Critical implementation boundaries

ConfigurationManager owns validation/persistence. Preserve schema sizes, offsets and explicit migration; register new fields in the write allowlist and test valid writes through it. Hardware V8 is 76 bytes, version offset 72, builtin_led_startup_anim at byte 2; migrate V7 without resetting encoder, colors, brightness or external boot selection. Built-in startup uses its existing independent worker/shared catalog; preserve saved/runtime defaults, dark frames, phase, notification fallback and global-disabled/fault/pairing precedence.

SDK ownership, callback reservations and cooperative worker shutdown must survive timeout/failure. Stop must preserve resources until outstanding operations drain; do not forcibly delete a worker holding locks or buffers. Health/state snapshots must not block on ordinary transfer locks or fabricate readiness. I2C resource readiness is not a bus probe; retain SDK/PEC/freshness evidence. ESP-IDF owns newlib reentrancy; never replace __getreent with a shared pointer.

Rebuild only the derived Zigbee reporting dataset through the public SDK API before endpoint registration; errors prevent registration and unwind ownership. Never wipe network storage or use private SDK fields to repair reporting. Preserve pending connected notifications under contention; disconnect cancels undelivered work. SDK factories own mandatory attributes; do not add duplicates.

Product catalogs in resources/translations are UTF-8 JSON discovered at build time. Keep generated catalogs/fonts build-local, unchanged bytes/mtime stable, lv_font_conv pinned and licensed Noto checksum verified. tools/product_resources.cmake skips script-mode scanning; tools/platformio_manifest.py fingerprints resource inputs and LOC_PRINTF consumers without including unrelated edits. Localization snapshots are immutable for a frame/response. Glyph coverage does not establish shaping or physical readability.

Production diagnostic flags remain OFF. Extended diagnostic boot phases are not evidence of normal boot timing. Preserve 0.1 degree range resolution and min_gap in HA cards; mode selectors remain accessible when inactive controls hide. Back up and compare the live editor configuration before dashboard replacement.
