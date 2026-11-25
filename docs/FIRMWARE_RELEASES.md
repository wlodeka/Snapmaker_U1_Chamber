# Firmware releases

## Download and verify the OTA image

Open [GitHub Releases](https://github.com/wlodeka/Snapmaker_U1_Chamber/releases) and choose the version you want to install. Download these assets from the same release:

- `Snapmaker_U1_Chamber.ino.bin` — the application image for Wi-Fi OTA.
- `SHA256SUMS.txt` — the checksum for the attached image.
- `BUILD_INFO.txt` — the source commit, build profile and image provenance.

On Linux, run `sha256sum --check SHA256SUMS.txt` from the download directory. On Windows, run the following command in PowerShell and compare the result with the corresponding line in `SHA256SUMS.txt`.

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\Snapmaker_U1_Chamber.ino.bin
```

Upload the verified image using the controller's Wi-Fi OTA page. For first installation or a board that needs a new partition table, use the USB procedure in the [installation instructions](INSTALLATION.md).

## First stable release

[v1.0.0](https://github.com/wlodeka/Snapmaker_U1_Chamber/releases/tag/v1.0.0) contains the previously accepted publication image from 24 November 2026. Its SHA-256 is `870bb215d559d1e3cabb090ee71f46bf9dc66d608d12cbc9ac9cc709b39488e1` and its size is 1,171,600 bytes. The [original package manifest](https://github.com/wlodeka/Snapmaker_U1_Chamber/blob/v1.0.0/binaries/SHA256SUMS.txt) also records the three source file hashes.

The original build used 1,171,449 of 1,966,080 bytes of program flash and 65,776 of 327,680 bytes for global variables. See [version provenance](PROVENANCE.md) for the relationship between the hardware-tested and publication images.

## Control build comparison

The [first Actions control build](https://github.com/wlodeka/Snapmaker_U1_Chamber/actions/runs/45577870503) produced a 1,171,424-byte image with the same ESP32-S3 image header and ESP-IDF v5.5.5 as the accepted binary. The runner's shorter diagnostic paths change the read-only data layout and the addresses referring to it. The compile timestamp, ELF digest and resulting image checksums also differ. Both images pass their internal integrity checks.

The control run used 1,171,273 of 1,966,080 bytes of program flash and 65,776 of 327,680 bytes for global variables. The stable asset remains the previously accepted image identified above.


## Build and publishing workflow

The [firmware workflow](https://github.com/wlodeka/Snapmaker_U1_Chamber/actions/workflows/firmware.yml) runs on firmware changes and can be run manually from GitHub Actions. These runs upload a control-build artifact for inspection. Pushing a version tag matching `v*` triggers the release job after a successful build. The `v1.0.0` tag uses the pinned checksum to select the accepted image. Later tags package the application generated from the tagged source.

The runner uses Arduino CLI **1.5.1** and the Espressif Arduino-ESP32 core **3.3.12**. The core package pins the bundled libraries and tools, including the ESP32-S3 SDK libraries **3.3.12**, Xtensa toolchain **2601** and esptool **5.3.1**. The sketch requires no separately installed Arduino libraries. See the [exact FQBN and options](INSTALLATION.md#arduino-build).
