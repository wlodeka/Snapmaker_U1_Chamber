# Version provenance

## v1.0.1

The owner validated the firmware and WebGUI on a physical Snapmaker U1 after a Wi-Fi OTA update with the rc.2 application image. The tested image has SHA-256 `233439d71dfb2377aab04aee4825e832333ce1c354347baaa968f630a6355df5`. The [stable release](https://github.com/wlodeka/Snapmaker_U1_Chamber/releases/tag/v1.0.1) uses the same firmware source, with the source commit and checksum for the rebuilt OTA asset recorded in the release attachments.

## v1.0.0

The owner tested and accepted the operating behavior included in stable v1.0.0 on a physical Snapmaker U1 and its ESP32-S3-Zero controller. The tested version adds 60% TopCover circulation during MANUAL heating, hands off to U1 at PRINTING without writing new targets and checks the current bed target before manual shutdown.

## Packaging verification for v1.0.0

- The [Arduino sketch](https://github.com/wlodeka/Snapmaker_U1_Chamber/blob/v1.0.0/firmware/Snapmaker_U1_Chamber/Snapmaker_U1_Chamber.ino), [forward declarations](https://github.com/wlodeka/Snapmaker_U1_Chamber/blob/v1.0.0/firmware/Snapmaker_U1_Chamber/chamber_forward_types.h) and [WebGUI](https://github.com/wlodeka/Snapmaker_U1_Chamber/blob/v1.0.0/firmware/Snapmaker_U1_Chamber/chamber_web_ui.h) form the source package.
- Packaging edits to the firmware were confined to comments. The shipped WebGUI matches the version that was tested.
- The OTA application distributed in [v1.0.0](https://github.com/wlodeka/Snapmaker_U1_Chamber/releases/tag/v1.0.0) was compiled with the documented Arduino-ESP32 3.3.12 profile. Compiler path normalization removes local account paths from Arduino core diagnostic strings. The packaging image was verified offline against the hardware-tested build.
- The [firmware release record](FIRMWARE_RELEASES.md#first-stable-release) identifies the packaged application and links to the original source checksums.

## Hardware and licensing

The [schematic](schematics/README.md) contains the owner's replacement exports in PDF, SVG and PNG. All three use the firmware GPIO assignments. The [BOM](../BOM/README.md) distinguishes documented component values from details that still need confirmation on the assembly. [Project licenses](../LICENSE.md) · [Third-party notices](../THIRD_PARTY_NOTICES.md)
