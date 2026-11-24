# Operation and Snapmaker U1 integration

## Local heater

The local NTC is the thermostat feedback sensor. The printer cavity temperature is additional telemetry, not a replacement for that sensor. START arms the local controller; it does not bypass its fault checks. The sequence is idle, fan lead-in, heating with hysteresis, then fan cooldown. Default lead-in is 20 seconds and cooldown is 30 seconds; settings are editable. The heater has a minimum OFF interval of 10 seconds. STOP disarms heating and permits cooldown.

## MANUAL and PREHEAT

MANUAL controls the local thermostat and can set TopCover circulation to 60%. Circulation requires active local heating, an online U1, a detected TopCover and confirmation that the printer is idle. The local heater can cycle independently of TopCover. At the end of a heating cycle or on STOP, the controller requests zero speed for the circulation fan it took over, subject to the same idle and TopCover checks.

PREHEAT adds BED ASSIST. The firmware requests 30% `cavity_fan` and a 100 °C bed target. With a detected TopCover, it requests 60% circulation while local heating is active and zero when that condition ends. Selecting PREHEAT can start BED ASSIST on its own. Use START to arm the local thermostat. BED ASSIST can renew the bed target after 240 seconds without reissuing the fan settings.

PREHEAT READY is triggered at target minus half the configured hysteresis and requests three short buzzer pulses. READY indicates local chamber temperature. It leaves heating and assistance running.

When PRINTING is detected, pending U1 commands and renewals are canceled. Control of the bed and fans passes to the printer without additional G-code. The controller returns to MANUAL and an armed local thermostat can keep regulating.

STOP in PREHEAT, or switching to MANUAL, ends active or pending BED ASSIST. Only an idle U1 can receive the release commands. The controller can turn off the bed if U1 previously acknowledged its 100 °C command and a fresh query reports an idle printer with a bed target still at 100 °C. Other bed targets are left in place. A switch to MANUAL preserves the current thermostat arming state.

After this controller has observed a PRINTING-to-terminal-state transition, it stops local heating and clears PREHEAT. Standby before a print does not by itself cancel a manual warm-up.

Every U1 write requires a fresh `standby`, `complete` or `cancelled` response. Paused, error and unknown states block writes. A failed state check leaves the latest valid request eligible for retry after 5 seconds. A confirmed PRINTING state cancels pending requests, including an earlier STOP. The state check and write are separate HTTP requests. U1 can change state after the check, and a request already sent cannot be recalled.

## Moonraker interface

The polling interval is approximately **5 seconds**. Request execution and queued work can delay a poll. On applying a snapshot, the controller invalidates cached temperatures older than **30 seconds**. An invalid snapshot or more than **45 seconds** since the last valid response causes OFFLINE. The following objects are queried:

- `temperature_sensor cavity`: `temperature`
- `print_stats`: `state`
- `heater_bed`: `temperature`, `target`, `power`
- `purifier`: `power_detected` (TopCover detection)

Commands use `/printer/gcode/script` with `SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=...` and `SET_FAN_SPEED FAN=... SPEED=...` for `cavity_fan` and `circulation_fan`.

The controller does not use slicer changes or M141/M191 chamber macros for this workflow. Printer firmware must provide the objects and named fans above. A missing TopCover field is not interpreted as a connected cover. Loss of the U1 link causes an active local heating cycle to stop and cool down; this is not an offline standalone heater mode.

## Virtual U1 diagnostics

Virtual U1 supports standby with or without TopCover, printing, completion and cancellation. It defaults OFF after restart. Switching between simulated and real U1 discards pending commands and ownership. A real request already in flight may finish. Local heater, fan, temperature sensing and buzzer remain active hardware during simulation. Use it with a supervised test setup.

## Configuration and history

Wi-Fi, printer address, OTA password and thermostat settings are stored in NVS. Last-fault diagnostics can survive restart. Browser history and current status are available through the UI (`/temp.json`, `/history.json`). A reset does not automatically resume heating.
