# Firmware binary

`Snapmaker_U1_Chamber.ino.bin` is the application image for the controller's Wi-Fi OTA page. Username: `chamber`; password: the one configured on the device.

Do not write this application-only image at flash offset zero. For first installation use Arduino IDE and the source sketch with the configuration documented in [installation](../docs/INSTALLATION.md). OTA requires a compatible existing partition table.

Fresh build: 2026-10-31, Arduino-ESP32 3.3.12, ESP32-S3, 4 MB flash, QIO, Minimal SPIFFS with OTA, USB CDC enabled. Compiler exit code: 0.

- Program usage: 1,146,033 / 1,966,080 bytes (58%).
- Global variables: 65,744 / 327,680 bytes (20%).
- Application binary: 1,146,176 bytes.

This build has not been newly flashed or hardware-tested during packaging. The source is byte-identical to the owner's previously confirmed working version, except for the sketch's filename/path. No device memory was read to create this file; private NVS settings are not included. Compiler metadata can contain build paths, which are not credentials.

SHA-256 checksums for the application and its two source files are in `SHA256SUMS.txt`.
