# MP24 engineering sample — evidence and adapter

Identified on 2026-10-14. The owner confirms MakerPhone 2.0 engineering hardware; no vendor SDK/schematic was supplied. This is not a production-board compatibility claim.

| Component | Observed configuration | Evidence |
|---|---|---|
| MCU | ESP32-S3 rev 0.2, embedded 4 MB flash, 2 MB quad PSRAM | Espressif ROM identification and factory boot log |
| Original app | `makerphone_mp24`, build Sep 16 2026, ESP-IDF v5.5.4, UI identifies v2.6 | ESP app descriptor and factory boot log |
| Display | ST7735, 160×128 landscape, 26 MHz; SPI MOSI 5, SCK 4, DC 6, reset 7, no SPI CS | Display boot log and its SPI/GPIO configuration in the saved app |
| Main I²C | SDA 47 / SCL 48, 100 kHz | Factory boot log |
| Keypad | XL9555 at 0x20 and 0x21, interrupt GPIO 9/10 | Factory boot log and 23-entry input mapping table |
| Backlight | AW9523B at 0x5B, P1_0 active low | Factory GPIO configuration and backlight routine |
| Audio amp | XL9555 U9 P1_1 gate | Factory boot log; kept muted in initial custom build |
| I²S speaker/mic | BCLK 39, WS 40, DOUT 38, DIN 41 | Factory boot log; audio adapter not yet implemented |
| SD SPI | CS 34, MOSI 35, SCK 36, MISO 37 | Factory boot log; 3840 MB mounted there |
| Modem | Quectel EG91-NAX, North American variant | Factory AT identification log |
| Modem UART | UART1 115200 8N1, TX 17, RX 18, no CTS/RTS | UART configuration in saved app |
| Modem control | GPIO12 power-key inverting driver (2.5 s HIGH pulse); GPIO16 reset inverting driver (LOW released); GPIO11 sleep/wake | Factory control routine and diagnostic text |
| Power button | GPIO2, active HIGH, external divider, no internal pulls | Factory boot log |

These are recovered hardware assignments, not copied vendor application routines. A new adapter implements them using public peripheral APIs. The full engineering firmware and disassembly remain private and are not redistributed or assigned an open-source license.

Key mapping: digit 0–9, `*`, `#`, A/B/C/D, joystick up/down/left/right/click, volume up/down. The native scan service debounces 25 ms while polling at 100 Hz and buffers simultaneous new presses. No guesses from original Ringo are used.

The factory diagnostics also reported modem supply readings above its configured 4.3 V warning threshold while USB-powered. This is an unverified ADC report, not a meter measurement; electrical power and battery calibration need validation before a daily-use release.

The original firmware successfully mounts the installed SD card despite its refusal of optional SPI command CRC control (CMD59). The replacement Arduino 3.3.12 SD driver also mounted this card and passed a dedicated write/read/compare test. The test removed only its own new file. No formatting was performed.

## Recovery

The private `.work/recovery/backup-info.json` records the complete image length and SHA-256. The saved image includes the bootloader, partition table, original app, NVS, SPIFFS and coredump partition, but does not include the removable SD card. Keep it local. Restore only to this matching sample if needed, preferably restoring only its original application partition when other partitions were not modified.
