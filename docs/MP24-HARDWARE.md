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
| Audio amp | XL9555 U9 P1_1 active-high gate | Factory boot log; output confirmed through headphones in 0.5 |
| I²S speaker/mic | BCLK 39, WS 40, DOUT 38, DIN 41 | Factory boot log; output uses I2S1, stereo 32-bit slots; microphone samples measured and two-way call audio confirmed in 0.6 |
| Eight LEDs | AW9523B P0_2..6 and P1_5..7, dim registers 0x26–0x2A, 0x2D–0x2F | Eight-entry factory hardware table; all eight physically confirmed in 0.5 |
| SD SPI | CS 34, MOSI 35, SCK 36, MISO 37 | Factory boot log; 3840 MB mounted there |
| Modem | Quectel EG91-NAX, North American variant | Factory AT identification log |
| Modem UART | UART1 115200 8N1, TX 17, RX 18, no CTS/RTS | UART configuration in saved app |
| Modem control | GPIO12 power-key inverting driver (2.5 s HIGH pulse); GPIO16 reset inverting driver (LOW released); GPIO11 sleep/wake | Factory control routine and diagnostic text |
| Modem PCM | BCLK 14, frame sync 21, modem-to-host 13, host-to-modem 33 | Factory configuration table and diagnostic arguments; modem initially selected PCM slave, 8 kHz, 16-bit, 2.048 MHz BCLK; see the clock investigation below |
| Power button | GPIO2, active HIGH, external divider, no internal pulls | Factory boot log |
| Power latch | XL9555 U9 P1_0: input/floating normally, driven LOW to shut down | Factory power routine/diagnostics; physical battery-only shutdown acceptance pending |
| IR expansion module | RX GPIO42 with pull-up; TX GPIO45 inverted, idle HIGH; factory carrier 38 kHz | Factory module identification (`0xff13`) and its receiver/LEDC initialization |

These are recovered hardware assignments, not copied vendor application routines. A new adapter implements them using public peripheral APIs. The full engineering firmware and disassembly remain private and are not redistributed or assigned an open-source license.

The ESP32-S3 I2S driver limits a frame to 128 bits; the modem's original
2.048 MHz clock at 8 kHz needs 256 bits. Lowering the modem clock to 256 kHz
produced a local tone but prevented subsequent calls from reaching the peer.
The 1.024 MHz trial also failed call setup. Restoring the original 2.048 MHz
setting restored an active call, with silence confirmed by the owner. A modem
clock-master trial did not provide reliable transfers on this board.

The working 0.6 bridge instead uses eight 16-bit slots at 16 kHz,
keeping 2.048 MHz BCLK within a 128-bit frame. The modem accepted
`AT+QDAI=1,1,0,4,0,1,1,1`. PCM BCLK is inverted to match EG91 sampling on
falling edges and changing data on rising edges; I2S1 runs speaker/mic at 48 kHz.
Local tone and live-call transfers had zero errors. The owner confirmed speech
in both directions on the 16 kHz call. This remains sample-specific evidence.

`VOICE TEST` is a bounded three-second local bridge check and places no call.
`VOICE STATUS` reports transfer counts, input peaks and an initialization error
stage (1-5 PCM, 6-10 speaker/mic, 11 worker allocation).

No vibration-motor pin or driver has been established for this sample. Buzzing
reported only during tone playback does not independently establish a motor.
LED/backlight/amp control preserves unrelated expander pins. Audio, SD and radio
workers run without holding the main I²C bus; amp requests are applied by the UI
loop. The loop stack is 16 KiB and the MP3 worker stack/buffers use PSRAM.

The IR adapter uses ESP-IDF RMT capture/transmission with hardware carrier removal for the raw photodiode. Its receive polarity and optical performance still require testing with a physical remote. The selected carrier is an assumption for learning, not a frequency measurement. No module-slot power or identification-expander registers are guessed or written. Startup drives the identified inverted emitter HIGH (off); RMT resources are initialized only when recording or transmitting.

Key mapping: digit 0–9, `*`, `#`, A/B/C/D, joystick up/down/left/right/click, volume up/down. The native scan service debounces 25 ms while polling at 100 Hz and buffers simultaneous new presses. No guesses from original Ringo are used.

The factory diagnostics also reported modem supply readings above its configured 4.3 V warning threshold while USB-powered. This is an unverified ADC report, not a meter measurement; electrical power and battery calibration need validation before a daily-use release.

The original firmware successfully mounts the installed SD card despite its refusal of optional SPI command CRC control (CMD59). The replacement Arduino 3.3.12 SD driver also mounted this card and passed a dedicated write/read/compare test. The test removed only its own new file. No formatting was performed.

## Recovery

The private `.work/recovery/backup-info.json` records the complete image length and SHA-256. The saved image includes the bootloader, partition table, original app, NVS, SPIFFS and coredump partition, but does not include the removable SD card. Keep it local. Restore only to this matching sample if needed, preferably restoring only its original application partition when other partitions were not modified.
