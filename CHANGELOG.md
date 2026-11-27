# Changelog

## v1.0.1

- Show the last simulated BED ASSIST, CAVITY FAN and TOPCOVER FAN commands separately from active Chamber control. Retain the last values through print handoff.
- Add a bounded RAM simulator log with TX, RX SIM and controller events, a command count and the BED ASSIST renewal countdown.
- Highlight the complete HEATER and FAN tiles when the corresponding ESP32 output is ON.
- Document Virtual U1 and its hardware safety limits with screenshots from the physical controller.
- Include the START/STOP, PRE-FAN and OTA safeguards detailed in v1.0.1-rc.1.
- Validated on a physical Snapmaker U1 after updating via Wi-Fi OTA.

## v1.0.1-rc.1

- Send explicit, idempotent START and STOP requests. STOP can cancel BED ASSIST before the local thermostat is armed and preserves print handoff safeguards.
- Recheck heat demand before energizing the relay after PRE-FAN, including after the minimum relay-off delay.
- Require the current OTA password for credential changes and network factory reset when configured. Require explicit reset confirmation and block credential changes when NVS cannot be read. Document physical USB recovery.
- Run focused host regression tests and verify the pinned Task Watchdog configuration and OTA image integrity in CI.
