# ApexPyro Security Architecture

## 1. Purpose

This document describes the implemented security-relevant and safety-relevant architecture in ApexPyro firmware and web UI.

It is a technical reference for maintainers and reviewers. It documents what exists today, what assumptions it relies on, and what limitations remain.

## 2. System Context and Trust Boundaries

ApexPyro is an ESP32 controller with a browser UI over WebSocket (`/ws`) served from the same device.

Primary trust boundaries:

- Browser client ↔ ESP32 WebSocket endpoint
- Controller role ↔ viewer role authorization boundary
- Software command path ↔ hardware arming/firing relays
- Device local storage (NVS/LittleFS) ↔ runtime memory

Key files:

- `src/websocket_handler.cpp`
- `src/main.cpp`
- `src/relay_manager.cpp`
- `src/wifi_manager.cpp`
- `src/storage.cpp`
- `include/config.h`
- `data/index.html`

## 3. Threat Model (Current)

### In scope

- Unauthorized command attempts from connected clients
- Replay and tampering attempts against signed command payloads
- Unsafe behavior during disconnect/reconnect events
- Failing-safe behavior for arming/firing and emergency stop paths

### Assumptions

- Deployment occurs on a controlled local network or direct AP access path
- Operators control physical access to the controller hardware
- Browser runtime provides standard WebCrypto APIs for modern signing path

### Out of scope

- Internet-exposed operation with strong remote identity guarantees
- Transport-level confidentiality against LAN observers (firmware currently serves port 80)
- Physical extraction resistance for all persisted configuration values

## 4. Layered Controls

## 4.1 Hardware and Boot Fail-Safe Controls

Implemented in `src/main.cpp` and `include/config.h`:

- Boot sequence sets safety GPIO states before I2C/peripheral initialization.
- Master arm relay pin is initialized low (disarmed) at boot.
- Physical kill switch (`KILL_SWITCH_PIN`) is debounced and can trigger emergency stop.
- Physical WiFi reset button is debounced with long-press behavior.

Main-loop ordering remains:

1. continuity manager update
2. relay manager update
3. WebSocket handler update
4. show runner update
5. WiFi manager update

This ordering supports timely safety state propagation and relay auto-off behavior.

## 4.2 Authorization Model (Controller vs Viewer)

Implemented in `src/websocket_handler.cpp`:

- One active controller client ID, additional clients are viewers.
- Controller-only enforcement is centralized through `requireControllerRole(...)`.
- Dangerous operations are denied for viewers and return `UNAUTHORIZED` errors.

Controller-gated operations include:

- `fire`
- `fire_group`
- `arm`
- `aux`
- `estop`
- `estop_reset`
- `auto_start`
- `auto_stop`
- zone/config/settings mutation commands
- WiFi/AP credential mutation commands
- factory reset, relay test, serial monitor control, role lock change

Role-lock behavior:

- Role lock can preserve controller ownership across disconnects.
- Reclaim uses `client_hello` identity key matching.
- Timeout-based takeover is allowed after `ROLE_LOCK_RECLAIM_TIMEOUT_MS`.

## 4.3 Command Authenticity and Replay Controls

### Modern path: HMAC-SHA256

Client-side signing (`data/index.html`):

- Uses WebCrypto `subtle.importKey` and `subtle.sign` with HMAC SHA-256.
- Signature input format: `COMMAND:VALUE:TIMESTAMP_MS:NONCE`.

Server-side validation (`src/websocket_handler.cpp`):

- `validateCommandSignature(...)` checks:
  - required fields present
  - nonce length bounds
  - signature length
  - client identity key availability
  - nonce not reused for the client
  - timestamp monotonicity for the client
  - expected HMAC equality

Signed command families:

- `FIRE`
- `FIRE_GROUP`
- `ARM`
- `AUTO_START`

### Compatibility path: CRC checksum

If signature fields are absent, firmware can validate a legacy checksum path (`validateLegacyChecksum(...)`).

Important limitation:

- CRC is integrity-oriented compatibility logic, not cryptographic authentication.

### Client key behavior

Client identity key is created in the browser and stored in localStorage (`apx_client_key`) in `data/index.html`.

Implication:

- Treat this as operational client identity for role/session continuity, not as high-assurance user identity.

## 4.4 Emergency and Safety State Controls

Emergency paths in `src/websocket_handler.cpp` and `src/main.cpp`:

- E-Stop can be triggered by controller command or physical kill switch flow.
- Heartbeat timeout path can enforce abort/disarm behavior depending on settings.
- `triggerEmergencyStop(...)` aborts show, disarms master arm, turns relays off, and latches E-Stop state.

Reset policy:

- E-Stop reset mode is persisted and enforced.
- `POWER_CYCLE_ONLY` mode blocks software clearing.
- `TWO_STEP_CONFIRM` mode requires explicit confirmation sequence.

## 4.5 Connection Health and Rate Controls

Connection management in `src/websocket_handler.cpp`:

- Heartbeat ping and timeout tracking for controller activity.
- `ws.cleanupClients()` in update loop to clear stale clients.
- Per-client command rate limiting (window + max commands per window).

State propagation:

- full-state and status broadcasts keep clients synchronized after role/connection transitions.

## 4.6 WiFi and Network Controls

WiFi behavior in `src/wifi_manager.cpp`, defaults in `include/config.h`, and persistence in `src/storage.cpp`:

- Device supports AP mode and STA client mode.
- AP credentials are configurable and persisted.
- Client network credentials are persisted for reconnect behavior.
- WiFi reset flow restores default AP credentials and clears client credentials.

Transport note:

- HTTP/WebSocket server runs on port 80; firmware does not implement TLS termination.

## 4.7 Persistence and Data Handling

Storage behavior in `src/storage.cpp`:

- NVS stores WiFi credentials and settings.
- LittleFS stores show zone configuration (`zones.json`).
- Schema/version migration is implemented for settings defaults.

Operational note:

- Persisted values are intended for controller operation and recovery; physical access controls remain important.

## 5. Command Security Matrix

| Command Type                            | Controller Role Required | HMAC Path | Legacy Checksum Fallback |
| --------------------------------------- | ------------------------ | --------- | ------------------------ |
| `fire`                                  | Yes                      | Yes       | Yes                      |
| `fire_group`                            | Yes                      | Yes       | Yes                      |
| `arm`                                   | Yes                      | Yes       | Yes                      |
| `auto_start`                            | Yes                      | Yes       | Yes                      |
| `auto_stop`                             | Yes                      | No        | No                       |
| `estop`                                 | Yes                      | No        | No                       |
| `estop_reset`                           | Yes                      | No        | No                       |
| `aux`                                   | Yes                      | No        | No                       |
| `set_setting` and related config writes | Yes                      | No        | No                       |

## 6. Known Limitations and Residual Risk

Current design limitations to account for during deployment and review:

- No TLS in firmware transport (assume controlled local network boundary).
- Browser-stored client identity key is not equivalent to strong user authentication.
- Legacy checksum fallback is not cryptographic.
- AP credentials should be changed from defaults before real operations.
- Security posture depends on physical access control and network segmentation.

## 7. Validation Checklist for Security/Safety Changes

When modifying arming, firing, role control, WebSocket protocol, or WiFi behavior:

1. Confirm fail-safe boot GPIO ordering is unchanged.
2. Confirm controller-only actions still reject viewer clients.
3. Confirm signed command validation still rejects bad nonce/signature/timestamp cases.
4. Confirm E-Stop trigger and reset-mode behavior remains intact.
5. Confirm disconnect policies (`abortOnDisconnect`, `disarmOnReloadOrDrop`) still behave as configured.
6. Confirm reconnect/state resync works for controller and viewer clients.
7. Build firmware (`pio run`) and validate behavior with serial logs.

For contributor guardrails and operational procedure detail, also review:

- `SECURITY.md`
- `CONTRIBUTING.md`
- `docs/APEXPYRO_WEB_USER_MANUAL.md`
- `docs/ESP32_DEVKITC_V4_WIRING.md`
- `.github/instructions/safety.instructions.md`
- `.github/instructions/websocket.instructions.md`
