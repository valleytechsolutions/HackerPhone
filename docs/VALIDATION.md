# Engineering validation

## Completed on the original device

- ROM identification: ESP32-S3 rev 0.2, 4 MB flash, 2 MB quad PSRAM.
- Full 4,194,304-byte recovery image, checksum recorded privately.
- Original app descriptor, partition table, boot diagnostics and peripheral identities inspected.
- Original firmware mounts the installed 3840 MB card and communicates with the EG91-NAX modem; SIM is absent.
- Recovery drill: original application partition restored, flash hash verified, and original runtime/AT diagnostics observed afterward. Other flash partitions and SD data were not written by that recovery.

## Native code checks

`tools/test_core.py` passes 18 groups covering normal no-SIM operation without power cycling, call state acknowledgments versus actual active calls, incoming call events, UCS-2 SMS PDU structure, bounded UTF-8 parsing, timeout/ambiguous message handling without auto-retry, SIM removal during a send, registration/CSQ status, wraparound timing, malformed UART lines, UART write failure, CSV quoting/formula neutralization, file path validation, PCAP record headers, bounded recon-table eviction, selected and detail-page target retention, vendor database bounds and private-address handling, disconnect/BLE burst monitors, lab/attack frame construction, and bounded IR import/protocol encoding.

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
- Framebuffer inspection caught a partial-clear issue caused by using a display-sized clear on a landscape sprite. Full sprite clearing replaced it. The final preview's four themes, settings, security menu and SD browser were captured; inspection confirmed the old right-edge wallpaper artifact was gone. Screenshots verify the framebuffer, not the physical panel's contrast.
- PCAPs, nearby device identifiers, serial logs and recovery images remain local and are excluded from this repository.

The final 0.2 development preview was rebuilt from the separate HackerPhone folder and flashed application-only with a matching write hash: **1,304,784 bytes**, SHA-256 `cf099a1755aee5e8223fc2c5b10ef29898c7eb0e57a13c5a368632e7fb1d8dc9`. The final idle/UI check reported a passing heap-integrity check, 3,260 bytes of loop-task stack headroom, 296 display frames with zero transfer errors, working SD availability, and modem online/No-SIM. An empty GATT target command was rejected. This hash identifies the local tested artifact; rebuilding in another environment can change it.

## 0.3 attack and infrared preview — 2026-10-14

- All 21 local attack builders were exercised across 256 synthetic entropy values, including undersized output buffers, canaries, invalid targets and BLE advertisement structure. This checks construction/bounds, not peer vulnerability or delivery.
- A one-second, one-packet open-authentication session targeted the owner's explicitly selected test SSID. The driver accepted one request, reported no error, and the timer stopped the session. The accompanying capture contained two records and no authentication response. No deauthentication or pairing-spam effect is claimed from that pilot.
- IR native checks cover raw/parsed records, multiple buttons, malformed headers/names/values, the 1,024-duration limit, parsing-budget exhaustion, unsupported-protocol continuation, traversal rejection and spaced paths. NEC timing was decoded independently in the test to check address/command byte order and inverse bytes.
- On the final device build, the dedicated IR SD test passed: write two synthetic buttons, load/advance/backtrack, preserve the last button at end of file, then remove only the test fixture. Three receive start/stop cycles and a separate ten-second timeout completed without a reset. No optical signal was received during these unattended checks, and no IR transmission was performed.
- After two ten-second combined recon samples, internal free heap held at 111,492 bytes; the second sample had 156 Wi-Fi frames, 737 BLE advertisements, zero queue drops and zero I²C errors. Stopping recon, starting/stopping IR and rerunning its SD test left heap integrity passing with 2,696 bytes of loop-task stack headroom. Display transfers reached 802 frames with zero errors; the backlight probe succeeded.
- A preceding warm-reset boot had one backlight-expander probe failure. Initialization now retries that identified device up to three times, preserving unrelated output bits. The final boot reported no I²C error. USB framebuffer writes now check for a short write and allow bounded backpressure; final captures completed with their expected trailer.

Final locally flashed 0.3 application: **1,333,200 bytes**, SHA-256 `a67f259a7cbe9d4e1d107e1277259907e3ae2215c265ac21cf7b26f75decdd93`. It was written only at `0x10000`, with recovery/identity checks and write-hash verification. A private recovery copy of this working application is retained. The physical IR module still needs a remote-control learning/replay test; RMT startup and filesystem tests do not establish optical operation.

## Acceptance gates still open

1. The user confirmed the replacement panel is visible and joystick/A navigation works. Physical acceptance of the expanded 0.2 screens remains open.
2. Sustained recording, removal/full-card behavior and power-loss recovery beyond the passing SD smoke test.
3. Long-duration No-SIM and registered-cellular operation while passive Wi-Fi/BLE scans run.
4. Field checks of device-family evidence and RSSI hunt behavior, plus sustained table/queue pressure tests.
5. Speaker/mic/PCM audio route and cellular call states work with a compatible SIM and carrier.
6. SMS inbox/outbox, actual sending/receiving, contacts and failure/retry UI are tested.
7. Each Marauder feature in the inventory is ported or has a specific hardware limitation recorded.
8. Daily power use and complete recovery/distribution checks pass before publishing a web flasher or a daily-use release. The source repository is available for development with its current limitations documented.
9. Optical IR reception/replay, carrier/polarity verification, range and imported-remote appliance behavior.
