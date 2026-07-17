# ApexPyro Web User Manual

This manual explains how to operate the ApexPyro web interface safely and consistently.

Use this guide with the wiring reference: [ESP32 DevKitC V4 Wiring](ESP32_DEVKITC_V4_WIRING.md).

## 1. Before You Begin

This system controls pyrotechnic hardware. Treat all operations as safety-critical.

- Confirm hardware is correctly wired and validated before connecting live igniters.
- Keep Master Arm OFF until all checks are complete.
- Verify an Emergency Stop (E-Stop) path is available and understood by all operators.
- Use controlled, legal operating environments only.

## 2. Quick Start

### 2.1 Connect to Controller

1. Power the controller and wait for boot completion.
2. Connect to ApexPyro over one of these paths:
3. AP Mode: connect to the controller-hosted WiFi network.
4. STA Mode: connect through your existing LAN once credentials are configured.
5. Open the ApexPyro web UI in a browser.

### 2.2 Confirm Safe Initial State

1. Check System Status shows healthy connectivity.
2. Confirm Master Arm is OFF.
3. Confirm no zones are actively firing.
4. Confirm continuity is reporting expected zone status.

### 2.3 Assign Operator Role

1. Confirm your client is in Controller role before attempting arm, fire, or settings changes.
2. If Role Lock is enabled, use the lock owner workflow to claim control.

## 3. Operator Workflows

### 3.1 Read the Status Area

Use the status cards to verify:

- WiFi mode and connection state (AP or STA).
- Master Arm state.
- Enabled zone counts.
- Overall controller/system readiness.

Do not arm if status shows unresolved errors or disconnect warnings.

### 3.2 Master Arm Procedure

1. Complete continuity and show configuration checks.
2. Notify operators that arming is about to occur.
3. Enable Master Arm.
4. Verify arm indicators update in UI.
5. Keep Master Arm enabled only during active operation windows.
6. Disable Master Arm immediately after operation.

### 3.3 Manual Show Mode (Ready -> Fire)

Manual mode uses a two-step confirmation model.

1. Select Manual mode.
2. Choose a zone (or group) and put it into Ready state.
3. Confirm the UI Ready indicator is active.
4. Execute Fire while still within the Ready window.
5. Validate post-fire state updates.

If the Ready window expires, repeat the Ready step before firing.

### 3.4 Auto Show Mode

1. Select Auto mode.
2. Verify enabled zones/groups and sequence order.
3. Verify auto delay settings.
4. Arm the system only when all checks pass.
5. Start Auto show.
6. Monitor progress and system status continuously.
7. Use Stop immediately for unexpected behavior.

### 3.5 Emergency Stop (E-Stop)

1. Trigger E-Stop immediately if unsafe behavior is observed.
2. Confirm firing is halted and system state indicates emergency stop active.
3. Follow reset policy before returning to operation.
4. Re-validate continuity, arm state, and show configuration after reset.

## 4. Show Builder Workflows

### 4.1 Create and Edit Zones

1. Open Show Builder.
2. Create or edit zone labels and sequence order.
3. Set each zone enabled/disabled state as needed.
4. Save changes.

### 4.2 Group Zones

1. Assign zones to groups for simultaneous firing.
2. Verify group membership and firing intent carefully.
3. Re-check any overlap between group and individual zone plans.

### 4.3 Configure Auxiliary Relays

1. Name each auxiliary relay by its real-world function.
2. Validate each aux control in a safe test state before live use.

### 4.4 Import and Export Show Data

Use export before major edits as an operational backup.

1. Export current show configuration and keep a dated copy.
2. Import only trusted files from your operation set.
3. After import, review zones, groups, and settings before arming.

## 5. Settings Reference

### 5.1 WiFi Settings

- AP Mode: direct local control path.
- STA Mode: controller joins existing network.
- Credential reset/forget options should be treated as maintenance actions.

### 5.2 Igniter Timing

- Igniter pulse duration controls relay on-time for firing events.
- Keep values within validated hardware limits.
- Re-test after any timing change.

### 5.3 Safety Settings

- E-Stop reset mode controls how recovery is allowed.
- Disconnect-related safeguards may stop active show behavior by policy.
- Keep conservative safety defaults unless your test process validates alternatives.

### 5.4 Continuity Calibration

- Continuity thresholds classify zone health states.
- Tune only when measurement hardware and wiring are stable.
- Re-check known-good and known-open channels after changes.

### 5.5 Battery Monitoring

- Battery curve settings map measured voltage to state-of-charge.
- Validate curve behavior against known voltage points before relying on readings.

## 6. Troubleshooting

### 6.1 Cannot Arm

Check:

- You have Controller role.
- No active E-Stop condition.
- Required safety preconditions are satisfied.
- UI is connected and synchronized.

### 6.2 Zone Won't Fire

Check:

- Zone is enabled.
- Master Arm is ON.
- In Manual mode, zone/group is still inside Ready window.
- Continuity status is acceptable for that channel.
- Relay and wiring path are validated.

### 6.3 Auto Show Stops Unexpectedly

Check:

- Disconnect handling policy and recent network events.
- E-Stop or safety lockout events.
- Zone/group configuration validity.

### 6.4 Continuity Looks Incorrect

Check:

- Sensor wiring and address configuration.
- Threshold calibration values.
- Known-test channel comparisons.

### 6.5 Recovery Checklist After Fault

1. Disarm system.
2. Clear E-Stop condition if active.
3. Reconnect and regain Controller role.
4. Re-validate settings, continuity, and show data.
5. Perform safe dry-run checks before live operation.

## 7. Glossary

- Zone: A single firing channel.
- Group: A set of zones intended to fire together.
- Master Arm: Global gating control for firing capability.
- E-Stop: Emergency stop action to halt firing immediately.
- Ready State: Temporary pre-fire state used in Manual mode.
- Manual Mode: Operator-confirmed per-step firing workflow.
- Auto Mode: Sequenced firing workflow using configured order and timing.
- AP Mode: Controller-hosted WiFi access point mode.
- STA Mode: Controller joins an external WiFi network.
- Controller Role: Client role with authority to arm, fire, and configure.
- Viewer Role: Read-only client role.
- Role Lock: Restriction that keeps controller authority with lock ownership.

## 8. Reference Appendix

### 8.1 Primary UI Areas

- Status Overview
- Show Builder
- Show Mode (Manual/Auto)
- Safety Controls
- Settings
- Hardware Test

### 8.2 Operational Safety Gates

Before any firing operation:

1. Connectivity stable.
2. Correct operator role confirmed.
3. Continuity reviewed.
4. Intended sequence and groups reviewed.
5. Master Arm enabled only when ready to execute.

### 8.3 Related Project Documentation

- [ESP32 DevKitC V4 Wiring](ESP32_DEVKITC_V4_WIRING.md)
- [128-Zone Continuity Roadmap](CONTINUITY_128_ROADMAP.md)
- [Hardware Notes](../hardware/README)
