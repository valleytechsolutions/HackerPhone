# Marauder capability mapping

HackerPhone 0.5 independently implements the principal Wi-Fi/BLE scan, target, capture and active-test workflows for the MP24 engineering sample, plus SD missions, list editing/import, file management and notification controls. **It does not have exact Marauder feature parity or verified attack effectiveness.**

The [complete reviewed ledger](marauder-modes.json) maps all **94 upstream mode identifiers** at [Marauder commit d3ceb28](https://github.com/justcallmekoko/ESP32Marauder/tree/d3ceb28afeda1196ca27c13adeaa69ada52383f3) to local controls, implementation status and remaining limits. Upstream assigns `85` to both iBeacon and Find My sound; names preserve that distinction. Upstream IDs are not HackerPhone command IDs. `tools/marauder_inventory.py` regenerates the mapping from that pinned header and fails on unreviewed identifiers.

| Area | Current implementation | Limits and next validation |
|---|---|---|
| Wi-Fi recon | APs, stations, probes, AP/client map, Pwnagotchi, Espressif, Pineapple heuristics, three-SSID BSSID detection, WPS/security/PMF/SAE details | 64 observations; channels 1–11 on 2.4 GHz; shared vendor clues are not definitive identities |
| BLE recon | All BLE, HC-03/05/06 clues, Find My/AirTag, Flipper, Flock, Meta/Ray-Ban, iBeacon and additional Dragon Detector families | Legacy advertisements; passive default with optional active scan responses; address rotation is not persistent identity; field accuracy unverified for every family |
| Graphs and hunts | Wi-Fi/BLE packet rates, management/data/control counts, dwell-normalized channels, selected-device signal history, stale/peak/trend indication | RSSI is neither range nor bearing; notification audio is verified, but hunt sonification is not implemented |
| EAPOL and captures | All-channel or selected-AP raw/EAPOL PCAP, M1–M4 counters, ordered nonce/replay-matched sequences and PMKID-frame count | Counts do not verify MICs/passwords; 1024-byte snaplen and uptime timestamps; SD required |
| Remote ID | Wi-Fi beacon/NAN and BLE decoder, selected aircraft identity/location/operator page | Eight decoded-aircraft slots; self-reported coordinates, not independently verified; no live drone fixture |
| Attack targets | Live/manual AP/client selection, marked observations, observed-client snapshots, channel/SSID editor, up to 16 AP/client pairs with 250 ms list dwell | Explicit review/start, 1–60 seconds and bounded rate; selecting clients does not start a transmission |
| Wi-Fi transmission | Named/random/SD/Rickroll/funny beacons, captured beacon cloning and random-BSSID clones, probes, authentication, deauth/disassociation, active EAPOL, bad-message and sleep frames, SAE commits, CSA and Quiet | Builders and bounds tested; original beacon security IEs preserved. Peer effects still unverified. SAE supports group-19 H&P token retries, not H2E or complete authentication |
| BLE transmission | Swift Pair, Fast Pair, Samsung, Flipper, Apple Juice, Sour Apple and mixed adverts; selected legacy payload replay with optional random-MAC replay | No per-OS popup/effect claim; no public-MAC/GATT/key cloning; bounded sessions |
| Find My sound | Selected AirTag/Find My/DULT sound request and timed live Find My sweep | Three protocol writes validated against an owned laptop fixture; no real tracker audibility result; broad sweep intentionally not used during QA |
| Connected network | SSID/password editor, saved local profile, join/DHCP, host/subnet ping, ARP, common/all TCP ports and named service-port subnet sweeps | Local socket tests pass; authenticated-router/LAN peer testing remains open; /24–/30 sweeps; open port does not identify an application |
| Hotspot / portal | Local open/WPA2 AP; SD HTML; DNS/HTTP portal; bounded POST records to SD; selected probe SSID can become the portal SSID | No Internet forwarding or HTTPS interception; not an arbitrary all-probe responder; local service tests do not establish client captive-portal detection |
| Storage | Buffered PCAP, CSV sessions, GATT/network exports, missions, paged browser, Marauder AP JSON interchange, SSID editor, file copy/rename/trash, USB upload and raw SPIFFS backup | Loaded observations remain stale; finite table/file limits; SPIFFS restore and arbitrary database compatibility are not implemented |
| GPS / wardrive / following | Unlocated recon/PCAP logs remain available | Ten upstream modes need a verified GNSS source/antenna/route. None established on this engineering sample; no invented coordinates or following verdicts |
| Updates | Chip-checked, recovery-checked application-only USB development update | Factory-only partition layout has no OTA slot; SD update and web flasher are unfinished |

Auxiliary workflows outside the numeric mode enum are tracked explicitly:

| Workflow | 0.5 state |
|---|---|
| Recon missions | Original JSONL observation/link/probe/alert sessions with manifest; no GNSS geofence or upstream binary-format compatibility |
| SSID/AP/client lists | Add/remove/generate/save/load; marked targets; bounded Marauder AP JSON import/export, verified with synthetic fixtures |
| Radio MAC configuration | AP/station randomize, explicit set and factory reset, with driver readback; idle interfaces only |
| SD file manager | Copy, rename, new folder, recoverable trash and CRC-checked USB upload; no automatic overwrite |
| Filesystem backup/restore | Read-only SPIFFS-to-SD backup verified against the private original; restore deliberately unavailable under this sample's recovery layout |
| WiGLE / WDG | Explicit TLS upload of a user-supplied WiGLE CSV, credential file and receipt; no authenticated external upload test or generated geolocated logs |
| Notification hardware | Eight dimmable LEDs, event pulse patterns, side volume, MP3 and RTTTL; physical audio/LED acceptance recorded |
| Updates | App-only USB; no OTA slot or on-device SD self-updater; no published web flasher |

The 94-row ledger is a mode inventory, not a claim that auxiliary features
disappear from the parity requirement. See [0.5 controls and limits](TOOLKIT.md).

The pinned Espressif raw-frame API does not promise arbitrary management-frame injection. HackerPhone retains Marauder's credited MIT sanity-override/linker technique. A previous one-packet authentication pilot was driver-accepted, but no peer response was observed. The latest iPhone test address was not observed on the AP's 2.4 GHz channel, so no targeted disconnect test was run. **Driver acceptance is not RF delivery, successful disconnection or captured authentication.**

Bluetooth Classic and 5 GHz cannot be added to this ESP32-S3 by firmware. No GNSS assignment or OTA partition is invented to fill a menu entry. Cellular voice audio, contacts, SMS inbox and real SIM/call testing remain separate open phone work; headphone music and notification output now work. BLE GATT proxy/MITM from other reference firmware is also separate from Marauder's mode list.

ChimeraBLE and SquachWatch remain GPL feature references only. The owner chose independent MIT implementations. See [credits](../CREDITS.md), [controls](TOOLKIT.md) and [measured validation](VALIDATION.md).
