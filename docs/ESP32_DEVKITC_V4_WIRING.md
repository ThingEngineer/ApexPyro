# ESP32 DevKitC V4 Wiring (ApexPyro)

This document defines the expected hardware wiring for ApexPyro on ESP32 DevKitC V4.

## Safety Notes

- Master arm output must default OFF on boot.
- Firing relay I2C bus is safety-critical and should not be repurposed.
- Auxiliary controls and continuity ADCs are on a separate I2C bus to avoid disturbing firing bus timing.

## GPIO Assignment Table

| Function                                 | GPIO | Direction         | Notes                             |
| ---------------------------------------- | ---: | ----------------- | --------------------------------- |
| Primary I2C SDA (firing PW535 only)      |   21 | bidirectional     | Keep stable; 100 kHz              |
| Primary I2C SCL (firing PW535 only)      |   22 | output/open-drain | Keep stable; 100 kHz              |
| Auxiliary I2C SDA (aux PW535 + ADS1115s) |   26 | bidirectional     | Dedicated auxiliary bus           |
| Auxiliary I2C SCL (aux PW535 + ADS1115s) |   27 | output/open-drain | Dedicated auxiliary bus           |
| MUX S0                                   |   16 | output            | Shared across all 8 muxes         |
| MUX S1                                   |   17 | output            | Shared across all 8 muxes         |
| MUX S2                                   |   18 | output            | Shared across all 8 muxes         |
| MUX S3                                   |   19 | output            | Shared across all 8 muxes         |
| Master Arm Relay                         |   25 | output            | Active HIGH, boot LOW             |
| Kill Switch                              |   34 | input             | Active HIGH, debounced            |
| WiFi Reset Button                        |   32 | input             | Active LOW, INPUT_PULLUP, hold 3s |

## Physical WiFi Reset Button

- Connect a normally-open momentary button between GPIO32 and GND.
- The firmware enables `INPUT_PULLUP` on GPIO32, so no external pull-up resistor is required.
- Hold the button for at least 3 seconds to reset WiFi credentials to defaults:
  - AP SSID: `ApexPyro`
  - AP password: `apexFIRE!pyro`
  - Saved external client network credentials are cleared.

## I2C Devices

### Primary Bus (Wire, GPIO21/GPIO22)

- PW535 firing relay boards at 0x20 to 0x27

### Auxiliary Bus (Wire1, GPIO26/GPIO27)

- PW535 auxiliary controls board at 0x20
- ADS1115 at 0x48: continuity muxes 1-4 (zones 1-64)
- ADS1115 at 0x49: continuity muxes 5-8 (zones 65-128)
- ADS1115 at 0x4A: battery divider on A0, spare channels on A1-A3

## PW535 Address Map (A0/A1/A2)

| Address | A0  | A1  | A2  |
| ------- | --- | --- | --- |
| 0x20    | GND | GND | GND |
| 0x21    | VCC | GND | GND |
| 0x22    | GND | VCC | GND |
| 0x23    | VCC | VCC | GND |
| 0x24    | GND | GND | VCC |
| 0x25    | VCC | GND | VCC |
| 0x26    | GND | VCC | VCC |
| 0x27    | VCC | VCC | VCC |

## ADS1115 Address Map (0x48/0x49/0x4A/0x4B)

| Address | ADDR Pin Connection |
| ------- | ------------------- |
| 0x48    | GND (default)       |
| 0x49    | VDD (VCC)           |
| 0x4A    | SDA                 |
| 0x4B    | SCL                 |

## Continuity Hardware Scope

- Continuity scan hardware covers zones 1 to 128.
- Each mux position reads 8 shared mux outputs across 3 ADS1115 devices on the auxiliary bus.
- The battery divider is isolated to ADS1115 `0x4A` so continuity scan capacity stays dedicated to zones 1 to 128.
