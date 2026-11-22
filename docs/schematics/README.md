# Circuit schematic

[Chamber wiring drawing (PNG)](chamber_1esp32_schem.png)

The drawing is a draft for comparison with the working assembly. It includes 230 V AC circuitry and has not been verified for wiring or component selection.

- The project targets ESP32-S3-Zero. The owner used the ESP32-C3 Zero symbol as a placeholder because the ESP32-S3-Zero symbol was unavailable in the drawing software.
- The drawing labels the NTC input GP6 and the buzzer GP7. The checked-in sketch assigns them to GPIO7 and GPIO8, respectively. Confirm the connections on the assembled controller.
- The drawing lists R1–R7 as 330 Ω. The previous parts list records 300 Ω. Confirm the fitted values.

See [firmware GPIO assignments](../HARDWARE.md) and the [preliminary parts list](../../BOM/README.md).
