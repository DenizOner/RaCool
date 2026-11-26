---
trigger: always_on
---

# GEMINI.md - RaCool AI Agent Workspace Directives

> This file defines how the AI agent behaves in the RaCool workspace, bridging Antigravity / Gemini features with the authoritative project engineering directives in `AGENTS.md`.

---

## ⚖️ RULE HIERARCHY & AUTHORITY (CRITICAL - START HERE)

> **MANDATORY:** `AGENTS.md` contains durable, authoritative engineering and verification rules for the RaCool project.
>
> **Rule Priority:**
> `P0 (AGENTS.md - Durable Engineering Directives)` >
> `P1 (GEMINI.md - Workspace Agent Behavior)` >
> `P2 (Agent personas in .agent/agents/)` >
> `P3 (Domain skills in .agent/skills/)`.
>
> In any case of conflict, divergence, or ambiguity between `.agent/` files and `AGENTS.md`, **`AGENTS.md` strictly governs**.
> Never append dated logs, session summaries, or work logs to `AGENTS.md` or `CODEBASE.md`.

---

## 📥 REQUEST CLASSIFIER (STEP 1)

**Before ANY action, classify the request:**

| Request Type     | Trigger Keywords                           | Active Directives              | Expected Result             |
| ---------------- | ------------------------------------------ | ------------------------------ | --------------------------- |
| **QUESTION**     | "what is", "how does", "explain"           | TIER 0 only                    | Direct Text / Explanation   |
| **SURVEY/INTEL** | "analyze", "list files", "overview"        | TIER 0 + Read-only             | Concise Intel & Evidence    |
| **SIMPLE CODE**  | "fix", "add", "change" (single file/scope) | TIER 0 + TIER 1 (lite)         | Inline Edit + Verification  |
| **COMPLEX CODE** | "build", "create", "implement", "refactor" | TIER 0 + TIER 1 (full) + Plan  | Plan / Worktree / Small PR  |
| **HARDWARE/LAB** | "flash", "measure", "device", "physical"   | TIER 0 + Hardware Gate         | Explicit Approval / User Lab|

---

## 🤖 INTELLIGENT AGENT ROUTING & EMBEDDED CONTEXT (STEP 2)

RaCool is an embedded FreeRTOS / ESP-IDF C/C++ firmware project for ESP32-C6 (8 MB flash, native Zigbee, OLED, sensors).

1. **Embedded Default**: For firmware, hardware drivers, Zigbee, diagnostics, and test suites, work under the embedded engineering discipline defined in `AGENTS.md` and `CODEBASE.md`.
2. **Specialist Personas**: Personas in `.agent/agents/` (e.g., `orchestrator`, `debugger`, `security-auditor`) may be consulted for domain expertise.
3. **No Synthetic Rituals**: Do not require rigid boilerplate announcements (`🤖 Applying knowledge of...`) on embedded firmware edits. Keep communication direct, natural, and informative.
4. **Single Writer & Worktree**: Always respect the single-writer rule and keep branch mutations in isolated worktrees (`.worktrees/<task-name>/`).

---

## TIER 0: UNIVERSAL RULES (Always Active)

### 🌐 Language Handling

Per `AGENTS.md` engineering directives:

1. **User Communication**: Respond in the user's language (Turkish for GitHub issue titles, acceptance criteria, PR descriptions, and user reports).
2. **Code & Comments**: Source identifiers, APIs, executable agent instructions, and **code comments remain strictly English**. Preserve exact technical symbols and translate legacy comments only in edited code.
3. **Product Labels**: UI/display labels and user-facing CLI text may be Turkish where specified by product requirements.

### 🧹 Clean Code & Engineering Standards

**All code MUST follow `@[skills/clean-code]` and `AGENTS.md` directives:**

- **Interface Source of Truth**: Read actual interfaces (`IService`, module headers) before editing. Do not force unnecessary wrappers or singletons.
- **Concurrency & ISR Safety**: Keep event, ISR, and timer callbacks short. Dedicated workers block on queues. Never allocate, log unsafely, or take standard mutexes in an ISR.
- **Tick Rate & Delay**: Production tick rate is 100 Hz (10 ms per tick). Convert positive polling sleeps to at least one tick (`pdMS_TO_TICKS`).
- **Validation**: Validate all inputs at module boundaries before changing settings (range, overflow, finite numbers, enum bounds). Share validation across CLI, Zigbee, and OLED paths.
- **Secrets & Privacy**: Never commit credentials, private Zigbee network keys, or auth dumps.

### 📁 File Dependency Awareness

**Before modifying ANY file:**
1. Check `CODEBASE.md` for module responsibilities and interfaces.
2. Inspect consumers and dependents.
3. Update all affected callers and tests together.

---

## TIER 1: CODE RULES & AUTONOMOUS DELIVERY

### 📱 Project Type Routing

| Project Type | Primary Discipline | Directives & Skills |
| ------------ | ------------------ | ------------------- |
| **EMBEDDED FIRMWARE** (ESP32-C6, FreeRTOS, ESP-IDF, C/C++) | Embedded Software Engineer | `AGENTS.md`, `CODEBASE.md`, `clean-code`, `systematic-debugging` |
| **INTEGRATION / Z2M** (Zigbee2MQTT, Home Assistant) | Integration Specialist | `CODEBASE.md`, `api-patterns` |
| **HOST TOOLS & SCRIPTS** (Python, Node.js, Shell) | Tools Developer | `clean-code`, `testing-patterns` |

### 🛑 Socratic Gate vs. Autonomous Execution

Per Product Owner standing approval (14 November 2026, Issue #20):
- **Authorized / Ongoing Scope**: When working within an approved task, existing Issue, or explicit user direction, **work autonomously** on authorized changes and discoverable information. Do **NOT** block execution with arbitrary questionnaires or gate-keeping questions.
- **Unclear / New Scope**: Use Socratic questioning only when requirements are genuinely missing, ambiguous, or when a change exceeds authorized boundaries (e.g., new product scope, destructive flash/NVS operations, external paid services).
- **Physical Observations**: Observations requiring physical eye or multimeter checks on hardware require the user; complete automatic tests first, then explain one clear action and expected observation.

### 🏁 Final Checklist Protocol (RaCool Canonical Verification Gates)

**Trigger:** When completing code modifications, verifying fixes, or preparing PRs:

| Gate | Canonical Command | Purpose |
| ---- | ----------------- | ------- |
| **1. Test Layout** | `node test/check_test_layout.mjs` | Verifies test directory and discovery wrapper structure |
| **2. Test Inventory** | `node test/check_test_inventory.mjs` | Verifies suite counts, manifest entries, zero empty suites |
| **3. Host Native Suites** | `node test/run_native_tests.mjs` | Runs all host native Unity unit test suites |
| **4. Helper Suites** | `node test/run_helper_tests.mjs` | Runs Python/Node helper test suites |
| **5. Production Firmware** | `pio run -e esp32-c6-devkitc-1` | Compiles firmware; must achieve 0 errors and 0 warnings |
| **6. GitHub Actions CI** | CI workflow check | Exact-HEAD Linux, Windows, and CodeQL checks |

> 🔴 **Note:** Generic web tools (SEO, Lighthouse, Playwright) do not apply to RaCool firmware. The master runner `python .agent/scripts/checklist.py .` automatically executes RaCool's canonical gates when run in this repository.

---

## TIER 2: INDEPENDENT REVIEW & DELIVERY

Per `AGENTS.md` autonomous delivery policy:
- One writer per affected branch and isolated worktree under `.worktrees/<task-name>/`.
- Independent review provenance (`REVIEW-PROVENANCE-v1`) required before PR merge.
- Never push directly or force-push to `main`.
- Green CI or passing tests is evidence, not proof of absolute correctness; document tested/untested status accurately.
