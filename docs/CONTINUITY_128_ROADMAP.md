# Continuity Expansion Roadmap (48 -> 128 Zones)

Current state:

- Firing relay capacity: 128 zones (8 PW535 boards x 16 relays)
- Continuity scan coverage: 48 zones

Reason for mismatch:

- ADS1115 provides 4 ADC channels.
- Current design uses 3 channels for continuity through 3 mux outputs (A0, A1, A2).
- Each mux channel contributes 16 zone measurements.

## Hardware Required to Reach 128-Zone Continuity

Option A (recommended):

- Add additional ADC capacity (for example more ADS1115 devices on primary I2C).
- Provide 8 continuity analog paths total (one per 16-zone segment).
- Route each 16-zone segment output to a dedicated ADC channel.

Option B:

- Use a higher channel-count ADC subsystem that can sample at least 8 continuity lanes.

## Firmware Work Items

1. Extend continuity channel mapping constants beyond current 3 channels.
2. Add detection/init for additional ADC devices and fail-safe handling.
3. Update scan loop to classify all 128 zone continuity values.
4. Keep scan cadence non-blocking so relay auto-off timing and e-stop responsiveness are preserved.
5. Add telemetry to distinguish ADC missing vs open-circuit states.

## UI Work Items

1. Add explicit continuity coverage indicator in Settings and Show pages.
2. Display expanded continuity states for zones 49 to 128 once hardware support is active.
3. Keep unknown/no-monitor state visually distinct from open-circuit.

## Validation Plan

1. Cold-boot detection of all continuity ADC devices.
2. Inject known continuity values on representative zones across all 8 segments.
3. Confirm no watchdog or loop starvation while scanning full coverage.
4. Confirm firing and e-stop timing remain deterministic under full continuity scan load.
