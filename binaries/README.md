# Firmware binary

[Snapmaker_U1_Chamber.ino.bin](Snapmaker_U1_Chamber.ino.bin) is the application image for the controller's Wi-Fi OTA page. Log in as `chamber` with the password configured on the device.

Do not write this application-only image at flash offset zero. For first installation use Arduino IDE and the source sketch with the configuration documented in [installation](../docs/INSTALLATION.md). OTA requires a compatible existing partition table.

Build verified on 24 November 2026 using Arduino-ESP32 3.3.12, ESP32-S3, 4 MB flash, QIO, Minimal SPIFFS with OTA and USB CDC enabled. Compilation completed with no warnings.

- Program usage: 1,171,449 / 1,966,080 bytes (59%).
- Global variables: 65,776 / 327,680 bytes (20%).
- Application binary: 1,171,600 bytes.

The owner tested the firmware on the physical device before packaging. The packaging build was checked against that image and had neutral names substituted for local compiler paths in diagnostic strings. It was verified offline. The build reads sources and libraries only, so it contains no device NVS settings.

See [SHA256SUMS.txt](SHA256SUMS.txt) for the application and its three source files. Manifest paths are relative to the repository root.

The OTA image links Arduino-ESP32 and ESP-IDF components. [Third-party notices and source references](../THIRD_PARTY_NOTICES.md)
