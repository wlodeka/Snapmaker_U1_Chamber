# Safety behavior and limits

This document describes the existing firmware, not a safety certification or a guarantee against hardware failure. Hardware drawings and exact component ratings still require owner review before public release.

## Implemented software behavior

- Boot initializes local heater, fan and buzzer outputs LOW. Normal heating needs an explicit START.
- A dedicated heater-safety task owns the relay and local fan. Blocking Moonraker requests run in another task. Shared control state uses a mutex.
- Failure to create the safety task/mutex blocks heating. Failure to register its watchdog blocks heating. The watchdog can reset a stalled controller; it is not an independent thermal cutoff.
- NTC measurements are sampled every 750 ms. Invalid measurements latch a fault.
- User target is capped at **60 °C** and cannot exceed the configured maximum. The configurable maximum cannot exceed **65 °C**. NTC temperature at or above **65 °C** latches overtemperature.
- A latched fault forces the local relay OFF, fan ON and alarm active. Reset requires a valid NTC, temperature below the configured maximum, a live U1 link and relay OFF. Fault reset does not automatically START heating.
- Missing thermal response is checked during continuous actual relay-ON operation at least 5 °C below target. A net rise below 0.5 °C over 10 minutes latches a fault. Relay-OFF ends that observation interval. This is not comprehensive detection of every possible thermal runaway condition.
- Local active heating is stopped on U1-link loss, with cooldown.
- OTA and browser-triggered restart request acknowledgement from the safety task before proceeding with local heater shutdown.

## Hardware requirements and limitations

GPIOs are logic signals, not power supplies for a heater, fan, relay coil or buzzer load. Use correctly rated driver stages and verify HIGH/LOW polarity. Provide an independent thermal cutoff, appropriate overcurrent protection and wiring suitable for the real heater and supply. A welded relay or incorrectly installed NTC cannot be made safe solely by setting a GPIO LOW. Confirm sensor placement and response against an independent thermometer.

The printer bed's 100 °C PREHEAT target is distinct from the local chamber target. Printer protections remain essential. A LAN outage or controller power loss cannot guarantee delivery of an OFF command to a remote printer. The software's last-fault record is diagnostic information, not proof of a persistent physical interlock.

## Network security boundary

The controller uses plain HTTP. OTA upload has Basic authentication with username `chamber` and the owner's saved password, but ordinary control/settings routes, including the OTA-password-setting route, are not authenticated. Therefore OTA authentication does **not** make the entire device safe on an untrusted network. Do not expose it through Internet port forwarding; restrict access to a trusted local network.

The initial AP password `chamberu1` is a shared setup value, not an owner secret. The setup AP is switched off after a successful station connection. NVS contains configured credentials on the physical device; firmware binaries in this repository contain no device NVS backup.
