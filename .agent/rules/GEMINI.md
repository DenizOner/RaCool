---
trigger: always_on
description: "Compatibility entry point for RaCool's optional legacy Antigravity Kit."
---

# RaCool legacy Antigravity Kit compatibility

This rule is a compatibility entry point, not a second source of engineering authority.
Follow the repository-root [AGENTS.md](../../AGENTS.md) and the Product Owner's latest
approved decisions for permissions, safety, tests, review, and conditional PR merge.
Use [CODEBASE.md](../../CODEBASE.md) to inspect current architecture and consumers.
If any loaded Agent, Skill, workflow, personal rule, or prompt conflicts with those
requirements, do not follow that conflicting instruction; report the conflict instead.
Do not assume that a Markdown file's existence proves a specific harness loaded it.

## Optional legacy references

- The existing `.agent/agents/`, `.agent/skills/`, and `.agent/workflows/`
  are optional task references. Keep them available to existing local workflows.
- Read a relevant Agent or Skill only when the task or the user calls for it.
  No automatic specialist announcement, parallel agent, Socratic questioning gate,
  task-plan file, or generic web/mobile checklist is required for routine work.
- An Agent or Skill is guidance, not a permission grant. The primary authorized
  executor handles reachable source, GitHub, and CI work directly.
- Existing `.agent/scripts/` and Skill scripts are optional tools, not RaCool's
  test authority. In particular, a missing or skipped script is never a PASS.
  Use the project-specific test commands and manifests from `AGENTS.md`,
  `CODEBASE.md`, and `test/README.md`. Do not replace or weaken their checks.

## Two representative task boundaries

**Ordinary authorized development:** inspect the actual Issue, current interfaces
and users of changed code; respect one writer and the baseline tests; make the
smallest approved change; run applicable RaCool tests and verify final-HEAD CI.
Do not force an optional legacy Agent or extra approval for a covered small fix.
Conditional PR merge follows `AGENTS.md` and the approved Issue #20 decision,
not a blanket requirement to ask for another merge approval.

**Independent read-only review:** only a separate execution session may give
the independent source/CI review. The reviewer must inspect the full relevant
current diff, exact HEAD/base, negative cases, safety boundaries and actual CI;
report PASS/FAIL/BLOCKED with evidence. The reviewer must not change code,
branches, CI, device settings, firmware, Zigbee, NVS, or PR merge state.
A review comment in the existing PR is the only authorized review-side write.

The project has no general license for auto-flash, reset, network mutation,
paid services, secrets disclosure, unattended auto-merge, or extra code writers.
The legacy catalog can be used where supported without changing any of these
project boundaries. Actual activation and tool availability require a test in
the selected local Antigravity/VS Code/Codex harness.
