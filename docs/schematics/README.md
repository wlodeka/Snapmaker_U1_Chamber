# Circuit schematic

The three exports show the ESP32-S3-Zero schematic supplied by the project owner. [PDF](chamber_1esp32_schem.pdf) · [SVG](chamber_1esp32_schem.svg) · [PNG](chamber_1esp32_schem.png)

| Function | GPIO | Schematic connection |
| --- | --- | --- |
| Heater | GPIO5 | R2, U2 and IC2 |
| Fan | GPIO6 | R1, U1 and IC1 |
| NTC | GPIO7 | R8 divider and J12 |
| Buzzer | GPIO8 | Buzzer output |

R1–R7 are marked 330 Ω. The drawing includes **230 V AC circuitry**. Verify fitted component ratings and the physical assembly before wiring or energizing it. [Hardware details](../HARDWARE.md) · [BOM](../../BOM/README.md) · [Safety](../SAFETY.md)

Original schematic by Wlodeka (wlodeka), drawn with Fritzing. [License and attribution](../../LICENSE.md)
