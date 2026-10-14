# Engineering validation

## Completed on the original device

- ROM identification: ESP32-S3 rev 0.2, 4 MB flash, 2 MB quad PSRAM.
- Full 4,194,304-byte recovery image, checksum recorded privately.
- Original app descriptor, partition table, boot diagnostics and peripheral identities inspected.
- Original firmware mounts the installed 3840 MB card and communicates with the EG91-NAX modem; SIM is absent.
- Recovery drill: original application partition restored, flash hash verified, and original runtime/AT diagnostics observed afterward. Other flash partitions and SD data were not written by that recovery.

## Native code checks

`tools/test_core.py` passes 16 groups covering normal no-SIM operation without power cycling, call state acknowledgments versus actual active calls, incoming call events, UCS-2 SMS PDU structure, bounded UTF-8 parsing, timeout/ambiguous message handling without auto-retry, SIM removal during a send, registration/CSQ status, wraparound timing, malformed UART lines, UART write failure, CSV quoting/formula neutralization, file path validation, PCAP record headers, bounded recon-table eviction, selected and detail-page target retention, vendor database bounds and private-address handling, disconnect/BLE burst monitors, and lab frame construction.

The same run feeds 40,000 synthetic malformed/random radio buffers through the Wi-Fi/BLE parsers. These are robustness tests, not a field detection accuracy measurement.

## Startup diagnosis and display repair

Two early builds wrote successfully but left the screen blank. Restoring the original application recovered normal factory operation. The MP24 recipe now pins pioarduino `55.03.312-1` / Arduino 3.3.12 in a project-local toolchain.

Minimal USB-only and USB-plus-2-MB-PSRAM probes both booted under the existing bootloader. Added startup tracing localized the full application's stall to `TFT_eSPI::init()`, rather than establishing a bootloader mismatch. The application now uses a small ST7735 adapter through Espressif's queued SPI API; TFT_eSPI supplies only the framebuffer drawing operations. The adapter uses a 2,560-byte internal DMA staging buffer, bounded completion waits and a persistent transaction descriptor. A failed transfer stops further panel transfers while USB diagnostics remain available.

The replacement app subsequently reached `HP_READY`, mounted SD, communicated with the EG91 and identified SIM absence. Its framebuffer was captured and visually inspected. USB observed hundreds of completed display frames with zero SPI errors; that does not itself verify physical panel color, orientation or keypad usability. The dedicated SD write/read/compare test passed and removed its own temporary file. Existing card data was not formatted.

Recon testing also found a Bluetooth-controller initialization panic. Arduino 3.3.12's startup releases BLE memory unless a linked library registers that it needs it; pinned NimBLE-Arduino 2.3.6 does not include the new registration header. The radio adapter now includes the framework's `esp32-hal-alloc-ble-mem.h`, and initializes the default network event loop before Wi-Fi. Combined radio validation is recorded separately below.

## Combined radio validation

After the BLE memory registration fix, an on-device combined scan observed 456 Wi-Fi frames and 2,090 BLE advertisements. Queue drops remained zero; four successive ten-second samples each reported 138,192 bytes free internal heap and 1,956,348 bytes free PSRAM. Modem status stayed online/No-SIM, I²C errors stayed zero, and the display completed 1,080 cumulative frames with zero transfer errors. These observations establish a short engineering smoke test, not long-term field stability or detection accuracy. The final preview is rechecked after cleanup changes.

The earlier 0.1 preview SHA-256 was `95d157ff64e52523302600bc1ba6f0b6657bc242bb068950e118e6fe94fac0da` (1,238,976 application bytes). At 91.6 seconds uptime, its smoke test reported 440 Wi-Fi frames, 2,130 BLE advertisements, 42 live observations, zero queue drops, zero I²C errors, 906 completed display frames with zero transfer errors, 33,474 bytes in the SD CSV session, and a passing full heap integrity check. Free internal heap held at 138,120 bytes across the four radio samples; free PSRAM held at 1,956,348 bytes. The loop-task stack high-water mark was 5,048 bytes free. The modem remained online with SIM absent. The user subsequently confirmed the visible panel and joystick/A navigation.

## 0.2 toolkit validation — 2026-10-14

- Three successive ten-second combined listening samples held internal free heap at 112,632 bytes and free PSRAM at 1,930,748 bytes. At about 76 seconds uptime: 298 Wi-Fi frames, 1,380 BLE advertisements, zero queue drops, zero I²C errors and a passing heap-integrity check. Display transfers remained error-free.
- SD write/read/compare passed. A capture was exported in full over USB and independently parsed: 249 PCAP records, 87,052 total bytes, largest frame 561 bytes and no truncated records in that sample. This validates one short recording and export, not full-card or removal behavior.
- A temporary service on the owner's laptop verified GATT discovery (35 characteristics), an exact expected read, an acknowledged write received by the laptop, subscribe/notification delivery, unsubscribe and disconnect. Combined recon resumed afterward with zero queue drops and a passing heap-integrity check. The fixture was stopped after testing; no third-party device was connected.
- The laptop received the phone's named BLE test beacon. A requested Wi-Fi scan also independently observed its `HackerPhone-Lab` Wi-Fi beacon. Both timed modes were checked for the 15-second automatic stop; an explicit stop was also exercised. Successful driver calls alone were not treated as evidence of reception.
- Framebuffer inspection caught a partial-clear issue caused by using a display-sized clear on a landscape sprite. Full sprite clearing replaced it; the four themes are checked again on the final preview. Screenshots verify the framebuffer, not the physical panel's contrast.
- PCAPs, nearby device identifiers, serial logs and recovery images remain local and are excluded from this repository.

## Acceptance gates still open

1. The user confirmed the replacement panel is visible and joystick/A navigation works. Physical acceptance of the expanded 0.2 screens remains open.
2. Sustained recording, removal/full-card behavior and power-loss recovery beyond the passing SD smoke test.
3. Long-duration No-SIM and registered-cellular operation while passive Wi-Fi/BLE scans run.
4. Field checks of device-family evidence and RSSI hunt behavior, plus sustained table/queue pressure tests.
5. Speaker/mic/PCM audio route and cellular call states work with a compatible SIM and carrier.
6. SMS inbox/outbox, actual sending/receiving, contacts and failure/retry UI are tested.
7. Each Marauder feature in the inventory is ported or has a specific hardware limitation recorded.
8. Daily power use and complete recovery/distribution checks pass before publishing a web flasher or a daily-use release. The source repository is available for development with its current limitations documented.
