# Hacker Band — implementation and hardware status

Hacker Band is the planned watch companion to HackerPhone, developed in
[valleytechsolutions/HackerPhone](https://github.com/valleytechsolutions/HackerPhone).
It targets the owner's CircuitMess Maker Band engineering hardware. Manufacturer
product context: [MakerPhone 2.0](https://circuitmess.com/ca-en/products/makerphone-2-0)
and [Maker Band](https://circuitmess.com/products/makerband). Product pages do not
establish the pinout of this particular sample.

## First milestone — 2026-10-16

| Area | Implemented now | Still required |
|---|---|---|
| Watch | Validated UTC/timezone state; never invents a time when unset | Physical RTC driver, source of phone time, time-setting UI |
| Stopwatch | Start, pause, resume, reset; 64-bit elapsed time across `millis()` wrap | Button and watchface integration |
| Calls | Separate ignore and decline states, duplicate protection, stale-call guard, 15-second lost-link timeout | Authenticated BLE delivery, phone command integration, buzzer/driver identification, live call tests |
| Messages | Four-entry bounded notification inbox, duplicate suppression, display text sanitization | Phone SMS inbox decoding, BLE delivery and watch rendering; current text sanitation is printable ASCII |
| Matrix wallpaper | Fixed-memory rain animation with an interaction burst and bounded frame catch-up | Actual panel renderer, button/wrist interaction, frame rate and battery testing |
| Boot identity | Existing owner-supplied BWM source asset remains available in the shared repository | Reuse it on the identified band display; phone currently uses a static BWM splash for approximately 1.2 seconds |
| Companion protocol | Versioned packet envelope, exact length checks, replay/session checks | GATT service, payload handlers, secure pairing UI, reconnect and radio coexistence |
| Recon alerts | Bounded repeat-sighting filter and cooldown | Feed phone/band observations into it, share provenance and evidence, field validation |
| Hardware | ROM identified ESP32-C3 rev 0.4, 4 MB embedded XMC flash | Complete private recovery, partition map, display/RTC/button/alert/power pin evidence |
| Distribution | Source and build/test targets in the existing repository | Validated device releases and shared web flasher |

`hackerband_core` is a **compile-only foundation**, not working watch firmware.
It runs no radio service and assigns no peripheral GPIOs. The ordinary phone
application does not yet expose a Hacker Band pairing screen or BLE endpoint.
An authenticated-session flag in a portable test is not a Bluetooth security
implementation or a claim of tested pairing.

## Daily behavior

The home screen should remain a readable clock over dim green Matrix rain.
Buttons wake/brighten the screen and briefly react with the rain. Notifications
temporarily take priority, then return to the watch. Stopwatch timing is
independent of clock corrections and animation. A screen timeout is required;
continuous animated illumination and continuous Wi-Fi scanning are not the
default battery plan.

On an incoming call, one physical button **ignores** it: the band stops sounding,
the phone keeps ringing, and repeated updates for the same call do not unmute
the band. A separate button **declines** it: an authenticated command identifies
that specific ringing episode. Ignore must not prevent a later decline. A
decline is pending until the phone reports a result; a failed command must not
display “declined.” Ending a call or losing the connection clears the band alert.
Physical button names await board identification.

## Pairing and time plan

1. Select **Pair Hacker Band** on the phone and **Pair phone** on the watch.
2. Open a short discovery window and compare/confirm a code on both screens.
3. Use BLE LE Secure Connections with authenticated pairing and bonding. Reject
   unencrypted/unauthenticated notification writes and call-control commands.
4. Establish a fresh nonzero random session on each connection, then send valid
   phone UTC plus the current timezone offset. Keep RTC storage in UTC; apply
   the offset only for display. Resync the offset after DST/timezone changes.
5. Reconnect only to the saved peer outside the pairing window. Provide visible
   forget/re-pair controls and clear old notifications when forgetting a peer.

Proposed roles: phone GATT server, band GATT client. This lets the band find one
known service while the phone remains the cellular authority. The current phone
recon service initializes NimBLE with unauthenticated security defaults; that
must be refactored before it shares a stack with a protected companion service.
Pairing, normal recon, explorer connections and transmission tools need explicit
radio ownership. Calls and the companion connection take priority over scans.

The phone currently has no validated wall-clock source. Add modem/network-time
parsing or explicit time provisioning before promising automatic RTC setup.
An unset phone clock must not overwrite a valid band RTC. The portable clock
accepts 2020–2099 UTC timestamps and offsets from UTC−12 through UTC+14. Its
adapter must tick at least once per 32-bit `millis()` period, and reseed from the
RTC after reset/deep sleep; the core alone is not a persistent clock.

### Packet envelope v1

All integers are little-endian. This envelope runs **inside** authenticated BLE;
it is not encryption or a substitute for pairing.

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 2 | ASCII `HB` |
| 2 | 1 | Version 1 |
| 3 | 1 | Kind: time, ring, ended, message, observation, ignore, decline, result |
| 4 | 4 | Fresh nonzero connection session |
| 8 | 4 | Strictly increasing nonzero sequence in this direction |
| 12 | 2 | Payload length, maximum 144 |
| 14 | 0–144 | Payload |

Unknown kinds/versions, truncated or oversized frames, trailing bytes, wrong
sessions and repeated/backward sequences are rejected. Reconnect before sequence
wrap. A full frame requires ATT MTU of at least 161; the transport must negotiate
that or add explicit bounded fragmentation. Do not assume the default ATT MTU
can carry these frames. Payload schemas and acknowledgments remain to implement.
The phone must recheck session, current call identifier, ringing state and command
age immediately before issuing `AT+CHUP`; no modem work belongs in a BLE callback.

## Counter-surveillance behavior

The phone already has evidence parsers for Axon/TASER, camera-glasses clues,
Ring/camera vendors, Flipper, trackers and broadcast drone Remote ID. Reuse
the pinned parsers and their attribution. The band should contribute brief
passive BLE observations, with optional short Wi-Fi scans; the phone should do
the larger classification/history view. Neither device should transmit attacks
as part of a watch alert.

Each shared observation needs family, radio, confidence, evidence, age and source
(phone or band). Deduplicate repeated observations without presenting two radios
hearing one advertisement as independent proof. The initial gate requires three
medium/strong observations at least one second apart within a continuous sighting
window, then applies a global one-minute alert cooldown. Weak names remain visible
as clues without an urgent default alert. These policy values still need field use.

A signature indicates a possible device family, not malicious intent or active
recording. Arbitrary names/MACs can be spoofed. A quiet/offline camera cannot be
discovered by these radios; neither ESP32 variant covers every radio band or drone
protocol. RSSI does not establish distance or direction. No nearby-device captures
or identifiers are included in the public source.

## Recovery and hardware work

ROM identification succeeded. Stub-based flash reads failed before producing a
complete recovery image; the USB device subsequently disappeared from enumeration.
A ROM-only flash-ID read identified 4 MB flash. No band flash writes were performed.
The USB fault is not yet diagnosed; do not infer a bad board from this observation.

Once the band appears reliably in the loader, use the project's Python:

```text
python tools/backup_band.py --port <band-port> --expected-mac <band-ROM-MAC>
```

The read-only helper checks chip, expected identity and flash size, reads flash
twice without a stub, and requires matching SHA-256 hashes. It writes only to
ignored `.work/recovery/hackerband/`; it never erases or writes device flash.
It leaves the original device in its loader for a physical reset. This alternate
read procedure remains unverified on the sample until USB access returns.

Next recover the partition map and original startup diagnostics. Establish panel
controller/orientation/SPI pins; RTC part/address/I²C pins; buttons/polarity;
speaker/buzzer/motor presence and driver; battery ADC/charger; backlight and wake
behavior. Do not borrow the original Clockstar, Clockstar v2 or phone pinout.
Keep recovery, extracted vendor firmware and disassembly private. Only factual
hardware assignments and independently written adapters belong in source.

## Shared web flasher release requirements

The requested flasher is one page in this project with separate **HackerPhone**
and **Hacker Band** selections and separate manifests. It follows hardware
validation; no public installer or downloadable band image is available now.

- Identify the chip and supported board/partition profile before enabling a write.
  ESP32-S3 or ESP32-C3 identity alone is not sufficient board identification.
- Produce application images from the public build, with sizes and SHA-256 hashes.
  Never package a recovery dump, vendor extraction, NVS, pairing keys or captures.
- On the MP24 sample preserve the existing bootloader, partition table, NVS and
  SPIFFS. Write only the validated application at `0x10000`; never distribute its
  generic `firmware.factory.bin` as an installer.
- Establish the band layout independently. Do not assume its offset or partitions
  match the phone, and do not enable install while the profile is unknown.
- Validate recovery, boot, panel, buttons, battery/wake, authenticated pairing,
  time sync/reconnect, live call ignore/decline, SMS and scan coexistence before
  advertising a usable paired release.

## Verification

Local results on 2026-10-16: all 28 existing phone/recon test groups and the
new band suite passed, including 40,000 malformed/random inputs in each suite.
Both `mp24_sample` and `hackerband_core` compiled successfully. An attempted
PlatformIO upload using an intentionally nonexistent port was rejected by the
compile-only guard before serial access. The recovery helper passed Python syntax
and argument-help checks, but a full band backup has not succeeded.

`python tools/test_core.py` runs the existing phone/recon checks and the added
native band suite: packet bounds and malformed input, session/replay guards,
clock/stopwatch rollover, ignore then decline, stale call IDs, disconnect timeout,
bounded inbox, alert cooldown and Matrix animation bounds. It never opens a
serial port or places a call. Synthetic tests do not establish real pairing,
notification delivery, detection accuracy, RTC operation or battery life.

Build both targets with:

```text
python -m platformio run -e mp24_sample -e hackerband_core
```
