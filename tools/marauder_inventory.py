"""Generate the reviewed mapping for the pinned Marauder mode identifiers."""
import argparse, collections, json, re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('header', type=Path)
a = p.parse_args()
source = a.header.read_text().split('#define BASE_MULTIPLIER')[0]
modes = re.findall(r'^#define\s+(WIFI_\w+|BT_\w+|REMOTE_ID_\w+|GPS_\w+|OTA_UPDATE|SHOW_INFO|ESP_UPDATE|LV_\w+)\s+(\d+)\b', source, re.M)
if not modes: p.error('No mode identifiers found; inventory unchanged')
review = {}
def map_modes(names, status, implementation, limits):
    for name in names.split():
        if name in review: raise ValueError('Duplicate review: '+name)
        review[name] = dict(status=status, implementation=implementation, limits=limits)

map_modes('WIFI_SCAN_OFF', 'implemented', 'C / Stop all tools / STOP', 'Connection workers cancel asynchronously; no automatic active test on boot.')
map_modes('SHOW_INFO', 'implemented', 'Hardware screen / INFO / HEALTH / DISPLAY', 'Engineering-sample evidence only; not production board certification.')
map_modes('WIFI_SCAN_PROBE WIFI_SCAN_AP WIFI_SCAN_STATION WIFI_SCAN_ALL', 'implemented', 'Security lists and SCAN 0..4', '64 live/saved observation slots, 15-second live window; 2.4 GHz only.')
map_modes('WIFI_SCAN_PWN WIFI_SCAN_PINESCAN WIFI_SCAN_ESPRESSIF WIFI_SCAN_MULTISSID', 'implemented_unvalidated_detection_accuracy', 'Dedicated scanners 5..8', 'Pineapple heuristics are low-confidence shared-hardware clues. Multi-SSID requires three distinct SSIDs per BSSID; only three variants retained.')
map_modes('BT_SCAN_ALL BT_SCAN_SIMPLE BT_SCAN_SIMPLE_TWO', 'implemented', 'All BLE devices / SCAN 4; optional BLE ACTIVE 1', 'Shared list/details UI instead of upstream layouts; legacy BLE advertising only, no Bluetooth Classic.')
map_modes('BT_SCAN_SKIMMERS BT_SCAN_AIRTAG BT_SCAN_FLIPPER BT_SCAN_FLOCK BT_SCAN_RAYBAN BT_SCAN_IBEACON', 'implemented_unvalidated_detection_accuracy', 'Dedicated scanners 9..14 and evidence pages', 'Public identifier/name heuristics; spoofable and not evidence of malicious intent. No physical fixture for every device family.')
map_modes('BT_SCAN_AIRTAG_MON', 'independent_subset', 'Find My scanner, raw advertisements, payload-change counter and RSSI history', 'Address rotation is not resolved to a persistent identity. No ownership/key decryption.')
map_modes('REMOTE_ID_SCAN_ALL REMOTE_ID_SCAN_TARGET', 'implemented_unvalidated_detection_accuracy', 'Drone scanner; selected-device Remote ID detail page and hunt', 'Wi-Fi beacon/NAN and BLE decoded by pinned OpenDroneID; eight decoded-aircraft slots. Coordinates are self-reported. No live drone fixture yet.')
map_modes('WIFI_SCAN_EAPOL WIFI_SCAN_TARGET_AP WIFI_SCAN_TARGET_AP_FULL', 'implemented', 'Raw/EAPOL PCAP; target tools Record this AP; PCAP AP / PCAP EAPOLAP; EAPOL monitor', '1024-byte snaplen. AP filter matches frame addresses. Complete-sequence count checks order/replay/nonce, not MIC/password correctness.')
map_modes('WIFI_SCAN_DEAUTH', 'implemented', 'Disconnect scanner and Traffic alerts', 'Retry-filtered burst threshold; cannot prove an attack or identify the operator.')
map_modes('WIFI_PACKET_MONITOR WIFI_SCAN_PACKET_RATE BT_SCAN_ANALYZER', 'implemented', 'Packet / BLE analyzer; D toggles EAPOL summary', '48 one-second samples; combined mode alternates Wi-Fi/BLE and is not simultaneous RF reception.')
map_modes('WIFI_SCAN_CHAN_ANALYZER WIFI_SCAN_CHAN_ACT', 'implemented', 'Channel observations / channel lock', 'Channels 1..11; rates normalized by time listening, not a spectrum analyzer.')
map_modes('WIFI_SCAN_AP_STA WIFI_SCAN_DISPLAY_AP_INFO', 'implemented', 'AP/client map and AP details pages', 'Client association inferred from DS-direction traffic; security/WPS/PMF/manufacturer are advertised claims.')
map_modes('WIFI_SCAN_SIG_STREN BT_SCAN_FOX_HUNT', 'implemented', 'Selected-device signal hunt', 'RSSI trend and history; no measured distance, bearing, or verified audio feedback.')
map_modes('WIFI_SCAN_RAW_CAPTURE', 'implemented', 'Record Wi-Fi PCAP / PCAP START', '64 KiB PSRAM ring; 1024-byte snaplen; uptime timestamps; loss/error counters; SD required.')
map_modes('WIFI_SCAN_SAE_COMMIT', 'implemented', 'SAE authentication scanner / SCAN 21', 'Counts observed SAE authentication frames, not authentication success.')
map_modes('LV_SELECT_AP', 'implemented', 'Use as attack AP / Target settings / ATTACK AP', 'Explicit target snapshot; no automatic transmission.')
map_modes('LV_ADD_SSID', 'independent_subset', 'Editable beacon SSID and saved SD SSID playlists', 'No complete upstream list editor or native Marauder JSON import; ASCII SSID playlist streamed from SD.')
map_modes('LV_JOIN_WIFI WIFI_CONNECTED', 'implemented_unvalidated_network_peer', 'Connected network tools: SSID/password editor, saved local profile, Join, DHCP status', 'No live authenticated-router join tested in this pass; one local NVS profile, no Internet uplink routing.')
map_modes('WIFI_PING_SCAN WIFI_ARP_SCAN WIFI_PORT_SCAN_ALL WIFI_SCAN_TELNET WIFI_SCAN_SSH WIFI_SCAN_DNS WIFI_SCAN_HTTP WIFI_SCAN_HTTPS WIFI_SCAN_SMTP WIFI_SCAN_RDP', 'implemented_unvalidated_network_peer', 'Network tools / NETWORK PROBE 0..11', 'TCP/ICMP loopback passes; LAN peer effects untested. Subnet sweeps /24..30; named services test TCP port reachability, not application identity. Full-port scan cancellable, up to 65535 ports.')
map_modes('WIFI_HOSTSPOT WIFI_SCAN_EVIL_PORTAL', 'implemented_unvalidated_network_peer', 'Hotspot / captive portal; select probe SSID for Karma-style portal; SD HTML and POST log', 'Own AP only, no Internet forwarding. HTTP headers 1 KiB, POST 512 B, HTML 16 KiB, form queue four. No HTTPS interception; no arbitrary probe responder.')
map_modes('WIFI_ATTACK_BEACON_SPAM WIFI_ATTACK_BEACON_LIST WIFI_ATTACK_RICK_ROLL WIFI_ATTACK_FUNNY_BEACON', 'experimental_effect_unvalidated', 'Random / SD / Rickroll / funny beacon playlists', 'Original short playlist text; duration 1..60s, 1..100 requested packets/s. Shared RF reachability not tested per playlist.')
map_modes('WIFI_ATTACK_AUTH', 'experimental_effect_unvalidated', 'Probe requests and open authentication requests', 'One earlier driver-accepted authentication request; no peer response was observed.')
map_modes('WIFI_ATTACK_MIMIC WIFI_ATTACK_AP_SPAM WIFI_ATTACK_CSA WIFI_ATTACK_QUIET', 'experimental_effect_unvalidated', 'Captured full-beacon clone, random-BSSID clone, CSA and Quiet IE replacement', 'Complete <=1024 B beacon snapshot required. Native bounds/security-IE tests pass; no peer channel/silence effect established.')
map_modes('WIFI_ATTACK_DEAUTH WIFI_ATTACK_DEAUTH_MANUAL WIFI_ATTACK_DEAUTH_TARGETED WIFI_SCAN_ACTIVE_EAPOL WIFI_SCAN_ACTIVE_LIST_EAPOL', 'experimental_effect_unvalidated', 'AP/client MAC selection, manual targets, 16-entry AP/client list, deauth/disassoc and EAPOL PCAP', 'No iPhone disconnect/reconnect result: supplied client was not observed on the test AP 2.4 GHz channel. Driver acceptance alone is not delivery.')
map_modes('WIFI_ATTACK_BAD_MSG WIFI_ATTACK_BAD_MSG_TARGETED WIFI_ATTACK_SLEEP WIFI_ATTACK_SLEEP_TARGETED', 'experimental_subset_effect_unvalidated', 'Selected client EAPOL M1 / association-power-management builders; AP/client lists', 'Explicit client per selected AP required; no automatic all-station walk and no peer-effect test.')
map_modes('WIFI_ATTACK_SAE_COMMIT', 'experimental_subset_effect_unvalidated', 'Group-19 scalar/point generation, stable client and anti-clogging token retry, per-AP list state', 'Hunting-and-pecking token layout; no H2E negotiation, successful SAE authentication, or live WPA3 response test.')
map_modes('BT_ATTACK_SOUR_APPLE BT_ATTACK_SWIFTPAIR_SPAM BT_ATTACK_SPAM_ALL BT_ATTACK_SAMSUNG_SPAM BT_ATTACK_GOOGLE_SPAM BT_ATTACK_FLIPPER_SPAM BT_ATTACK_APPLE_JUICE', 'experimental_effect_unvalidated', 'Pairing advertisement modes 14..20; timed start/stop', 'Builder/bounds tests pass; per-device OS popup/effect untested. BLE advertising rate capped at ten updates/s.')
map_modes('BT_SPOOF_AIRTAG', 'experimental_subset_effect_unvalidated', 'Selected-device Replay BLE payload + MAC', 'Live random BLE addresses and complete <=31 B payloads only; public MAC, GATT, pairing keys and identity rotation are not cloned. 15-second session.')
map_modes('BT_FINDMY_SOUND', 'implemented_protocol_fixture_validated', 'Selected-device Tracker sound request / TRACKER SOUND index', 'AirTag, Find My and DULT writes independently received by laptop fixture; real tracker sound/ownership behavior not verified.')
map_modes('BT_ATTACK_FINDMY_LIVE', 'implemented_unvalidated_peer_effect', 'Find My live sound sweep / TRACKER LIVE START', 'Explicit 15-second sweep, up to eight distinct fresh Find My addresses; no broad live sweep run during QA. Accepted writes do not establish audible sound.')
map_modes('WIFI_SCAN_GPS_DATA WIFI_SCAN_WAR_DRIVE WIFI_SCAN_STATION_WAR_DRIVE BT_SCAN_WAR_DRIVE BT_SCAN_WAR_DRIVE_CONT WIFI_SCAN_GPS_NMEA GPS_TRACKER GPS_POI BT_SCAN_FLOCK_WARDRIVE WIFI_SCAN_DETECT_FOLLOW', 'unavailable_pending_gnss_hardware', 'No geolocation workflow enabled', 'No verified GNSS source, antenna or route on this engineering sample. SD recon logs remain available without invented location. Location-based following/geofence inference is not implemented.')
map_modes('OTA_UPDATE', 'unavailable_partition_layout', 'App-only USB development flashing', 'Factory-only partition map has no OTA slot. Repartitioning would destroy the established recovery contract; no OTA claim.')
map_modes('ESP_UPDATE', 'independent_subset', 'App-only USB update with chip/recovery/hash checks', 'No on-device SD updater or web flasher until engineering hardware/recovery acceptance.')
rows=[]
for name,value in modes:
    if name not in review: p.error('Unreviewed upstream mode: '+name)
    rows.append(dict(mode=name,upstream_value=int(value),**review[name]))
unused=set(review)-{x['mode'] for x in rows}
if unused:p.error('Review names missing from header: '+','.join(sorted(unused)))
data=dict(reference='justcallmekoko/ESP32Marauder',commit='d3ceb28afeda1196ca27c13adeaa69ada52383f3',meaning='Reviewed capability mapping, not full feature parity. Numeric aliases are preserved; use local command identifiers from TOOLKIT.md.',modes=rows)
(root/'docs/marauder-modes.json').write_text(json.dumps(data,indent=2)+'\n')
print('Reviewed',len(rows),'upstream identifiers:',dict(collections.Counter(x['status'] for x in rows)))
