<p align="center">
  <img src="data/apexpyro-logo.webp" alt="ApexPyro logo" width="320">
</p>

# ApexPyro

ApexPyro is an ESP32-based fireworks controller with firmware, a browser-based control interface, continuity monitoring, and support for up to 128 firing zones. The project is intended for builders, operators, and contributors who need a documented starting point for assembling, flashing, validating, and extending the controller.

## Safety Notice

This project controls pyrotechnic hardware and must be treated as safety-critical.

- Fireworks, igniters, and firing circuits can cause severe injury, death, fire, and property damage.
- ApexPyro is provided as-is, without warranty of any kind.
- You assume all responsibility and liability for compiling, flashing, wiring, testing, and operating this system.
- Use only in legal, controlled environments and follow the regulations that apply in your jurisdiction.
- This repository does not provide pre-compiled binaries; anyone deploying the system is responsible for validating the exact build they run on physical hardware.

## What ApexPyro Includes

- ESP32 firmware built with PlatformIO and the Arduino framework.
- A web UI served from LittleFS for controller setup, show building, and live operation.
- A separated dual-I2C design: one bus for firing relay boards and one bus for continuity and auxiliary control hardware.
- Continuity monitoring for up to 128 zones.
- Safety-oriented controls including master arm, E-Stop handling, role-based control, and configurable operational safeguards.

## Hardware Overview

The current firmware and docs assume an ESP32 DevKitC V4-compatible controller with these major subsystems:

- 1 ESP32 DevKitC V4-compatible board
- Up to 8 PW535 relay boards on the primary firing bus
- 1 auxiliary PW535 relay board for non-firing control outputs
- 3 ADS1115 devices on the auxiliary bus for continuity and battery measurement
- CD74HC4067 multiplexers for zone continuity scanning
- Master arm relay, physical kill switch, and WiFi reset button

For exact GPIO assignments and bus layout, use the wiring guide: [ESP32 DevKitC V4 Wiring](docs/ESP32_DEVKITC_V4_WIRING.md).

## Repository Layout

- `src/`: firmware modules such as WiFi, relay management, continuity, storage, WebSocket handling, and show execution
- `include/`: shared firmware headers and hardware configuration constants
- `data/`: web UI assets served from LittleFS
- `docs/`: user and hardware reference documentation
- `hardware/`: reserved for hardware-specific documentation and future design files

## Getting Started

### Prerequisites

- PlatformIO CLI or PlatformIO in VS Code
- An ESP32 DevKitC V4-compatible target connected over USB
- A validated wiring setup before any live hardware test

### Build Firmware

```bash
pio run
```

### Upload Firmware

```bash
pio run --target upload --environment az-delivery-devkit-v4
```

### Upload Web Assets

```bash
pio run --target uploadfs --environment az-delivery-devkit-v4
```

### Monitor Serial Output

```bash
pio device monitor
```

## Documentation

- [ApexPyro Web User Manual](docs/APEXPYRO_WEB_USER_MANUAL.md)
- [ESP32 DevKitC V4 Wiring](docs/ESP32_DEVKITC_V4_WIRING.md)
- [128-Zone Continuity Roadmap](docs/CONTINUITY_128_ROADMAP.md)
- [Hardware Notes](hardware/README)

## Roadmap

The project roadmap tracks planned product and UX improvements for upcoming iterations. See [ROADMAP](ROADMAP) for the current list.

## Firmware and UI Architecture

The project is organized around a few major runtime components:

- `src/main.cpp`: boot sequence, safe initialization order, subsystem wiring
- `src/wifi_manager.cpp`: AP/STA networking and credential management
- `src/websocket_handler.cpp`: browser-to-firmware command and state sync
- `src/relay_manager.cpp`: master arm state, relay updates, firing control
- `src/continuity.cpp`: continuity scanning and ADC sampling
- `src/show_runner.cpp`: manual and automatic show sequencing
- `src/storage.cpp`: persisted settings and saved state
- `data/index.html` and `data/style.css`: operator-facing web UI

## Contributing

See the dedicated contributor guide: [CONTRIBUTING](CONTRIBUTING).

## Licensing

This project is dual-licensed to separate software and hardware artifacts.

- Software in this repository is licensed under the [Apache License 2.0](LICENSE).
- Hardware design files in `hardware/` are intended to use the [CERN Open Hardware License Version 2 - Weakly Reciprocal](hardware/LICENSE-HARDWARE).

### Liability Disclaimer

Because this project controls pyrotechnic hardware, the authors and contributors disclaim liability for injury, death, fire, property damage, or regulatory violations arising from the use, modification, compilation, flashing, wiring, or operation of this project.
