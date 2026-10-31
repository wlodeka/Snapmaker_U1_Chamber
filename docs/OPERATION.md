# Operation and Snapmaker U1 integration

## Local heater

The local NTC is the thermostat feedback sensor. The printer cavity temperature is additional telemetry, not a replacement for that sensor. START arms the local controller; it does not bypass its fault checks. The sequence is idle, fan lead-in, heating with hysteresis, then fan cooldown. Default lead-in is 20 seconds and cooldown is 30 seconds; settings are editable. The heater has a minimum OFF interval of 10 seconds. STOP disarms heating and permits cooldown.

## MANUAL and PREHEAT

MANUAL provides local chamber thermostat control. PREHEAT adds bed assistance: the firmware requests a 100 °C bed target and a 30% cavity fan setting. PREHEAT selection alone does not start the local heater; use START. With a detected TopCover, circulation is requested at 60% while local heating is active. Unknown or absent TopCover suppresses circulation-fan commands.

PREHEAT READY is triggered at target minus half the configured hysteresis and requests three short buzzer signals. On detecting PRINTING, bed assistance relinquishes control without requesting bed=0, and owned fans are released by requesting zero speed. The local chamber thermostat continues. Stopping PREHEAT or switching back to MANUAL has a different purpose: the workflow requests its owned fan/bed shutdown. Observe the printer's actual state when ending a workflow.

After this controller has observed a PRINTING-to-terminal-state transition, it stops local heating and clears PREHEAT. Standby before a print does not by itself cancel a manual warm-up.

Bed=100 commands require a fresh print-state query, in addition to the periodic snapshot. Failed or unknown state prevents that bed request. Command generations invalidate obsolete requests, and synchronization retries retain the newest desired state. These checks are not an atomic transaction with printer firmware: network requests already in flight cannot be recalled.

## Moonraker interface

Normal polling interval is approximately **5 seconds**, plus request/work execution time. Telemetry older than **30 seconds** is treated as stale. The following objects are queried:

- `temperature_sensor cavity`: `temperature`
- `print_stats`: `state`
- `heater_bed`: `temperature`, `target`, `power`
- `purifier`: `power_detected` (TopCover detection)

Commands use `/printer/gcode/script` with `SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=...` and `SET_FAN_SPEED FAN=... SPEED=...` for `cavity_fan` and `circulation_fan`.

The controller does not use slicer changes or M141/M191 chamber macros for this workflow. Printer firmware must provide the objects and named fans above. A missing TopCover field is not interpreted as a connected cover. Loss of the U1 link causes an active local heating cycle to stop and cool down; this is not an offline standalone heater mode.

## Virtual U1 diagnostics

The UI contains an existing virtual-printer test feature with simulated standby, cover and printing states. It defaults OFF after restart. While enabled it substitutes simulated U1 state and suppresses new real printer requests; an already-running request may finish. It is **not a local heater disconnect**: local outputs can still operate. Use it only with a supervised, suitable test setup. Exit it before normal use.

## Configuration and history

Wi-Fi, printer address, OTA password and thermostat settings are stored in NVS. Last-fault diagnostics can survive restart. Browser history and current status are available through the UI (`/temp.json`, `/history.json`). A reset does not automatically resume heating.
