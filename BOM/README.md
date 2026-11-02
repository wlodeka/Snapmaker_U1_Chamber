# Bill of materials

This is a functional checklist, not a purchase-ready or electrically validated BOM. Add exact part numbers, quantities, ratings, supplier links and the final circuit after checking the owner's working assembly.

| Function | Specification known from firmware | pc. | comments |
| --- | --- | --- | --- |
| Controller | ESP32-S3 Zero Waveshare 4 MB flash | 1 | Exact board/revision |
| Optoisolator | PC817 | 2 | 4pin |
| Temperature sensor | NTC 100 kOhm | 1 |tolerance 1% |
| Divider resistor | 100 kOhm | 1 | 0,125W /tolerance 1% |
| Resistor | 300 Om | 7 | 0,125W |
| Heater switch/driver | Active-HIGH GPIO5 command | 2 |Isolation, ratings, schematic |
| Local fan driver | Active-HIGH GPIO6 command | 2 | Fan voltage/current and driver |
| Buzzer driver | GPIO8 switched output | 1 | Type and electrical interface |
| Heater and power supply | Not specified by firmware | 2 | Voltage, power, protection |
| Thermal cutoff/fuses | Independent of firmware | 2 | Selected ratings/locations |
| Enclosure and wiring | Application-specific | 2 |Connectors, wire gauges, mounts |

TopCover compatibility is based on the printer reporting `purifier.power_detected`; the controller does not detect cover hardware directly.
