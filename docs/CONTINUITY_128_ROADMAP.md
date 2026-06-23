# Continuity 128-Zone Implementation Checklist

## Target Architecture

- [ ] Primary I2C bus (`Wire` on GPIO 21/22) is relay-only and remains limited to the 8 PW535 firing boards at addresses `0x20` through `0x27`.
- [ ] Auxiliary I2C bus (`auxWire` on GPIO 26/27) hosts the auxiliary PW535 board at `0x20` plus all continuity ADS1115 devices.
- [ ] Shared mux select pins (`GPIO 16/17/18/19`) drive all 8 CD74HC4067 muxes in parallel.
- [ ] Continuity coverage spans all 128 firing zones using 8 mux lanes x 16 positions.
- [ ] ADS1115 `0x48` reads muxes 1-4, ADS1115 `0x49` reads muxes 5-8, and ADS1115 `0x4A` reads battery voltage plus spare channels.

## Firmware Checklist

### Hardware constants and init

- [ ] Update [include/config.h](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/include/config.h) to define the auxiliary-bus continuity model explicitly.
- [ ] Add or revise ADC address/channel constants so the 8 continuity lanes map cleanly to the 3 ADS1115 devices.
- [ ] Keep the `MAX_ZONES = 128` relay model unchanged.
- [ ] Preserve boot-safe GPIO ordering before any I2C activity.

### Continuity manager refactor

- [ ] Refactor [src/continuity.cpp](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/src/continuity.cpp) and [include/continuity.h](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/include/continuity.h) to support multiple ADS1115 instances on `auxWire`.
- [ ] Initialize each ADS1115 with `ads.begin(address, &auxWire)`.
- [ ] Keep gain configuration and threshold classification behavior unchanged.
- [ ] Scan one mux position at a time, wait for the existing 1 ms settle time, then sample all 8 lanes for that position.
- [ ] Preserve non-blocking update behavior so continuity work does not starve relay timing or e-stop handling.
- [ ] Keep battery reads on ADS1115 `0x4A` and ensure a battery read fault does not disable continuity reads on the other devices.
- [ ] Degrade missing ADC coverage to `UNKNOWN` for only the affected zones or lane block instead of disabling the full manager.

### Boot and bus isolation

- [ ] Update [src/main.cpp](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/src/main.cpp) so primary I2C initialization stays relay-only.
- [ ] Initialize the auxiliary PW535 board and the continuity ADS1115 devices on `auxWire` without changing safe relay-off behavior.
- [ ] Keep kill-switch, master arm, and other safety initialization ahead of peripheral traffic.

### Telemetry and UI

- [ ] Verify [src/websocket_handler.cpp](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/src/websocket_handler.cpp) still broadcasts a complete 128-zone continuity payload.
- [ ] Update [data/index.html](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/data/index.html) so no user-facing copy still says continuity is limited to zones 1-48.
- [ ] Keep continuity help text, status text, and zone rendering aligned with the new hardware model.

## Documentation Checklist

- [ ] Update [docs/ESP32_DEVKITC_V4_WIRING.md](/Users/Josh/Documents/PlatformIO/Projects/ApexPyro/docs/ESP32_DEVKITC_V4_WIRING.md) with the final 128-zone wiring, I2C bus split, and ADS1115 address map.
- [ ] Update any other docs or operator notes that still describe continuity as a 48-zone system.
- [ ] Keep the roadmap file itself as the current implementation checklist until the refactor is complete.

## Validation Checklist

- [ ] Build firmware successfully after each major firmware edit.
- [ ] Confirm boot logs show relay boards on primary I2C only and continuity ADCs on auxWire.
- [ ] Confirm live continuity status updates for zones across all 8 mux lanes.
- [ ] Confirm relay start/complete serial logs still appear while continuity scanning is active.
- [ ] Confirm the live browser page reflects the 128-zone continuity architecture after UI/doc changes.

## Notes

- Keep zone indexing 0-based internally and 1-based in operator-facing text.
- Prefer graceful degradation for a missing ADC block over disabling the entire continuity subsystem.
