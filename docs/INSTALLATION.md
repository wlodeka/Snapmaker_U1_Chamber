# Build, installation and OTA

## Arduino build

Open the [firmware sketch](../firmware/Snapmaker_U1_Chamber/Snapmaker_U1_Chamber.ino) in Arduino IDE. Keep [chamber_forward_types.h](../firmware/Snapmaker_U1_Chamber/chamber_forward_types.h), [chamber_web_ui.h](../firmware/Snapmaker_U1_Chamber/chamber_web_ui.h) and [chamber_virtual_u1.h](../firmware/Snapmaker_U1_Chamber/chamber_virtual_u1.h) in the same directory. Arduino combines sketches in this directory, so keep archived versions elsewhere.

Build configuration:

- Espressif Arduino-ESP32 core **3.3.12**.
- Board: **ESP32S3 Dev Module** (`esp32:esp32:esp32s3`).
- USB CDC on boot: enabled.
- Flash: **4 MB**, flash mode **QIO**.
- Partition scheme: **Minimal SPIFFS** (`min_spiffs`, with OTA slots).
- Remaining options: core defaults. No PSRAM-dependent application allocations are required by this sketch.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=min_spiffs,FlashMode=qio --build-path .build firmware/Snapmaker_U1_Chamber
```

All included libraries come with the ESP32 Arduino core. The repository does not require a separate LVGL, display driver or external Arduino library. Match the physical module and flash capacity before flashing; the board profile alone does not identify the exact module or power circuit.

## First USB installation

Use Arduino IDE’s normal USB upload with the Arduino build settings above. The USB upload installs the application and its partition table. A board with a different existing partition table must first receive the matching USB installation; application OTA cannot replace its partition table. Select the board's actual USB port. Serial diagnostics use 115200 baud.

## Setup and connection

On an unconfigured device the setup AP is `U1-Chamber-Setup`, with password `chamberu1`. Open the AP IP printed on Serial (normally `192.168.4.1`). Enter Wi-Fi SSID/password and the U1 IPv4 address. Configuration is saved in NVS. The AP closes once station Wi-Fi connects; the recovery portal is available when connection setup fails.

U1 must expose the required Moonraker objects and G-code commands on TCP port 7125. This firmware does not install Moonraker or change U1's firmware.

## OTA update

1. Stop heating and plan a supervised update.
2. Set an update password of at least eight characters in the connection/settings area. Changing an existing password requires the current password.
3. Open **FIRMWARE UPDATE (Wi-Fi)**, or `/update`.
4. Username: **`chamber`**. Password: the one saved on your controller.
5. Download `Snapmaker_U1_Chamber.ino.bin` and `SHA256SUMS.txt` from the same [GitHub Release](https://github.com/wlodeka/Snapmaker_U1_Chamber/releases). [Verify the SHA-256](FIRMWARE_RELEASES.md#download-and-verify-the-ota-image) before uploading the application image.

Never upload a bootloader, partition-table, merged flash image or NVS dump through this page. The safety task must acknowledge the local heater OFF before a flash write starts. The local fan remains on during upload. Successful upload restarts the controller; heating requires a new explicit START. A failed upload can leave the safety lockout active until restart.

Application updates normally preserve NVS settings. The application image is built from source.

## Recovering a forgotten OTA password

A configured controller requires the current OTA password for password changes and network factory reset. If it is lost, a qualified operator can recover the **Chamber ESP32-S3 controller** over its physical USB port. Disconnect mains power to the heater before service.

1. Record the Wi-Fi, U1 address and thermostat settings that will need to be restored. A full erase clears the application, saved credentials and settings, and last-fault diagnostics.
2. Open the desired version of the Chamber sketch in Arduino IDE and select the [documented ESP32-S3 profile](#arduino-build) and the controller’s USB port.
3. Enable **Tools → Erase All Flash Before Sketch Upload** for this upload and use **Upload** over USB. Set the erase option back to **Disabled** afterwards.
4. Reconfigure the controller through its setup AP. Enter the Wi-Fi and U1 settings and set a new OTA password.

[Espressif Arduino IDE erase option](https://docs.espressif.com/projects/arduino-esp32/en/latest/guides/tools_menu.html#erase-all-flash-before-sketch-upload)
