# Cellular service and the 4G hotspot

The MP24 engineering sample has a Quectel EG91-NAX LTE modem. On the tested Mint
SIM, its saved network mode was WCDMA only. Restoring automatic network selection
enabled LTE registration. The SIM did not need reactivation.

## Setup on the phone

Open **Settings → Cellular / 4G hotspot**. For Mint, choose **Apply Mint APN** and
**Enable automatic LTE**. The profile sets context 1 to `Wholesale`, IPv4/IPv6,
without changing the IMS or emergency contexts. **Enable VoLTE** requires a modem
restart; this sample already has the T-Mobile VoLTE carrier profile selected.
Do not change carrier profiles or radio bands without evidence that they are wrong.

The Android/iPhone choices in Mint's setup guide describe device setup. This
firmware enters the modem settings directly. LTE registration, an internet data
session and working voice audio are separate checks.

## Share internet

1. Choose **Start cellular data** and wait for **LTE data connected**.
2. Choose **Start 4G hotspot**. Connect another device to **HackerPhone-4G**.
3. **Show WiFi password** displays the generated 12-character WPA2 password. It is
   saved on this phone and survives a restart.
4. **Test internet** makes a small HTTP request to example.com and reports the
   result. **Stop data / hotspot** disconnects sharing.

The hotspot uses IPv4 NAT and supplies the cellular DNS server through DHCP.
An uplink loss disables forwarding and closes the hotspot. The existing local
demo portal remains a separate network tool. Radio scans and transmissions stop
when starting a hotspot; call activity takes priority and closes Wi-Fi sharing.
Cellular data does not auto-start at boot. This version's data profile is Mint.

The modem UART remains at its verified 115200 baud without flow control, so this
is suitable for light internet use rather than smartphone hotspot speeds. No
throughput or sustained battery-life claim has been established.

## Diagnostics

| USB command | Purpose |
|---|---|
| `PHONE STATUS` | SIM, LTE/circuit/packet registration, signal, call and timeout state |
| `PHONE QUERY AT+QNWINFO` | Query network technology and band |
| `PHONE QUERY AT+CGDCONT?` | Query APN contexts |
| `PHONE REPLY` | Read the last bounded diagnostic result |
| `PHONE CONFIG MINT`, `AUTO`, `LTE` | Apply the named APN or network mode |
| `PHONE CONFIG IMS_ON`, `IMS_DEFAULT`, `RESTART` | Configure IMS or restart the modem |
| `CELL START`, `CELL STOP`, `CELL STATUS` | Control and inspect cellular data |
| `CELL HOTSPOT ON`, `CELL HOTSPOT OFF` | Control Wi-Fi sharing |
| `CELL WIFI` | Show local hotspot credentials; keep this output private |
| `CELL TEST` | Test an internet request through the cellular interface |
| `PHONE DIAL number`, `PHONE ANSWER`, `PHONE HANGUP` | Explicit live call controls |
| `VOICE STATUS`, `VOICE TEST` | Bridge diagnostics or a three-second local audio-path check without calling |
| `SD INFO`, `SD LIST /path` | Card capacity and a bounded directory listing |

Queries use an exact allowlist; arbitrary AT writes and command chaining are
rejected. Configuration is blocked during calls and while cellular data owns
the modem. An uncertain SMS is never automatically resent. Timeout recovery
requires a tagged read-only response before normal commands resume.

## Voice setup on this engineering sample

**Set PCM voice clock** applies the verified sample's 16 kHz audio setting;
use **Restart modem** afterward. It keeps the modem in PCM slave/short-frame mode
with 16-bit linear samples and a 2.048 MHz bit clock. USB `PHONE CONFIG PCM16K`
applies it; `PHONE QUERY AT+QDAI?` should report `1,1,0,4,0,1,1,1`.
`PCM2048` restores the sample's original 8 kHz setting. These settings are not
automatically applied on boot. Do not use the lower-clock diagnostic settings
`PCM256` or `PCM1024`: both prevented call setup in the engineering trials.

`VOICE TEST` followed by `PHONE CONFIG PCM_TONE` exercises a short local modem
tone without dialing. A local tone and microphone samples do not establish a
working call. The owner separately confirmed two-way speech with the 16 kHz
configuration; see [hardware evidence](MP24-HARDWARE.md) and [validation](VALIDATION.md).

## Recon while moving

Security → Recon missions can record Wi-Fi/BLE observations and time on SD without
GPS. Those records have no measured coordinates and are not geolocated WiGLE
logs. The modem exposes a GNSS interface, but a position fix and the board's GNSS
antenna still need verification. Diagnostic `GPS_ON` / `GPS_OFF` settings and
`PHONE QUERY AT+QGPSLOC=2` support that check; no guessed locations are added.

## Sources

- [Mint APN setup](https://www.mintmobile.com/help/how-to-configure-android-settings/)
- [Quectel network/IMS configuration](https://quectel.com/content/uploads/2024/02/Quectel_EC2xEG2xEG9xEM05_Series_QCFG_AT_Commands_Manual_V1.0.pdf)
- [Quectel EG91 PCM electrical timing](https://forums.quectel.com/uploads/short-url/XtVJkuu4EMHOqjYkZ8fg98pX2k.pdf)
- [Quectel digital audio configuration](https://forums.quectel.com/uploads/short-url/w8Y0o7voqL5apyCaZtpszM0up1D.pdf)
- [Quectel GNSS application note](https://forums.quectel.com/uploads/short-url/zjzsqfCXEVzM5utCpHYw5sQ3OVG.pdf)

Private SIM identifiers, phone numbers, network credentials, captures and logs
remain outside the repository. See [validation](VALIDATION.md) for measured results.
