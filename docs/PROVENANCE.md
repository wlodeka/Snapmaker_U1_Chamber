# Version provenance

Review package prepared on **2026-10-31** from the owner's last confirmed working Chamber source:

`esp32s3_chamber_U1_17102026_PREHEAT_WIFI_AP_OFF.ino`

The owner confirmed the correction working on 2026-10-17. This version follows the PREHEAT/UI/TopCover version dated 2026-10-16 and closes the configuration access point after a successful station Wi-Fi connection.

## Packaging changes

- Renamed the sketch to `Snapmaker_U1_Chamber.ino` and placed it inside a matching Arduino sketch directory.
- Included the original required `chamber_forward_types.h` beside it.
- Added English documentation, installation instructions, hardware/BOM placeholders and a Polish owner guide.
- No source content, control logic, pin assignment, credentials handling or UI was changed.

Original and packaged sketch SHA-256:

```text
3bda033b1cd324a98a3185b2089197173c2f37597e8fc7f4fab54d29a62aece7
```

Original header SHA-256:

```text
d1c183bc1e60c6f82d1092bc415a24ddd4cfde6faf143a4a5394167d58f07cb6
```

The owner's hardware testing applies to that original source version. A fresh compilation of this package is separate validation and does not constitute a new physical hardware test. Build details and binary hashes are recorded under `binaries`.

## Before public release

- Add and review actual schematics, component ratings, BOM and photographs.
- Decide on the project license and review third-party binary redistribution notices.
- Review the documented plain-HTTP/local-network security boundary.
- Confirm the exact ESP32-S3 module and installation instructions against the real assembly.
