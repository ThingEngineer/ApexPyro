# Security Policy

## Scope

ApexPyro is an ESP32-based controller for pyrotechnic hardware. This repository contains firmware and web UI code that can control arming and firing behavior.

Security issues can directly affect physical safety. Treat any bug that can bypass arming checks, E-Stop behavior, command authorization, or relay-safe defaults as high severity.

## Supported Code Line

- Security fixes are applied on the current `main` branch.
- Tagged historical releases are not guaranteed to receive backported security fixes.

## Reporting a Vulnerability

Use this repository's Issues as the reporting channel.

1. Open a new issue in this repository.
2. Prefix the title with `[SECURITY]`.
3. Include enough detail for maintainers to reproduce and validate.

Required report content:

- Affected area (for example: WebSocket command handling, role lock, E-Stop, WiFi credential flow)
- Exact firmware/UI revision (commit SHA if possible)
- Reproduction steps
- Expected behavior vs actual behavior
- Safety impact assessment
- Logs, payload samples, screenshots, or serial output

No fixed response SLA is currently guaranteed.

## Disclosure Expectations

Because reports are submitted as GitHub issues in this project, assume reports are public.

- Keep proof-of-concept details focused on reproducibility and risk.
- Avoid publishing unnecessary details that increase misuse risk before maintainers can assess impact.
- Maintainers may request additional validation artifacts in the issue thread.

## What This Policy Covers

Examples of in-scope security issues:

- Authorization bypass for controller-only actions
- Signature/replay validation bypass for signed commands
- E-Stop bypass or reset-policy bypass
- Unsafe arming or relay activation behavior caused by software flaws
- Credential exposure bugs beyond current documented architecture

Examples typically out of scope for security triage:

- Feature requests without a security impact
- Performance tuning without security or safety consequence
- UI-only styling defects

## Current Security Posture (Summary)

Current design uses layered controls:

- Hardware fail-safe defaults (master arm off by default, relay-off boot posture)
- Controller/viewer authorization model on WebSocket commands
- HMAC-SHA256 command signing for fire/arm/auto-start class commands
- E-Stop latch and role-restricted reset flow
- Connection-loss safety policies (`abortOnDisconnect`, `disarmOnReloadOrDrop`)

Known constraints to account for during deployments:

- WebSocket transport is HTTP on port 80 (no TLS in firmware)
- AP default credentials are defined in firmware defaults and should be changed for real deployments
- WiFi credentials are stored on-device for operation convenience

## Additional Technical Detail

- Architecture and threat model: [docs/SECURITY_ARCHITECTURE.md](docs/SECURITY_ARCHITECTURE.md)
- Operator procedures: [docs/APEXPYRO_WEB_USER_MANUAL.md](docs/APEXPYRO_WEB_USER_MANUAL.md)
- Wiring and hardware safety context: [docs/ESP32_DEVKITC_V4_WIRING.md](docs/ESP32_DEVKITC_V4_WIRING.md)
- Contributor safety requirements: [CONTRIBUTING.md](CONTRIBUTING.md)
