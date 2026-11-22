# Circuit schematic

[Chamber wiring drawing (PNG)](chamber_1esp32_schem.png)

The drawing is a draft for comparison with the working assembly. It includes 230 V AC circuitry and has not been verified for wiring or component selection.

- The drawing is captioned ESP32 S3 ZERO but uses an ESP32-C3 Zero symbol. Its NTC and buzzer labels are GP6 and GP7, respectively. The checked-in sketch assigns the NTC input to GPIO7 and the buzzer to GPIO8.
- The drawing lists R1–R7 as 330 Ω. The previous parts list records 300 Ω. Confirm the fitted values.

See [firmware GPIO assignments](../HARDWARE.md) and the [preliminary parts list](../../BOM/README.md).
