# Bill of materials

Quantities and ratings below come from the existing parts list or the [draft schematic](../docs/schematics/README.md). Compare them with the working assembly before choosing or ordering parts.

| Part or function | Recorded specification | Recorded quantity | To confirm on the assembly |
| --- | --- | --- | --- |
| Controller | Waveshare ESP32-S3-Zero, 4 MB flash in the existing list | 1 | Exact board revision. The C3 symbol is a placeholder in the drawing. |
| Optoisolator | PC817, 4-pin | 2 | Manufacturer and exact variant |
| Temperature sensor | 100 kΩ NTC at 25 °C, β = 3950 K in the sketch, 1% in the existing list | 1 | Exact sensor model, tolerance and beta rating |
| Divider resistor (R8) | 100 kΩ, 0.125 W, 1% in the existing list | 1 | Fitted resistor rating and tolerance |
| Series resistors (R1–R7) | 330 Ω, confirmed by the owner. The previous list records a 0.125 W rating. | 7 | Fitted power rating |
| Capacitors (C1–C3) | The draft labels C1 as 470 µF, C2 as 100 nF and C3 as 2.2 nF, each 35 V | 1 each on the drawing | Fitted quantity, type, capacitance and voltage rating |
| Heater and fan switching | The draft labels IC1 and IC2 as SSR-D3805HK. The sketch uses active-high outputs for both functions. | 2 SSRs on the drawing | Fitted models, ratings and number of drivers for each load. The earlier list gave 2 per function. |
| Heaters | The draft labels PTC-1 and PTC-2 as 150 W each | 2 on the drawing | Fitted models, voltage and power |
| Local fans | The draft shows FAN-1 and FAN-2 | 2 on the drawing | Fitted models, voltage and current |
| Power supply | The draft labels IC3 as HLK-5LD05 | 1 on the drawing | Actual model and input/output ratings |
| Buzzer | Switched output in the sketch | 1 | Buzzer type, voltage and driver |
| LED panel | The draft shows blue, red and green LEDs | 1 panel on the drawing | Actual LED count and part numbers |
| Thermal protection and fuses | The draft labels two thermostats and two fuses | 2 thermostats and 2 fuses on the drawing | Fitted types, trip temperatures, fuse ratings and locations |
| Enclosure and wiring | Assembly-specific | To confirm | Quantities, connectors, wire gauges and mounts |

TopCover compatibility is based on the printer reporting `purifier.power_detected`; the controller does not detect cover hardware directly.
