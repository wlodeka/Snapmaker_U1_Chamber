# Third-party notices

The sketch uses Arduino-ESP32 and its bundled libraries for networking, HTTP, WebServer, Preferences and OTA updates. The OTA application includes Arduino-ESP32 and linked ESP-IDF code. Copyright remains with Espressif Systems and the upstream authors. Component-specific terms and copyright notices accompany the corresponding upstream source.

| Component | Sources and terms |
| --- | --- |
| Arduino-ESP32 3.3.12 | [Release and source](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12) · [LGPL 2.1 text](LICENSES/LGPL-2.1-or-later.txt) |
| ESP-IDF v5.5.5 | [Versioned source and component notices](https://github.com/espressif/esp-idf/tree/v5.5.5) · [Apache-2.0 text](LICENSES/Apache-2.0.txt) |
| Arduino-ESP32 prebuilt libraries | [Espressif library builder](https://github.com/espressif/esp32-arduino-lib-builder), build revision `6671d0b` |
| libb64 in the Arduino core | [Public-domain dedication supplied with the core](LICENSES/libb64.txt) |

The Arduino profile, application source and version information are in the [installation instructions](docs/INSTALLATION.md) and [binary record](binaries/README.md). Distribution of the linked OTA image is subject to the bundled components' terms, including the corresponding-source and relinking provisions of LGPL section 6. The MIT license covers the original Chamber source.

The circuit schematic was drawn with [Fritzing](https://fritzing.org/). The export retains its Fritzing mark. Snapmaker and other product marks belong to their respective owners.
