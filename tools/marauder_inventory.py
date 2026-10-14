"""Record the pinned upstream mode list without copying its implementation."""
import argparse,json,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('header',type=Path);args=p.parse_args()
names=[]
for name,value in re.findall(r'^#define\s+(WIFI_\w+|BT_\w+|REMOTE_ID_\w+|GPS_\w+|OTA_UPDATE|SHOW_INFO|ESP_UPDATE|LV_\w+)\s+(\d+)\b',args.header.read_text().split('#define BASE_MULTIPLIER')[0],re.M):
    if int(value)>102:continue
    names.append({'mode':name,'upstream_value':int(value),'status':'not_ported'})
baseline={'WIFI_SCAN_AP','WIFI_SCAN_ALL','WIFI_SCAN_PWN','BT_SCAN_ALL','BT_SCAN_AIRTAG','BT_SCAN_FLIPPER','BT_SCAN_FLOCK','BT_SCAN_RAYBAN','BT_SCAN_FOX_HUNT','WIFI_SCAN_SIG_STREN','BT_SCAN_IBEACON','WIFI_SCAN_PINESCAN','REMOTE_ID_SCAN_ALL'}
for item in names:
    if item['mode'] in baseline:item['status']='partial_independent_recon_baseline'
implemented={
    'WIFI_SCAN_OFF':'stop_all_radio_tools',
    'WIFI_SCAN_PROBE':'probe_request_view',
    'WIFI_SCAN_AP':'access_point_view',
    'WIFI_SCAN_STATION':'transmitting_station_view',
    'WIFI_SCAN_DEAUTH':'passive_disconnect_burst_monitor',
    'WIFI_SCAN_EAPOL':'passive_eapol_counter_and_filtered_pcap',
    'WIFI_SCAN_RAW_CAPTURE':'buffered_1024_byte_snaplen_pcap',
    'WIFI_SCAN_CHAN_ANALYZER':'frames_per_listening_second',
    'WIFI_SCAN_SIG_STREN':'selected_wifi_channel_and_rssi_history',
    'BT_SCAN_FOX_HUNT':'selected_ble_address_and_rssi_history',
    'WIFI_SCAN_EVIL_PORTAL':'ssid_security_mismatch_evidence_only',
}
for item in names:
    if item['mode'] in implemented:
        item['status']='independent_subset_implemented'
        item['implementation']=implemented[item['mode']]
data={'reference':'justcallmekoko/ESP32Marauder','commit':'d3ceb28afeda1196ca27c13adeaa69ada52383f3','meaning':'Inventory only; no full-parity or hardware-validation claim. Numeric modes are upstream identifiers, not our dispatch API.','modes':names}
(root/'docs/marauder-modes.json').write_text(json.dumps(data,indent=2)+'\n')
print(f'Recorded {len(names)} upstream mode identifiers')
