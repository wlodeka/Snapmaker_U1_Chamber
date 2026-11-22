# Hardware and GPIO assignment

The selected sketch targets ESP32-S3. Exact board model, power electronics, connector labels and mechanical assembly must be documented from the owner's working hardware before others reproduce it.

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

The [schematic notes](schematics/README.md) list differences between the draft drawing and these firmware assignments. The [preliminary BOM](../BOM/README.md) records which parts and values still need confirmation against the working assembly.
