<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# NXDN data transport

`CONFIG_NXDN_DATA` enables the experimental conventional data transport and defaults off.
It requires `CONFIG_NXDN`. See [NXDN](nxdn.md) for channel selection and the common air interface.
The transport carries opaque user bytes for future application handlers.

## Terminology

In RadioFoundry's conventional NXDN work, Kenwood's **Short Message** means the user-facing
text messaging feature. On-air captures of Kenwood text in simplex and through a conventional
repeater show a conventional Data Call (`DCALL` on UDCH), called **Long Data Call** in TS 1-B
§8.1.4. Use the decoded frames to identify the bearer. Reserve **NXDN Short Data Call** for the
specific trunking service described in TS 1-B §5.1. RadioFoundry's NXDN implementation is
conventional.

## Wire profile

NXDN TS 1-A v1.3 §§4.5.2.3, 4.5.5, 4.6, 5.2, 6.3, 6.4.1.3–6, 6.4.1.8, 6.5.19–21 and 9.3 define the
frame, messages and CRC. TS 1-B v1.3 §§4.2, 5 and 8.1.4 define the conventional sequence.
Support unconfirmed, single-fragment Data Calls at either configured NXDN rate.
Clear calls are always supported. Keyed data uses the 15-bit NXDN Scrambler when
`CONFIG_NXDN_SCRAMBLER` is enabled.
Use RDCH with USC `01`. Option `11` selects UDCH and `00` selects FACCH2. Receive both
directions. Transmit as a subscriber with inbound LICH.

The burst is a preamble, optional FACCH2 header-delay frames (`0F`), a UDCH header (`09`),
one to sixteen UDCH data blocks (`0B`), and a FACCH2 release (`08`). A ten-frame lead-in
counts from 9 to 0 and uses the same RAN, source, destination and call type as the data call.
Incoming FACCH2 header-delay frames do not begin a fragment.
Release is optional for receive completion. UDCH and FACCH2 share a 348-bit codec carrying
the SR byte, 22 message bytes, CRC-15 and convolutional tail bits. The SR supplies RAN.

An individual destination must be the local unit. A group must match the configured receive
group or AllCall. RAN zero on receive accepts any RAN, while all frames of a fragment must
agree on its actual RAN and direction. Trust fields only after channel CRC-15 passes.
The frame and block numbers both descend to zero. Remove the declared padding, then check
the big-endian CRC-32 over the user bytes. The checksum can cross a block boundary.
Deliver the verified bytes once. A new header, broken sequence, early matching release,
loss of sync, retune or mode exit discards an incomplete fragment.

## Keyed data

Transmit uses the active channel's configured cipher and key ID. Receive uses the cipher and
key ID in the CRC-verified `DCALL` header. Both directions use the existing conventional
system key lookup. An unsupported cipher or missing key prevents transmission or delivery.

For keyed TX, append the message CRC-32 and pad the fragment to whole 20-byte blocks.
Seed the shared 15-bit generator once for that fragment and XOR the resulting bits into
the packed byte stream, MSB first, including the CRC and padding. Then encode the UDCH
blocks and their channel CRC-15. For RX, validate the channel CRC-15 and collect the blocks
before applying the same transform. Remove the padding and verify the message CRC-32
before delivering plaintext. These rules use the generator in TS 1-D v1.3 Figure 5.3-1
and the UDCH vector in §7.2.2.1. Three first-party conventional repeater bursts confirmed
continuity across blocks and the scrambling of CRC and padding. The header, block
numbering, header delay and release remain readable through the common channel codec.

## Radio interfaces

- `radio_sm_nxdn_data()` runs on the radio task and takes a byte span and destination.
  Destination zero selects the channel's configured destination. The normal radio TX admission
  and provider state machine apply. The span is copied to staging storage. A second request
  cannot replace a staged or active send. A successful return indicates local TX start.
  Receive capacity is 316 user bytes. The 120-byte TX staging space accommodates 116 user
  bytes plus CRC before rounding to whole blocks.
- `radio_sm_nxdn_data_ex()` adds a per-send `head_delay_frames` setting of 0–256. The
  original entry point sends zero lead-in frames. Callers can pass a non-null ticket pointer
  to track terminal status. The function writes a nonzero ticket only after local TX starts.
- On the radio task, poll `radio_nxdn_data_take_tx_result(ticket, &result)`. It consumes a
  terminal result once. While a tracked result is outstanding, other data sends are refused
  so it cannot be overwritten. A rejected send leaves both the active request and the ticket
  output unchanged. `RADIO_NXDN_DATA_COMPLETED` means the provider started TX and reported
  completion after the full burst was queued, with no underrun or abort. Provider failure,
  cancellation or the radio's TX time-out gives `RADIO_NXDN_DATA_FAILED`. Unconfirmed data
  calls provide no remote delivery acknowledgment.
- `radio_nxdn_data_pop()` is the single-consumer handoff for future feature dispatch.
  It returns a verified payload and its length, source, destination, call type, rate, RAN,
  direction and block count. Its one-slot mailbox keeps the older pending packet if full.
  Application recognition runs after this handoff.

The data bearer does not use the voice codec. DES, AES, confirmed calls, selective retry,
multiple fragments, trunked Short Data Call, and application handlers are outside this profile.

## Verification

`nxdnTest` covers the shared codec, the captured ten-frame countdown, packet CRC and assembly,
and mutated receive frames. `nxdnEngineTest` covers the zero- and ten-frame burst sequences,
provider completion, abort and underrun. `radioCoreTestNxdn` and `radioCoreTestP25Nxdn` exercise
normal admission, busy rejection, terminal result consumption, TX time-out and the receive mailbox
on simulated hardware. `nxdnScrambleTest` checks the packed-byte transform against the
published UDCH vector. `nxdnEngineTest` checks an independently generated three-block
vector with a split CRC and scrambled padding, plus success, wrong-key and missing-key
cases at both rates. On-air validation of this implementation is pending.
