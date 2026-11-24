# Hardware and GPIO assignment

The project uses an ESP32-S3-Zero. The pin assignments below match the supplied [circuit schematic](schematics/README.md).

| Function | GPIO | Firmware behavior |
| --- | --- | --- |
| Local heater control | 5 | HIGH requests ON, LOW requests OFF |
| Local fan control | 6 | HIGH requests ON, LOW requests OFF |
| NTC analog input | 7 | 12-bit ADC with attenuation configured in sketch |
| Buzzer control | 8 | HIGH/LOW switched alarm output |

NTC divider assumed by the code:

```text
3.3 V --- fixed 100 kOhm --- GPIO7 --- NTC 100 kOhm --- GND
```

Thermistor model: 100 kOhm at 25 °C, beta 3950. Swapping the fixed resistor and NTC changes the calculation and is not supported by this wiring definition. Never apply 12 V or another load supply voltage to the ADC/GPIO pins.

The sketch uses Wi-Fi rather than a physical control wire to U1. There is no LCD or I2C display dependency. GPIO numbering here is ESP32 numbering, not an unverified connector pin number. Do not reuse the S4 Companion wiring diagram: this is a separate project.

The [BOM](../BOM/README.md) distinguishes recorded parts and ratings from values that need confirmation on the assembly. See the [mechanical model index](mechanical/README.md) for the printable parts and CAD files.
