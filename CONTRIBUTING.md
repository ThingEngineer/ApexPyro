# Contributing to ApexPyro

Thank you for contributing to ApexPyro.

This project controls pyrotechnic hardware. Contributions are welcome, but safety-critical behavior must remain deterministic and fail-safe.

## Before You Start

- Read the project overview in `README.md`.
- Review hardware wiring references in `docs/` before proposing hardware-adjacent behavior changes.
- Keep proposed changes focused and scoped to a single intent when possible.

## How To Report Issues

When opening a bug report, include:

- Firmware and environment details (`platformio.ini` target, board variant, monitor speed if relevant)
- Reproduction steps (exact sequence)
- Expected behavior and actual behavior
- Safety impact (if any)
- Relevant serial logs or screenshots

For safety-adjacent incidents (arming, relay firing, E-Stop, kill-switch, boot safety), clearly label the report as safety-critical.

## Branches, Commits, and Pull Requests

- Use a focused branch per change.
- Keep commits logically grouped and clearly messaged.
- Open PRs with a concise summary, risk level, and validation evidence.
- Link related issues and call out any protocol or wiring-impacting changes.

PR descriptions should include:

- What changed
- Why it changed
- Safety impact assessment
- Validation steps and results

## Safety-Critical Change Requirements (Mandatory)

For any change touching arming, firing, relay outputs, kill-switch, emergency stop, boot initialization order, pin/timing defaults, or realtime command/state handling:

- Preserve fail-safe defaults (outputs remain OFF unless explicitly armed and commanded).
- Do not bypass, weaken, or delay E-Stop and kill-switch behavior.
- Do not reorder boot safety GPIO initialization behind peripheral startup.
- Keep changes minimal and local to the required behavior.
- Provide validation evidence in the PR (build + behavior checks).

If a safety requirement cannot be preserved, do not merge without explicit maintainer approval and documented rationale.

## Build and Test Expectations

Minimum required validation for firmware-impacting changes:

```bash
pio run
```

If web UI files are changed, also validate embedded script syntax and behavior expectations for changed flows.

Recommended evidence to include in PR:

- `pio run` success output summary
- Notes on manual verification for changed behavior
- Any additional checks relevant to safety-critical logic

## Web UI and Backend Contract Alignment

- Keep frontend actions aligned with backend websocket message contracts.
- If message shapes, command names, or state payloads change, update both sides in the same PR.
- Ensure reconnect/resync behavior remains coherent after protocol-affecting edits.

## Coding Style and Scope

Style guidance is advisory, but consistency is expected:

- Prefer minimal diffs over broad refactors.
- Keep naming and structure aligned with the surrounding code.
- Avoid unrelated formatting churn.
- Add short comments only where logic is non-obvious.

## Documentation Expectations

If behavior or operator workflow changes, update docs in the same PR:

- `README.md` for high-level changes
- `docs/` for wiring or hardware procedure changes
- `hardware/README` for hardware-design documentation scope

## Contributor Conduct and Responsibility

By contributing, you acknowledge this repository controls pyrotechnic hardware and agree to prioritize safe behavior, clear validation, and transparent risk communication in all submitted changes.
