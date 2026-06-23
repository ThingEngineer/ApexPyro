# ESP32 DevKitC V4 Wiring (ApexPyro)

This document defines the expected hardware wiring for ApexPyro on ESP32 DevKitC V4.

## Safety Notes

- Master arm output must default OFF on boot.
- Firing relay I2C bus is safety-critical and should not be repurposed.
- Auxiliary controls are on a separate I2C bus to avoid disturbing firing bus timing.

## GPIO Assignment Table

| Function                                 | GPIO | Direction         | Notes                     |
| ---------------------------------------- | ---: | ----------------- | ------------------------- |
| Primary I2C SDA (firing PW535 + ADS1115) |   21 | bidirectional     | Keep stable; 100 kHz      |
| Primary I2C SCL (firing PW535 + ADS1115) |   22 | output/open-drain | Keep stable; 100 kHz      |
| Auxiliary I2C SDA (aux PW535)            |   26 | bidirectional     | Dedicated auxiliary bus   |
| Auxiliary I2C SCL (aux PW535)            |   27 | output/open-drain | Dedicated auxiliary bus   |
| MUX S0                                   |   16 | output            | Continuity channel select |
| MUX S1                                   |   17 | output            | Continuity channel select |
| MUX S2                                   |   18 | output            | Continuity channel select |
| MUX S3                                   |   19 | output            | Continuity channel select |
| Master Arm Relay                         |   25 | output            | Active HIGH, boot LOW     |
| Kill Switch                              |   34 | input             | Active HIGH, debounced    |

## I2C Devices

### Primary Bus (Wire, GPIO21/GPIO22)

- ADS1115 at 0x48
- PW535 firing relay boards at 0x20 to 0x27

### Auxiliary Bus (Wire1, GPIO26/GPIO27)

- PW535 auxiliary controls board at 0x20
- Active auxiliary channels: A0 to A3 (Aux 1 to Aux 4)

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

## Continuity Hardware Scope

- Current continuity scan hardware covers zones 1 to 48 only (3 mux channels x 16 positions).
- Firing capacity supports up to 128 zones.
- Zones 49 to 128 should be treated as no continuity monitor until hardware is expanded.
