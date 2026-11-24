# Safety behavior and limits

This document describes the firmware safeguards and their limits. The [circuit schematic](schematics/README.md) records the design. Verify fitted component ratings and the physical assembly before use.

## Implemented software behavior

- Boot initializes local heater, fan and buzzer outputs LOW. Normal heating needs an explicit START.
- A dedicated heater-safety task owns the relay and local fan. Blocking Moonraker requests run in another task. Shared control state uses a mutex.
- Failure to create the safety task/mutex blocks heating. Failure to register its watchdog blocks heating. The watchdog can reset a stalled controller; it is not an independent thermal cutoff.
- NTC measurements are sampled at a nominal 750 ms interval. Valid samples require a voltage above 20 mV and below 3280 mV, resistance from 1 kΩ to 1.2 MΩ and a finite temperature from −15 to 120 °C. An invalid reading latches a fault.
- User target is capped at **60 °C** and cannot exceed the configured maximum. The configurable maximum cannot exceed **65 °C**. NTC temperature at or above **65 °C** latches overtemperature.
- A latched fault forces the local relay OFF, fan ON and alarm active. Reset requires a valid NTC, temperature below the configured maximum, a live U1 link and relay OFF. Fault reset does not automatically START heating.
- Missing thermal response is checked during continuous actual relay-ON operation at least 5 °C below target. A net rise below 0.5 °C over 10 minutes latches a fault. Relay-OFF ends that observation interval. This is not comprehensive detection of every possible thermal runaway condition.
- Local pre-fan or active heating stops with cooldown on U1-link loss. The safety task uses an invalid snapshot or more than 45 seconds since the last valid snapshot. A new START is required if the thermostat was disarmed.
- OTA, browser-triggered restart, network configuration and factory reset wait up to 500 ms for the safety task to confirm local heater OFF. An unsuccessful acknowledgement prevents the operation.

## Hardware requirements and limitations

GPIOs are logic signals, not power supplies for a heater, fan, relay coil or buzzer load. Use correctly rated driver stages and verify HIGH/LOW polarity. Provide an independent thermal cutoff, appropriate overcurrent protection and wiring suitable for the real heater and supply. A welded relay or incorrectly installed NTC cannot be made safe solely by setting a GPIO LOW. Confirm sensor placement and response against an independent thermometer.

The printer bed's 100 °C PREHEAT target is distinct from the local chamber target. Printer protections remain essential. A LAN outage or controller power loss cannot guarantee delivery of an OFF command to a remote printer. The software's last-fault record is diagnostic information, not proof of a persistent physical interlock.

An alarm cancels BED ASSIST and can release controlled fans after confirming that U1 is idle. The alarm does not set the bed target to zero. Starting a print cancels all Chamber commands to U1. A manual STOP can zero the bed only after an acknowledged Chamber 100 °C request and a fresh idle response that still reports a target of 100 °C. A second source can set the same temperature, and U1 can change state between the check and the write. Missing TopCover detection can prevent even a circulation-off command.

## Network security boundary

The controller uses plain HTTP. OTA upload has Basic authentication with username `chamber` and the owner's saved password, but ordinary control/settings routes, including the OTA-password-setting route, are not authenticated. Therefore OTA authentication does **not** make the entire device safe on an untrusted network. Do not expose it through Internet port forwarding; restrict access to a trusted local network.

The initial AP password `chamberu1` is a shared setup value, not an owner secret. The setup AP is switched off after a successful station connection. NVS contains configured credentials on the physical device; firmware binaries in this repository contain no device NVS backup.
