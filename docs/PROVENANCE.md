# Version provenance

The owner tested and accepted the current operating behavior on a physical Snapmaker U1 and its ESP32-S3-Zero controller. The tested version adds 60% TopCover circulation during MANUAL heating, hands off to U1 at PRINTING without writing new targets and checks the current bed target before manual shutdown.

## Packaging verification

- The [Arduino sketch](../firmware/Snapmaker_U1_Chamber/Snapmaker_U1_Chamber.ino), [forward declarations](../firmware/Snapmaker_U1_Chamber/chamber_forward_types.h) and [WebGUI](../firmware/Snapmaker_U1_Chamber/chamber_web_ui.h) form the source package.
- Packaging edits to the firmware were confined to comments. The shipped WebGUI matches the version that was tested.
- The [OTA application](../binaries/Snapmaker_U1_Chamber.ino.bin) was compiled with the documented Arduino-ESP32 3.3.12 profile. Compiler macro path mapping removes local account paths from Arduino core diagnostic strings. The packaging build was verified offline against the hardware-tested build.
- The [binary record](../binaries/README.md) and [SHA-256 manifest](../binaries/SHA256SUMS.txt) identify the packaged application and sources.

## Hardware and licensing

The [schematic](schematics/README.md) contains the owner's replacement exports in PDF, SVG and PNG. All three use the firmware GPIO assignments. The [BOM](../BOM/README.md) distinguishes documented component values from details that still need confirmation on the assembly. [Project licenses](../LICENSE.md) · [Third-party notices](../THIRD_PARTY_NOTICES.md)
