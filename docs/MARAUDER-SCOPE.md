# Toolkit scope and unfinished work

Reference: [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder/tree/d3ceb28afeda1196ca27c13adeaa69ada52383f3), pinned during this port. [The complete mode ledger](marauder-modes.json) records upstream numeric identifiers, including aliases, without claiming an implementation. This is not an official Marauder distribution.

| Area | Engineering baseline | Still required |
|---|---|---|
| Phone | No-SIM handling, AT transport, call-state parser, message composer and reviewed SMS-submit flow | Voice PCM/mic bridge; SIM PIN UI; SMS inbox decoder, storage and contacts; call logs; carrier/VoLTE/SIM checks; actual call/SMS testing |
| Wi-Fi recon | Passive 2.4 GHz management/data observation, AP/station/probe views, transmitter-to-BSSID association, family evidence, channel observations and channel lock | Full scan modes, complete AP/client graph and network utilities |
| BLE recon | Passive advertisement scan, family classifiers, address types, retained advert browser, optional SD OUI lookup | Full SIG company/UUID dictionary, extended-advert support and complete scanner parity |
| Counter-surveillance | Dragon Detector families plus retry-filtered disconnect, BLE advert-burst and SSID-security-conflict monitors; 16-event history | Full parity regression against upstream ledgers; persistent alerts/watch/ignore rules; phone audio alerts |
| Fox hunt | Selected target radio/channel focus, median plus time-based RSSI smoothing, 24-second signal graph, peak/trend/stale indicators | Per-family audio feedback, persistent selection, directional accessories if any are added |
| SD | CSV recon snapshots, buffered raw/EAPOL PCAP, GATT tree export, read-only USB export, paged file list and preview; no formatting | File management, sustained full/removal/power-loss tests |
| Active test tools | Timed named Wi-Fi/BLE beacons, legacy advert replay and GATT; experimental packet builders and timed sessions for deauth/disassoc/auth/SAE/CSA/quiet/sleep/bad-message, active EAPOL and pairing adverts | Peer-effect validation; complete beacon/security-IE cloning and SAE token exchange; AP-list workflows/manual MAC targets; captive portals; Find My live workflows; BLE GATT cloning/proxy/MITM; complete mode parity |
| Infrared | Independent raw capture/replay, unique SD recording saves, streamed `.ir` libraries and five parsed-protocol encoders | Physical receiver/transmitter validation; more parsed protocols; carrier measurement; large-library and power-loss testing |
| Network utilities | Not implemented | Wi-Fi join profiles, network/host/service discovery and saved configurations |
| GPS / wardriving | Not implemented | Confirm whether this EG91-NAX board exposes supported GNSS; verify antenna/routing, then timestamps/location and exports. GPS and 5 GHz are not assumed. |
| Power / daily use | Full brightness, no auto-dimming in first preview | Sleep/wake, calibrated battery monitoring, charging/power-button behavior, per-app volume and tones |
| UI | Owner logos, four persistent themes, family colors, live RX/REC/TX status, keypad menus and explicit active-session start/stop | Physical acceptance of new screens, accessibility, larger-screen alternative if needed |
| Distribution | Separate public HackerPhone source repository, MIT notices and build/test CI | Web flasher after hardware/recovery acceptance; no factory image is published |

The ESP32-S3 supplies 2.4 GHz Wi-Fi and BLE; software cannot add 5 GHz reception or Bluetooth Classic. The modem's cellular radio is a separate component. RSSI is displayed as a relative signal indicator, not a distance or compass bearing. No automatic active radio tests run on boot.

The pinned Espressif raw-frame API does not promise arbitrary management-frame injection. This build includes the small raw-frame sanity override and linker technique used by Marauder, with its MIT notice retained. The link map confirms that the application definition is linked. One open-authentication request to the owner's selected test AP returned success and the session stopped on time; its capture contained no authentication response. **Driver acceptance is not evidence of over-the-air delivery or attack effectiveness.** The other new attack modes have builder/bounds tests but no peer-effect validation yet.

The AP mimic mode reconstructs an open beacon with the selected SSID/BSSID; it does not preserve the original security/capability information elements. CSA and Quiet currently use similarly reconstructed beacons. SAE generates a valid group-19 scalar/point for an initial commit, without anti-clogging-token handling or a complete authentication exchange. These limits are explicit so an experimental menu is not mistaken for complete Marauder parity.

ChimeraBLE `2330423dae8f8fa163617172310784889d8972a7` and SquachWatch `a6167dbb0693963b5ad7022e7923cfb458d7676c` are feature references. The owner chose to keep original HackerPhone code MIT through independent implementation. This choice is recorded to prevent later accidental inclusion of GPL source or generated application datasets.
