# Snapmaker U1 Chamber

ESP32-S3 chamber heater controller with a browser interface, local NTC temperature control, Snapmaker U1 / Moonraker integration, automatic TopCover detection and password-protected Wi-Fi firmware updates.

**Private owner-review edition.** The firmware is the owner's last confirmed working version from 17 October 2026. This repository reorganizes that version for publication; it does not introduce a new control algorithm. Hardware schematics, photographs and the final BOM will be added by the owner. No open-source license has been selected yet.

## Features

- Local thermostat with adjustable target, hysteresis, fan lead-in and cooldown.
- MANUAL and PREHEAT workflows, bed assistance and printer-state handoff.
- TopCover detection through U1's `purifier.power_detected` field.
- Separate heater-safety and Moonraker tasks; local NTC fault and overtemperature handling.
- Fault diagnostics, history, browser controls and a virtual U1 test mode.
- First-start Wi-Fi setup, saved configuration and OTA updates.

This is an **active controller**: besides switching its local heater and fan, it can send commands to the printer's bed and fans. Read [operation and integration](docs/OPERATION.md) and [safety and limitations](docs/SAFETY.md) before connecting hardware.

## Getting started

1. Check [hardware and pins](docs/HARDWARE.md) against your actual wiring.
2. Follow [build and installation](docs/INSTALLATION.md).
3. On first start, join `U1-Chamber-Setup` (setup password: `chamberu1`) and open the address printed on Serial.
4. Enter your Wi-Fi settings and U1 IP address. The setup access point switches off after the controller connects to Wi-Fi.
5. Open the controller's LAN address, configure the thermostat and set an OTA password.

The firmware has no embedded owner's Wi-Fi password, OTA password or printer address. These are supplied during setup and stored on the controller in NVS.

## Repository contents

| Folder | Contents |
| --- | --- |
| `firmware/Snapmaker_U1_Chamber` | Arduino sketch and required header |
| `binaries` | Build artifacts and identification instructions |
| `docs` | Installation, operation, safety and version provenance |
| `docs/schematics` | Placeholder for owner-supplied circuit drawings |
| `docs/photos` | Placeholder for assembly photographs |
| `docs/screenshots` | Placeholder for interface screenshots |
| `docs/mechanical` | Placeholder for mounts and enclosure files |
| `BOM` | Preliminary functional parts list; exact hardware pending |

See [version provenance](docs/PROVENANCE.md) for the selected source and validation. Snapmaker and SUNLU are trademarks of their respective owners; this is an independent project.
