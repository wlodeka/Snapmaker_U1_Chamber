# Bill of materials

This is a functional checklist, not a purchase-ready or electrically validated BOM. Add exact part numbers, quantities, ratings, supplier links and the final circuit after checking the owner's working assembly.

| Function | Specification known from firmware | To be supplied by owner |
| --- | --- | --- |
| Controller | ESP32-S3; review build targets 4 MB flash | Exact board/revision |
| Temperature sensor | NTC 100 kOhm, B3950 | Part, tolerance, mounting |
| Divider resistor | 100 kOhm | Tolerance and circuit details |
| Heater switch/driver | Active-HIGH GPIO5 command | Isolation, ratings, schematic |
| Local fan driver | Active-HIGH GPIO6 command | Fan voltage/current and driver |
| Buzzer driver | GPIO8 switched output | Type and electrical interface |
| Heater and power supply | Not specified by firmware | Voltage, power, protection |
| Thermal cutoff/fuses | Independent of firmware | Selected ratings/locations |
| Enclosure and wiring | Application-specific | Connectors, wire gauges, mounts |

TopCover compatibility is based on the printer reporting `purifier.power_detected`; the controller does not detect cover hardware directly.
