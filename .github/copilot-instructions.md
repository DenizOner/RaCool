# GitHub Copilot Instructions for RaCool

## 1. Project Overview & Architecture
RaCool is an embedded, mains-powered ESP32-C6 (revision 0.1, 8 MB flash) device with FreeRTOS, ESP-IDF 5.3.1, native Zigbee (SDK 2.0.5), an SSD1306 128×64 LVGL OLED display, KY-040 rotary encoder, MLX90614 infrared temperature sensor, a fan control relay, and two RGB indicators (builtin WS2812 and external RGB).
- Native Zigbee integration with Home Assistant through Zigbee2MQTT is a primary product requirement.
- There is NO battery, PMIC, or physical power-cut circuit. Software sleep must never be described as electrical power disconnection.
- Always consult `AGENTS.md` for durable engineering directives and `CODEBASE.md` for module responsibilities, interfaces, and architecture.

## 2. Language & Documentation Boundaries
- **Source Code & Comments**: Identifiers, APIs, executable agent instructions, and **code comments must remain strictly English**. Preserve exact technical symbols and translate legacy comments only in edited code.
- **GitHub & User Communications**: Issue titles, acceptance criteria, PR descriptions, and user-facing reports are in **Turkish**.
- **Product Labels**: UI/OLED display labels and user-facing CLI text may be Turkish where specified by product specifications.

## 3. Git, Worktree & Single Writer Protocol
- All commits and PRs must use the verified author identity: `DenizOner <12309838+DenizOner@users.noreply.github.com>`.
- One writer per affected branch and isolated worktree under `.worktrees/<task-name>/`.
- **Never push directly or force-push to `main`**. All changes go through authorized feature/fix branches and Pull Requests.
- Per Product Owner standing approval (Issue #20), once all applicable verification gates and independent review passes, authorized automated PR merge is permitted.

## 4. Concurrency, Memory & Hardware Safety
- **Tick Rate**: FreeRTOS tick rate is 100 Hz (10 ms per tick). Never use `vTaskDelay(0)` or delays smaller than 1 tick (`pdMS_TO_TICKS(10)` minimum).
- **ISR Safety**: Event, ISR, and timer callbacks must remain non-blocking. Never allocate memory, log through unsafe paths, or acquire standard mutexes in an ISR.
- **Validation**: Validate all inputs at module boundaries (bounds, finite numbers, integer overflow, enum bounds) before mutating NVS or runtime settings.
- **Compiler Diagnostics**: Zero compiler and linker errors AND zero warnings are required for firmware builds (`pio run -e esp32-c6-devkitc-1`).
- **Secrets & Privacy**: Never commit credentials, private Zigbee network keys, or auth storage dumps.

## 5. Verification Gates
Always run canonical verification before submitting or merging PRs:
1. Test Layout: `node test/check_test_layout.mjs`
2. Test Inventory: `node test/check_test_inventory.mjs`
3. Host Native Suites: `node test/run_native_tests.mjs`
4. Helper Suites: `node test/run_helper_tests.mjs`
5. Firmware Build: `pio run -e esp32-c6-devkitc-1`
