# Build, installation and OTA

## Arduino build

Open `firmware/Snapmaker_U1_Chamber/Snapmaker_U1_Chamber.ino` in Arduino IDE. Keep `chamber_forward_types.h` in the same directory. Do not copy older `.ino` versions into this sketch directory: Arduino combines all sketches there.

Build configuration used for this review package:

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

For first installation, use Arduino IDE's normal USB upload with these settings. A board with a different existing partition table must first receive the matching USB installation; application OTA cannot replace its partition table. Select the board's actual USB port. Serial diagnostics use 115200 baud.

## Setup and connection

On an unconfigured device the setup AP is `U1-Chamber-Setup`, with password `chamberu1`. Open the AP IP printed on Serial (normally `192.168.4.1`). Enter Wi-Fi SSID/password and the U1 IPv4 address. Configuration is saved in NVS. The AP closes once station Wi-Fi connects; the recovery portal is available when connection setup fails.

U1 must expose the required Moonraker objects and G-code commands on TCP port 7125. This firmware does not install Moonraker or change U1's firmware.

## OTA update

1. Stop heating and plan a supervised update.
2. Set an update password of at least eight characters in the connection/settings area.
3. Open **FIRMWARE UPDATE (Wi-Fi)**, or `/update`.
4. Username: **`chamber`**. Password: the one saved on your controller.
5. Upload **`Snapmaker_U1_Chamber.ino.bin`**, the application image only.

Never upload a bootloader, partition-table, merged flash image or NVS dump through this page. The safety task must acknowledge the local heater OFF before a flash write starts. The local fan remains on during upload. Successful upload restarts the controller; heating requires a new explicit START. A failed upload can leave the safety lockout active until restart.

Application updates normally preserve NVS settings. The distributed application image is built from source; it is not a readback of a configured device.
