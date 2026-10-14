# HackerPhone toolkit controls

The current build adds real tools to the working MakerPhone port. It does not yet reproduce every mode from the reference projects. Original HackerPhone implementations remain MIT, as requested; GPL application code from ChimeraBLE or SquachWatch is not incorporated.

## On the device

Open **Security** for radar, APs, stations, probes, BLE, channels, alert history, packet recording and radio test tools. Up/Down moves between entries; A or joystick click opens them. C returns home and ends an active transmitter or GATT connection. Passive listening/recording can continue behind the home screen; the top status line shows RX or REC. **Stop all radio tools** ends listening, transmission, connection and PCAP recording.

**Settings → Theme** cycles VTS Red, Dragon, Green Radar and Black & Gold. The selection survives reboot. The display remains on at full brightness. Scan mode and channel lock currently reset at boot.

**Device list → A** hunts the selected signal. The plot shows the most recent 24 seconds, filtered RSSI, peak and trend. Gaps and stale samples are not presented as a new reading. Radius represents relative signal strength; angular placement is a stable layout, not a measured direction. The 160×128 radar labels up to five signatures; the lists hold up to 64 observations. Selected/visible targets are protected from eviction.

**Device list → D** offers evidence, raw advertisement bytes, GATT connection and advertisement replay. Low-confidence names and OUIs stay labeled as such. A tracker advertisement cannot establish who owns it, whether it is following someone, or whether it is malicious. BLE privacy addresses remain distinct observations rather than being silently joined into one identity.

**GATT explorer:** choose a live BLE target, then Connect / BLE GATT. Up/Down selects a characteristic; A reads; D subscribes/unsubscribes; # saves the discovered tree to SD. B disconnects. The UI retains up to 48 characteristics, 48 bytes of the most recent value, and a notification counter. The small screen shows the first 16 value bytes; the USB status exposes up to 48. No automatic write or pairing-key persistence is requested. Some peers require pairing/security that this UI does not yet support. Live testing is separate from compilation.

**Radio test tools:** review and start a fixed-name `HackerPhone-Lab` Wi-Fi beacon or BLE beacon session. Complete legacy BLE advertisements up to 31 bytes can also be replayed from the selected device menu. Sessions end after 15 seconds, on Stop, on C/B leaving the test, or when a phone call takes priority. Wi-Fi sends at most ten frames per second; BLE uses a 100 ms advertising interval. A driver rejection ends the Wi-Fi session with an error. Accepted transmit calls alone do not establish reception on another radio. Replay does not copy the remote address, clone services, break encryption or transfer pairing keys.

**Wi-Fi / BLE attacks:** choose a live Wi-Fi AP from its device-tools menu, optionally select an observed client belonging to that BSSID, then review a mode under Security. Left/Right changes session duration; D changes rate; A starts; A/B stops the running session. Defaults are 15 seconds and ten packets per second, bounded to 1–60 seconds and 1–100 packets per second. BLE payload/address updates are capped at ten per second; actual advertisement interval is separately configured. The running screen reports driver-accepted requests and errors, not affected devices. Nothing starts automatically. The [mode ledger](MARAUDER-SCOPE.md) separates experimental subsets from unfinished upstream features.

The SD SSID playlist reads `/hackerphone/ssids.txt`, one printable 1–32-byte SSID per line. Blank lines and lines starting with `#` are skipped. It retains one name in RAM, with a 4 KiB work limit per search. A file without a valid line in that window stops the session. Active EAPOL starts an EAPOL-filtered capture when none is already running; an existing capture keeps its original filtering and ownership.

**Infrared** is a separate home app with recording, single-signal transmission and SD library browsing. See [IR controls](INFRARED.md). Entering IR stops radio tools and CSV recording.

## Traffic evidence

- Disconnect candidate: six deauthentication/disassociation frames in a rolling three-second window; repeated retry frames are suppressed. Sources are spoofable. Protected reason fields remain unknown.
- BLE candidate: twelve addresses in five seconds, or 24 pairing-payload variants. Repeated identical advertisements do not independently trigger an alert.
- SSID conflict: repeated contemporary advertisements for the same SSID with open versus protected security. Multiple mesh APs alone are not an alert.
- Channel bars count received frames per time actually spent listening on each channel. They are observations, not calibrated channel utilization or total airtime.

Radio alternation, channel hopping, shielding, weak signals and unsupported bands leave observation gaps. ESP32-S3 provides 2.4 GHz Wi-Fi and BLE; there is no 5 GHz or Bluetooth Classic reception.

## SD capture and vendor data

PCAP uses raw IEEE 802.11 link type 105 with a 1,024-byte snap length and original length retained for truncated packets. Capture timestamps are **time since boot expressed in PCAP's timestamp fields**, not a validated wall clock. Select EAPOL-only recording to retain observed EAPOL frames; a frame count is not proof of a complete handshake. The 64 KiB PSRAM buffer absorbs bursts and reports overflow. Writes stop on a short write. Flushes occur every two seconds; power loss can leave the final record incomplete. Files are never formatted or overwritten. There is no infinite recording capacity guarantee.

CSV snapshots and PCAP/GATT files live under `/hackerphone/`. USB export reads only that directory. Stop recording before exporting a capture:

```text
PCAP STOP
```

```powershell
python tools/phone_usb.py --port COM58 capture-00001.pcap saved-capture.pcap
```

An SD card is optional for listening, radar, themes, hunts and GATT. For a larger vendor list, obtain an IEEE MA-L CSV with `Assignment` and `Organization Name` columns and retain its source terms. Build the separate SD data file:

```powershell
python tools/build_vendor_db.py path/to/oui.csv path/to/sd/hackerphone/oui.bin
```

Copy `oui.bin` and its generated provenance JSON to `/hackerphone/` on the card and reboot. This is an independent `HPOUI1` format, not a ChimeraBLE binary. The loader bounds the record count and file size and searches fixed records with a small cache. It looks up only public BLE or globally assigned Wi-Fi addresses. Missing/invalid data leaves detection operational. Vendor lookup supplies a manufacturer clue; it never promotes a generic chip vendor into a camera identification. No external vendor database is bundled or relicensed as MIT.

## USB console

| Command | Action |
|---|---|
| `INFO`, `HEALTH`, `DISPLAY`, `TOOLKIT` | Aggregate diagnostics, heap integrity, radio/capture state |
| `HOME`, `TOOLS`, `SETTINGS`, `FILES`, `CHANNELS`, `ALERTS`, `HARDWARE` | Open a screen |
| `THEME 0` through `THEME 3` | Persist the theme |
| `MODE 0`, `MODE 1`, `MODE 2` | Combined, Wi-Fi-only, BLE-only |
| `RECON`, `PAUSE`, `STOP` | Start listening, pause listening, or stop all radio tools |
| `TARGETS`, `BLEAD index` | Observations or retained raw BLE advertisement |
| `PCAP START`, `PCAP EAPOL`, `PCAP STOP` | Begin raw/EAPOL recording or finish the file |
| `GATT CONNECT index`, `GATT STOP` | Explicitly connect to a live selected BLE observation or disconnect |
| `GATT STATUS`, `GATT DUMP` | Link/value state or discovered tree |
| `GATT READ handle` | Read a hexadecimal characteristic handle |
| `GATT WRITE handle hexbytes` | Explicit acknowledged write of 1–64 bytes |
| `GATT SUB handle`, `GATT UNSUB handle` | Enable or disable notification/indication subscription |
| `LAB WIFI`, `LAB BLE`, `LAB STATUS` | Start the named 15-second beacon session or inspect it |
| `ATTACKS`, `ATTACK LIST`, `ATTACK STATUS` | Open the review menu, list local mode IDs, inspect driver/session state |
| `ATTACK AP index`, `ATTACK CLIENT index`, `ATTACK CLEAR CLIENT` | Snapshot a live AP and optionally one of its observed clients |
| `ATTACK TIME seconds`, `ATTACK RATE packets-per-second` | Set bounded session parameters while stopped |
| `ATTACK CHANNEL n`, `ATTACK DEST n`, `ATTACK SSID name` | Set source/destination channel (1–11) or SSID |
| `ATTACK START mode-id`, `ATTACK STOP` | Explicit start/stop; IDs come from `ATTACK LIST`, not the upstream ledger |
| `IR`, `IR STATUS`, `IR RECORD`, `IR STOP` | Open/inspect/capture/cancel IR; further commands are in the IR guide |
| `FILEINFO relative-path`, `FILECHUNK relative-path offset` | Read-only SD export protocol |
| `SDTEST`, `SCREEN`, `RETRYMODEM` | Dedicated SD test, framebuffer capture, or modem-session retry |
| `LIGHT` | Retry the identified backlight expander and request full brightness |

Radio operations are not queued automatically on boot. Commands do not place calls or send texts. Captures and exported observation lists may contain nearby device identifiers; development outputs remain private and ignored by version control.
