<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# NXDN data services

The experimental conventional NXDN data service carries Kenwood-compatible text. The wire references are
NXDN TS 1-A v1.3 (Common Air Interface) and TS 1-B v1.3 (Basic Operation).
See [NXDN](nxdn.md) for the common air interface, channel settings and voice behavior.

## 1. Implementation status

The `CONFIG_NXDN_DATA` switch defaults off and requires `CONFIG_NXDN`. The service supports clear, unconfirmed,
single-fragment Long Data Calls at 4800 and 9600 bit/s where the provider supports the selected rate and direction.
It uses the active conventional channel's transmit frequency, receive frequency, RANs and unit ID for simplex
and conventional repeater operation. Host tests cover the transport and the messaging UI. Over-the-air
Kenwood display and repeater forwarding remain unverified.

## 2. Transport

Use RDCH with USC `01`. LICH option `11` selects UDCH and `00` selects FACCH2. The subscriber transmits inbound.
The receiver accepts both directions and requires a consistent direction and RAN within a fragment.
TS 1-A Table 5.2-2 defines the LICH combinations, and TS 1-B §§4.2 and 8.1.4 describe the call sequence.

| Order | Channel | Operation |
|---|---|---|
| Preamble | Common air interface | Use the conventional preamble from [NXDN](nxdn.md#1-on-air). |
| Ten delay frames | FACCH2 | Send `HEAD_DLY` (`0F`) with the call type and IDs, counting from 9 to 0. The lead-in adds 400 ms at 9600 bit/s or 800 ms at 4800 bit/s. This countdown was observed in a Kenwood simplex capture. |
| Header | UDCH | Send `DCALL` header (`09`) with call type, rate option, IDs and packet information. The cipher and key fields are zero. |
| Data blocks | UDCH | `DCALL` user data (`0B`) carries 20 bytes per block. Frame and block numbers descend together to zero. The receiver accepts up to 16 blocks. Kenwood text transmit uses up to six blocks. |
| Release | FACCH2 | Send `TX_REL` (`08`) with the same call type and IDs. |

The 348 bits after LICH carry an eight-bit SR and 22 layer-3 octets through the shared UDCH/FACCH2 codec.
SR has two zero structure bits and six RAN bits. Append CRC-15 (initial `0x7FFF`, polynomial `0x4CC5`, MSB first)
and four zero tail bits. Use the existing K=5 convolutional generators, puncture G2 at positions 2 and 6 in each
seven input steps, and interleave in 29 rows of 12. Write by rows, read by columns, then apply the common air
scrambler. Validate CRC-15 before using SR or layer 3. See TS 1-A §4.5.2.3 and Figure 4.5-6.

TS 1-A §§6.4.1.3–4 and 6.5.19–21 define the header and block fields. For this profile, encode block count minus
one, the pad count, and the start-fragment flag. Clear the delivery, selective retry, circular and fragment-count
fields. Compute CRC-32 over all application bytes with initial `0xFFFFFFFF`, polynomial `0x04C11DB7`, MSB first
and no final XOR. Append the CRC big-endian, then pad with zeros to the next 20-byte boundary. Receive capacity
is 316 application bytes. Text transmit uses 120 bytes of staging and shares TX storage with voice. See TS 1-A §9.3.

Keep one bounded receive fragment and require an accepted header and an exact block sequence. Clear incomplete
state after a replacement header, failed block, early release, loss of sync or channel change. Use the declared
padding to locate the CRC, which can span two blocks. Deliver once after the final block verifies, with the
application bytes and their length. `HEAD_DLY` and trailing release frames produce no application delivery.
Apply the receive RAN and destination filters. Individual calls match the local unit. Group calls match the
effective receive group or group AllCall (`FFFF`). The standard ID range for source and individual destination
is 1–65519. Group destinations accept the full nonzero 16-bit range, including special groups and AllCall.

## 3. Kenwood text

An observed clear Kenwood simplex burst carries `83 00` followed by printable US-ASCII text. The first text
profile uses that exact prefix and accepts 1–100 bytes in `20`–`7E`, including leading and trailing spaces.
There is no additional length byte or terminator. The transport computes the CRC over the prefix and text
together. The recognizer receives the verified payload with CRC and padding removed. It checks the prefix
and the entire text slice before publishing a message. Enable `CONFIG_NXDN_DATA_DIAG` to retain the last
verified raw payload for bench diagnostics. Normal builds report TX status and RX counters.

The shared Messages list, reader, composer and notifications handle NXDN text. On an NXDN channel, **Write…** in
Messages addresses the configured channel destination. A contact action or reply selects a per-message destination.
Replies use the sender's unit ID. The shared inbox retains the four most recent DMR and NXDN messages in RAM,
replacing the oldest when full. The air provider hands received NXDN text to the UI through a bounded queue.
The composer limits Kenwood text to 100 printable bytes. **Sent** means the unconfirmed burst finished.
Local refusal or abort shows a send failure. See [debug-link diagnostics](debuglink.md#nxdn-text).

The captured prefix and layout cover the observed Kenwood mode. Record model, firmware, settings and direction
when testing display compatibility. Add other vendor formats after their wire layout is documented.

## 4. Not implemented

- Icom and other vendor text formats need documented wire layouts. Bare ASCII has no text recognizer.
- GPS position decoding and application forwarding over Bluetooth or serial have no delivery handlers.
- Trunked data, confirmed delivery and acknowledgments, selective retries, multiple fragments, keyed scrambling,
  and other short or simultaneous data services are outside the implemented profile.

The verified-payload interface should preserve raw bytes, length, source, destination, call type, RAN and direction.
GPS can publish a structured position event once the application discriminator and field layout are established.
Configured application data can use a bounded forwarding queue for serial or Bluetooth.

One retained decoder log reports a GPS position in a 38-byte application payload carried by an individual NXDN96
call. The retained descrambled bytes match the message CRC. Other calls in the same log share the transport
options and the first three application bytes, so those alone cannot identify GPS. The capture used keyed
scrambling, which the first profile rejects. Obtain controlled clear reports with known position and timestamp,
including a no-fix case, before defining a recognizer. Keep this evidence outside the firmware repository.

## 5. Verification

Host frame tests compare the header, countdown, data block and release with the Kenwood capture. They also
check the message CRC, special group IDs, and mixed-direction block rejection. Engine simulations cover both
rates. UI flows cover typed and canned sends, contact addressing, opening a received message, and replies to
the sender. The shared inbox tests cover capacity, read and delete operations, and stable handles.

The opt-in hardware TX test checks local burst completion. Bench verification still needs exact text in both
directions between RadioFoundry and Kenwood on simplex and through a conventional repeater. Decode the
repeater input and output independently, and record the countdown, destination, CRCs and display result.
