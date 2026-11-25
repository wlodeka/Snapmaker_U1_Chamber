# Changelog

## v1.0.1-rc.1

- Send explicit, idempotent START and STOP requests. STOP can cancel BED ASSIST before the local thermostat is armed and preserves print handoff safeguards.
- Recheck heat demand before energizing the relay after PRE-FAN, including after the minimum relay-off delay.
- Require the current OTA password for credential changes and network factory reset when configured. Require explicit reset confirmation and block credential changes when NVS cannot be read. Document physical USB recovery.
- Run focused host regression tests and verify the pinned Task Watchdog configuration and OTA image integrity in CI.
