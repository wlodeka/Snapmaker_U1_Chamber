# Bill of materials

This list combines the owner's component details with the [circuit schematic](../docs/schematics/README.md). Quantities marked “on the drawing” still need to be confirmed against the working assembly.

| Part or function | Recorded specification | Recorded quantity | To confirm on the assembly |
| --- | --- | --- | --- |
| Controller | Waveshare ESP32-S3-Zero, 4 MB flash in the existing list. Heater GPIO5, fan GPIO6, NTC GPIO7, buzzer GPIO8. | 1 | Exact fitted board revision |
| Optoisolator | PC817, 4-pin | 2 | Manufacturer and exact variant |
| Temperature sensor | 100 kΩ NTC at 25 °C, β = 3950 K in the sketch, 1% in the existing list | 1 | Exact sensor model, tolerance and beta rating |
| Divider resistor (R8) | 100 kΩ, 0.125 W, 1% in the existing list | 1 | Fitted resistor rating and tolerance |
| Series resistors (R1–R7) | 330 Ω, confirmed by the owner. The previous list records a 0.125 W rating. | 7 | Fitted power rating |
| Capacitors (C1–C3) | C1 is 470 µF, C2 is 100 nF and C3 is 2.2 nF, each labeled 35 V | 1 each on the drawing | Fitted quantity, type, capacitance and voltage rating |
| Heater and fan switching | SSR-D3805HK-E solid-state relay. The schematic labels the relays SSR-D3805HK. | 2 SSRs on the drawing | Fitted relay ratings and number of drivers per load |
| PTC heaters | 230 V AC, 150 W each | 2 on the drawing | Fitted quantity and exact part number |
| Blower fans | 7530, 230 V AC, 0.11 A each | 2 on the drawing | Fitted quantity and exact part number |
| Power supply | Hi-Link HLK-5LD05, 230 V AC supply, 5 V DC output at 1 A | 1 on the drawing | Fitted quantity |
| Buzzer | Switched output in the sketch | 1 | Buzzer type, voltage and driver |
| LED panel | The schematic shows blue, red and green LEDs | 1 panel on the drawing | Actual LED count and part numbers |
| Thermal protector | KSD9700, normally closed (NC), 105 °C | 2 on the drawing | Fitted quantity and locations |
| Fuse holders | Fuse holders for the two listed fuses | 2 | Holder type and rating |
| Fuse | 3 A / 250 V | 1 | Fuse type and size |
| Fuse | 1 A / 250 V | 1 | Fuse type and size |
| Power switch | 10 A / 250 V | 1 | Switch model |
| Mains inlet | 6 A / 250 V | 1 | Connector type |
| Power cord | 1.5 m | 1 | Cord type |
| Enclosure and wiring | Assembly-specific | To confirm | Quantities, connectors, wire gauges and mounts |

TopCover compatibility is based on the printer reporting `purifier.power_detected`; the controller does not detect cover hardware directly.

The [hardware GPIO table](../docs/HARDWARE.md) records the firmware pin assignments. The [mechanical model index](../docs/mechanical/README.md) lists the printed components with links to the STL and STEP files. Fitted quantities and ratings marked for confirmation should be checked against the completed schematic and the assembly.
