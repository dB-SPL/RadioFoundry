<!-- SPDX-License-Identifier: MIT -->
# Radioddity GD-88 (Kydera DR-880UV) hardware

Status: bring-up. RadioFoundry runs on both SoCs (`boards/gd88`, `boards/gd88b`; section 13). Sections 1-5 record how the GD-88 programs and updates (a USB-serial cable, not native USB). Sections
6-12 are the hardware facts the port is written from. The survey entry is in [radios.md](radios.md); the HR_C7000 facts it builds
on (UART registers, USB core, clock gates) are in [hardware-dm32.md](hardware-dm32.md).

**Answer: a USB-serial cable.** The radio body has no USB connector. The PC link is a K1 (2.5 mm + 3.5 mm) jack,
and the cable carries a USB-to-UART converter chip. SoC A's **UART1** at 115200 baud is the PC link. The SoC's USB
controller has no connector to reach (section 3).

## Confidence tags

| Tag | Meaning |
|---|---|
| **DS** | HR_C7000 user guide (2016 user manual and the V2 software application manual) |
| **UNV** | Not yet checked on a radio |
| **COM** | Public material: Radioddity manuals, release notes, update guide, product page |
| **INF** | Inferred from the above; not seen directly |
| **HW** | Seen on a radio |

The update files are plain, headerless images that load at 0x0300C000, the same application base as the DM-32UV.

---

## 1. SoC pair and roles

| Item | Finding |
| --- | --- |
| SoCs | Two HR_C7000 (C-SKY CK803S core each), called A and B, with the same start-up as the DM-32UV and the HR_C7000 vocoder library in both |
| A | Master: display, keys, GPS, FM broadcast receiver, menus, the PC programming protocol, and resetting B |
| B | Second transceiver (VFO B). It has no GPS and no PC link of its own |
| A↔B link | UART3 (0x14090000) at 115200 on both chips, interrupt vector 45 on both |
| GPS (A only) | UART2 (0x14050000), interrupt vector 43, 9600 baud; the receiver takes a port-configuration sentence (section 9) |
| UART0 (0x14030000) | Not used. On the DM-32UV this is the programming-cable UART |
| Storage | One SPI NAND per SoC, TC58 family (Toshiba TC58CVG1S3HRAIJ) (unverified) |

UART vectors follow the PIC table: UART1 source 10 / vector 42, UART2 11 / 43, UART0 12 / 44, UART3 13 / 45,
USB 21 / 53 (datasheet).

## 2. Programming and update link

| Item | Finding | Tag |
|---|---|---|
| Radio connector | K1 jack on the right side of the GD-88. Radio RXD (data to radio) shares the 3.5 mm sleeve with PTT. Radio TXD (data from radio) is on the 2.5 mm ring. GND is on the 2.5 mm sleeve. The 3.5 mm tip carries +5 V through 100 Ω (not a supply) | COM (extended manual v3.4, 11.1 and 17.5) |
| Other connectors | SMA antenna socket and a 2-pin spring battery contact. The radio charges in a desktop charging base. Neither the product page nor the manual mentions a USB or Type-C port on the radio or the battery | COM |
| Cable | Radioddity: "a small chip that converts signals to and from the USB-side into serial signals to and from the K1 / RJ45 style connector". It appears on the PC as a virtual COM port, and a driver download is offered if Windows does not load one. The DB40-D box lists the cable as "FTDI-based"; the GD-88 box list says only "Programming Cable". Firmware and CPS release notes since 2022-10 add "support for programming cable based on FTDI-chip" | COM |
| Which SoC | A, UART1 (0x14040000), interrupt vector 42, 115200. The PC protocol uses the DB25-D family's command words (section 6) | UNV |
| Baud | 115200 8N1. The update guide's tool offers 115200 and 38400 | UNV, COM |
| Electrical level | Not stated for the K1 jack. The DB40-D's RJ-45 programming port, on the same platform, is documented as 3.3 V, 115.2 kbaud | COM; INF for the GD-88 level |
| Speaker with the cable in | The internal speaker still plays with the programming cable plugged in: the power-on tone sounds. The plug doesn't switch the speaker off, so beeps and receive audio can be heard while the debug link runs. Whether the plug cuts the internal microphone is not known | HW; mic unknown |
| B's own UART1 | Whether it is wired to the K1 jack (directly or through a switch), to test pads, or to nothing is unknown | INF |

### PC tools

The update tool and the CPS are Windows programs that use the cable's virtual COM port (COM: the update guide and the
manual, which says the cable holds the converter chip).

## 3. USB controller use

**None.** The radio has no USB connector, and RadioFoundry leaves the HR_C7000 USB core off on both SoCs (the clock
manager's USB gates, CLK_MGR 0x02C bits 16/17, stay off; the 48 MHz USB divider, 0x028, is not set).

### SoC pins (datasheet)

The USB data pins are the dedicated balls F1 (USB_DP) and F2 (USB_DM), with USB_AVDD33 on G3 (as on the DM-32UV). The
2016 user manual's pin-mux table also lists USB_DM / USB_DP as function 4 of PTA13 / PTA14 (balls L16 / M14, which are
UART3 RXD / TXD), but the IO-mux register for those pins marks the matching value 2 as reserved (conflicting). On the
GD-88 those balls carry the A↔B link, so they are not available for USB.

## 4. IAP bootloader entry

| Item | Finding | Tag |
|---|---|---|
| Entry | Radio off, cable connected, CPS closed. Hold the upper side key SK1 (P2 in the manual) and turn the radio on with the volume knob. The status LED lights red and a special screen shows "IAP_A" or "IAP_B" on the top line. Then release SK1. It can take more than one try | COM (update guide v2.2, release notes 2024-06) |
| A or B target | In IAP mode, long-press SK1 to switch between IAP_A and IAP_B. The A file goes to IAP_A and the B file to IAP_B. Write the codeplug after updating both | COM |
| Bootloader code | Not in the update files: they start at the 0x0300C000 application base, so the bootloader sits below it, as on the DM-32UV. How IAP_B reaches SoC B (A relaying over the UART3 link, or the cable's lines reaching B's UART) is not known | INF |
| How IAP_B may reach B | The update tool has no A or B choice: the target is picked on the radio only. Both SoCs' UART1 pads are PTA9 (RXD, also the K1 PTT line) and PTA10 (TXD); released, they are GPIO inputs (IO_DIPLEX0 bits 9 and 10 set). RadioFoundry's B releases them at start-up. The cable's two lines may reach both SoCs, with one owner at a time, so that in IAP_B B's loader answers the cable itself; A relaying is the other candidate. A continuity check from the K1 pads to both SoCs, or a `Flash Read ` probe in IAP_A and in IAP_B, settles it | INF |
| ROM serial boot | The HR_C7000 boot ROM offers serial (UART0) boot or SPI-flash boot, no USB boot (DS). The datasheet names UART0 as the debug and boot-upgrade port. Where A's and B's UART0 balls go on the GD-88 board is unknown | DS, INF |

## 5. Open questions

1. Is the K1 cable wired to A's UART1 directly, or through a switch that the bootloader also points at B when
   IAP_B is selected? Section 4 points to both SoCs' PTA9 / PTA10 on the same lines. Needs a board photo or a
   continuity check with the radio opened (no live probing).
2. Which converter chip is in the GD-88 cable as shipped (FTDI per the DB40-D box list and the release notes;
   other batches unknown)? Plugging the cable into a computer and reading its VID:PID answers it, with no radio needed.
3. The K1 data level (3.3 V like the DB40-D's RJ-45, or something else).
4. Where the UART0 balls of A and B go (test pads?), for ROM serial-boot recovery.
5. Is there any USB data path at all, for example test pads on the F1/F2 nets? Nothing public suggests one. The
   FCC filing 2AN62-GD88 internal photos were not reachable.

## 6. Start-up, clocks and the PC link

| Item | Finding |
|---|---|
| Start-up | As on the DM-32UV: VBR 0x00010000 (vector table in SRAM), cache controller 0xE000F000 (cacheable 0x03000000, size code 0x2D) |
| Clock modes | 24 MHz oscillator, 168 MHz (PLL 24 MHz / 1 x 28 / 2 / 2) and 192 MHz (24 MHz / 2 x 64 / 2 / 2). The XIP flash divider is 2 at 168 MHz and 4 at 192 MHz. Both SoCs alike |
| PC command words | `Erase`, `Flash Write`, `Flash Read `, `Record Read` (first 11 bytes), the same words as the DB25-D. `Record Read` returns an identification block: firmware version string, build date, build time, `+DRS-880plus`, `+136-480MHZ+` and two 6-byte fields |
| USMART console | A build `.021` has the USMART V3.1 debug console; older A builds and the B builds don't. On the DB25-D RadioFoundry loads its RAM stub through the same console without the bootloader ([hardware-db25d.md](hardware-db25d.md)). On the GD-88 that route is not checked on a radio yet, so the web flasher names the build but loads nothing through its console, and offers update mode only |

## 7. Storage

| Item | Finding |
|---|---|
| NAND | SPI NAND on SPI0, chip select PTA18, standard SPI-NAND opcodes; geometry is read from the chip at run time. Each SoC has its own. Its unique ID page is the SoC's UID for INFO_CHIP ([debuglink.md](debuglink.md) 3.9) |
| Factory data | The factory data lies below logical 0x420000 (the calibration page at 0x280000); the rest of the 2048 blocks is free but for the spare pool at the top ([spi-nand-lut.md](spi-nand-lut.md)). RadioFoundry's settings take blocks 1920-1951 |
| Calibration | Primary copy is one 2 KB NAND page at logical 0x280000 starting with `TUNE` and a 32-bit counter (higher = newer). The address is logical: the block map in block 0 places it ([spi-nand-lut.md](spi-nand-lut.md)). On the bench's A, bad blocks 186, 1536 and 1537 and the map put the page at physical 0x2A0000 (block 21). A backup copy sits in program flash at 0x031FE000 (4 KB). Each SoC has its own pair, with its own values: A's page sets A's transmitter and the PA drive B applies, B's page B's receiver (**HW** both pairs read through the map: A counter 1285, B 1427). RadioFoundry reads both through the map and never writes them. Items 0x602 (4 bytes), 0x606, 0x607 (1 each), 0x608 and 0x60B (3 each), past the DB25-D's layout, are 0xFF on both of the bench radio's pages (**HW**). Not used |

## 8. Memory map and the codec

| Item | Finding |
|---|---|
| Program flash | 2 MB at 0x03000000 on each SoC. The IAP loader is below 0x0300C000. RadioFoundry's first stage takes 0x0300C000-0x0300D000 and the application starts at 0x0300D000 ([boot/README.md](../../boot/README.md)) |
| Data in program flash | The display record at 0x031FD000 (`lcd` and a type byte) and the calibration copy at 0x031FE000 (section 7) |
| Codec slot | 0x030E8000-0x03100000 on each SoC. Both SoCs run A's codec, merged on the user's machine from A's update file (`tools/hrc7000_codec.py`, boards `gd88` and `gd88b`). B's slot is byte for byte A's: the slot header names the run address and the tables address, and both are the same on the two SoCs. The user supplies only A's file, or the web flasher reuses the slot it wrote to A when it installs B through A's bridge. B's own update file is still accepted for B (`[codec] from` in `boards/gd88b/board.toml`) |
| Codec RAM | The code runs at 0x44E00-0x58000. Its tables go to modem SRAM: A's builds use 0x18001204-0x18004324, B's 0x18000B40-0x18003B10. A links with A's window. B links with the union, 0x18000B40-0x18004324, so that it takes A's codec or its own. The only other modem SRAM use is the crash record at 0x18007EF0 |
| SRAM | 288 KB at 0x10000 |

## 9. SoC A pins and chips

Pin index 0-31 = PTA, 32-63 = PTB, 64-95 = PTC.

| Function | Pin / bus |
|---|---|
| Key matrix | 6 rows PTB18-23 (outputs, idle low) by 3 sense lines PTB9-11 (inputs). Plus three keys read directly on the sense lines (9.2); 20-tick debounce |
| Power key / latch | PTB12 in (high = pressed); PTB13 out, high keeps power on (9.1) |
| Rotary encoder | PTB5, PTB8, both edges: one event +1 or -1. PTB5 leads PTB8 clockwise; a detent rests with both lines equal (both high or both low), two steps each (**HW**). The GPIO B interrupt was lost for good after a few turns once (both lines pending, never taken), so the 10 ms key scan serves the bank when that happens (the HR_C7000 platform's GPIO interrupt watch, `platform/hrc7000/gpio_watch.c`) (**HW**) |
| TX / RX line | PTB2, high = transmit. The modem's RF TX and RX interrupts also switch it, so on DMR it follows every burst. It is wired to B's PTB2 too: B keys the PA on its edges (section 10) |
| Speaker amplifier | PTC18 high, together with the serial chain's amplifier bit (9.3). On while FM receive audio is open or the modem plays PCM (beeps, DMR voice), off in FM TX. The modem's ring play goes through the HR_C7000 TX gate (`hrc_audio_pcm_ring`, modem-hrc7000.md 8.4). Set directly, the gate's reopening left ring play off while the amplifier stayed on, and the speaker hummed after a DMR over (**HW**). Diagnostics: `spk a<amplifier> o<FM audio open> p<PCM route> r<ring play>`, the line after the TX timing line |
| PTC20 | Low cuts the modem's audio line while a beep plays (INF). Low at start-up |
| Volume level | One level for both paths: 3 dB a step, 0 dB at the top, 45 dB down at the first step. The speaker amplifier is loud: 24 dB down was still loud in a room (**HW**). FM: the main chip's AF gains (0x48 AF Rx Gain2, 0.5 dB a code, 0x3F down to 0x03: 0x01 mutes; then Gain1 below it, 6 dB a step, down to -18 dB; 48 dB in all), with its AF DAC gain held at 2 and the level 3 dB below DMR's, so one speech clip plays about as loud on both (DMR decoding to -6 dBFS peaks, FM at 2.5 kHz peak deviation on 12.5 kHz; **HW**, by microphone). The DAC gain codes are close to linear in amplitude, not in dB (0 to 1 adds about 9 dB, 14 to 15 under 1 dB): stepped as volume, it jumped. Beeps and DMR voice come from the modem's line out, not the main chip's AF: the codec's DAC gain (GCR_DACL) down to its -31 dB floor, then a gain on the samples. Decoded DMR voice is close to full scale: at the speaker its level stops following the gain above about +6 dB, and it distorts from 0 dB up (**HW**). The loudspeaker also needs B's switch closed (section 10) |
| Beep level | Beeps don't follow the volume (`hal_audio_t.spk_beep`): while the line out carries beeps only, its gain is `GD88_BEEP_DB` (-18 dB) and the core plays them at `beep_level` (none at volume 0). The amplifier's switching noise is about as loud as a quiet beep, so beeps that followed the volume down were lost in it. Tone volume 1 (level 10) at -18 dB is the level a beep had at level 38 and -30 dB (volume step 6, Tone volume 5): untested on the radio |
| PTT / K1 data | PTA9, shared with UART1 RXD; low = PTT. On transmit, FM and DMR, with the programming cable in, the transmission disturbs this line (**HW**), most in the 70 cm band. The host UART drops each character with a line error ([gotchas 2.13](gotchas.md)); the characters that still arrive are noise, which an idle port drops. At lowest power, a host polling through a key-up loses about 7 requests in 8 at 438 MHz, from about 0.2 s after key-up until the transmission ends, about 1 in 170 at 446 MHz and none at 146 MHz. Requests between transmissions are not affected |
| UARTs | UART1 PTA9/10 PC cable (115200); UART2 PTA11/12 GPS (9600); UART3 PTA13/14 link to B (115200) |
| GPS power | PTB17, high = on. On start-up the receiver gets `$CFGPRT,,0,9600,3,3` |
| LCD | 320x240 on the i8080 controller (0x12000000), reset PTC2 (low 10 ms, high 130 ms, twice). Four init variants (9.4) |
| Backlight | PWM channel 0 on PTA15, 1 kHz. The keypad light (red) is on PTA16 (PWM channel 1), high = on (**HW**); RadioFoundry switches it as a plain output, on only while the backlight is at the set brightness |
| NAND | SPI0, CS PTA18 |
| RTC | internal I2C2, 7-bit 0x70 |
| IMU | bit-banged I2C PTA7 (SCL) / PTA8 (SDA), 7-bit 0x68, MPU-6050 class |
| FM receiver | RDA5807 class on the same bit-banged pair, 7-bit 0x11. No enable line; its audio is gated by PTC18 |
| RF chips | bit-banged 3-wire: SCLK PTB16, SDIO PTB24, main chip select PTB25, second chip select PTA19; status lines PTB26/27 |
| Output expander | 64-bit serial chain: clock PTB0, data PTB1, latch PTB3 (rising edge). Carries the audio routing only (9.3). PTB0 / PTB1 are not LEDs |
| Band path | The LO BK4819's GPIO4 / GPIO5 (register 0x33): VHF = GPIO5 high, GPIO4 low; UHF the reverse. A has no band, PA or TX supply GPIO |
| PA drive | None on A: the PA, its supplies and its drive DAC are B's (section 10). RadioFoundry writes register 0x36 of the main BK4819 once at start-up (0x00BF) |
| ADC | Channel 0 microphone, 1 volume knob, 2 battery |
| Status LEDs | On SoC B (section 10). A switches them over the link |

### 9.1 Power and battery

| Item | Finding |
|---|---|
| Power key | PTB12 reads high while pressed. The radio is switched on with the volume knob, so PTB12 is most likely the knob's switch contact (INF). RadioFoundry latches power on PTB13 when it reads high, and switches off when the key is pressed again or the battery reading drops |
| Battery | ADC channel 2, 2S Li-ion. V x 10 = (raw + 5.7818) / 7.8551. Below 6.0 V critical, 6.0-6.9 V warnings, 7.0 V and up normal. A raw reading below 50 means the supply is going: switch off |
| Volume | ADC channel 1, an audio taper pot: 0 at the lowest end, 59 at a quarter of the turn, 129 at half, 671 at three quarters, 1010 at the highest (**HW**). Linear between those points (`drivers/power/knob`), with the ends at 5 and 980 so a knob that stops a little short still reaches 0 and 100 %, then 21 levels of 5 % with hysteresis, so each quarter of the turn is a quarter of the volume |

### 9.2 Keys

The matrix code is the key's scan number. The front panel has six keys besides the digits: two blank soft keys
under the display, green and red, and an up/down rocker. The soft keys' functions show on the screen's bottom row,
left and right. The sense lines are the digit columns (PTB9 left: 1 4 7 `*`, PTB10 center, PTB11 right). The
RadioFoundry key names (`KEY_*`) are the front panel's. Each front key's code was checked on the radio
(**HW**). RadioFoundry makes the soft keys OK and Back.

| Code | Name in the manual | Key (RadioFoundry) | Where | Source |
|---|---|---|---|---|
| 8-17 | digits 0-9 (8 = 0) | `KEY_0`-`KEY_9` | matrix | UNV |
| 18, 19 | `*`, `#` | `KEY_STAR`, `KEY_HASH` | matrix | UNV |
| 5 | navigation | `KEY_SOFT_LEFT`: at idle opens the main menu; Menu + `*` unlocks the keypad | PTB9 x PTB23 | HW |
| 6 | navigation | `KEY_SOFT_RIGHT`: at idle swaps VFO A and B | PTB11 x PTB23 | HW |
| 1 | navigation | `KEY_UP`: at idle the channel up | PTB10 x PTB23 | HW |
| 2 | navigation | `KEY_DOWN`: at idle the channel down | PTB10 x PTB22 | HW |
| 3 | P4 | `KEY_GREEN`: the programmable P4 (its function from the codeplug) | PTB9 x PTB22 | HW |
| 4 | mode key | `KEY_RED`: in a menu, back one level | PTB11 x PTB22 | HW |
| 22 | P2, the IAP key | `KEY_SK1`, upper left side key, above PTT | PTB9 direct |
| 23 | P1 | `KEY_SK2`, lower left side key, below PTT | PTB10 direct |
| 24 | P3 | `KEY_SOS`, the orange top key | PTB11 direct |
| - | PTT | `KEY_PTT`, left side between SK1 and SK2 | PTA9, low = pressed |

### 9.3 Audio routing in the serial chain

The chain is eight bytes. Byte 0 bit 0 is shifted first; a rising edge on PTB3 latches it. There are no RF bits.

| Bytes | Bit | Meaning |
|---|---|---|
| 0 (speaker), 1 (headset) | 0-3 | the source: bit 2 x (class - 1) + vfo. Class 1 is analog FM, class 2 DMR (INF; HW for vfo 0: A's FM noise plays with bit 0 alone, the modem's beeps with bit 2 alone). Vfo 0 is A's receiver, 1 is B's |
| 0, 1 | 5 | amplifier enable |
| 0, 1 | 6 | another source; RadioFoundry uses it for the broadcast FM tuner (INF) |
| 2-5 | 4 | the receive path in use, one-hot: byte 2 DMR A, 3 DMR B, 4 FM A, 5 FM B |
| 2-5 | 7 | the other VFO during TX |
| 6, 7 | 0-3 | the transmit route, the source bit of the transmitting class and vfo (byte 6 at volume 6 or less, else byte 7) |

B's audio reaches the speaker through these bits with vfo 1. With no receiver's squelch open, the source bits and the
amplifier bit are clear in both bytes and B's speaker switch (section 10) is open. An amplifier left on while B mutes
its source plays the noise of an undriven line.

### 9.4 Display

The panel record at 0x031FD000 holds `lcd` and a type byte. A panel that answers ID 0xC505 to command 0xD3 is
variant 1. Without a record RadioFoundry uses variant 3.

## 10. SoC B pins

B has no display, keys, power latch or GPS. Pin index as in section 9.

| Function | Pins |
|---|---|
| Clock | RadioFoundry keeps B at its loader's 72 MHz (APB 36 MHz) and doesn't reclock it. At 72 MHz the link ran with no bad frames, and FM and DMR keying for A held every PA drive point on time (**HW**). DMR receive and B's voice decode at 72 MHz are untested |
| RX band | UHF: PTB8 high, PTB7 low. VHF: the reverse |
| RX front end | UHF PTB15, VHF PTB14, high = on. **Shared**: A's receiver needs its band's front end too |
| RX path | **HW** A: PTB1, B: PTB0, high = on, in FM and DMR alike; neither receiver cares about the other's. B's reading of a bench signal rises 53 dB with PTB0 (-108 to -55 dBm), and without it B's floor is about -120 dBm. PTB13 and PTB5 (paired with PTB1 and PTB0 on the board), and PTB11 / PTB12 change neither receiver: held low. All off while transmitting |
| A's receiver | **HW** bench: with PTB15 + PTB1 low A hears nothing (noise floor -127 dBm); high, the floor is -86 dBm and a bench signal shows. VHF the same with PTB14. The band switch (PTB8 / PTB7) and PTB13 make no difference to A, nor to B. B sets them from A's `PATH` messages ([rx2-link.md](rx2-link.md)) |
| Audio source to A | mode 0: PTC18 high, PTB4 low; mode 1: the reverse. Held at mode 0 (A's chip) while A transmits (UNV) |
| Speaker switch | **HW** PTA15 high and PTB9 low close a switch in A's loudspeaker path; PTA15 low and PTB9 high open it, which leaves A's speaker silent. RadioFoundry: A sends `SPEAKER` ([rx2-link.md](rx2-link.md)) and holds it 1 s after the last sound. On the bench: A's microphone hears the receiver's noise with the monitor on only while it is closed |
| Audio band | VHF: PTB18 high, PTB22 low; UHF: the reverse. Set to A's TX band while A transmits (UNV) |
| TX path | UHF PTA17, VHF PTB10: B's own transmitter, for transmitting on B's chip. With it, per burst, PTB4 high and PTC18 low, and A's main BK4819 GPIO5 high (B's chip to the PA) and GPIO4 high for VHF (register 0x33). Without GPIO5 the PA keys with no signal; held high between bursts it deafens B's receiver (**HW**) |
| A's VHF signal to the PA | **HW** B's main BK4819 register 0x33 bit 5 (GPIO pin 1 as `bk4819_gpio` numbers them, output enabled): high for A's VHF transmission, low for UHF; bit 6 low (A's chip, not B's). Without it a VHF key showed only a leak about 70 dB under the level, in FM and DMR, with every PA pin and DAC as on UHF; with it the VHF curve follows UHF's. `pa_gd88b.c` sets it at key-on, on a band change only |
| PA select, then supply | UHF PTB6, 200 us, then PTB20. VHF PTB3, then PTB21. TX off clears the supplies first |
| A's TX line | PTB2 in, both-edge interrupt: A's PTB2 (section 9) |
| A's PA | **B keys the PA for A's transmitter** (**HW** FM). A sends B the drive and the page's delays first (`RX2_TX`), then raises PTB2. Rising edge: mode 0, the TX band pair, A's RX path PTB1 (and PTB13) off, 50 us, the TX-on delay, PA select, 200 us, supply, DAC B ramped up to the drive (180 steps of 4 us, S-shaped). Falling edge: the TX-off delay, the ramp down, supply then select off, 300 + 1000 + 300 us, the RX front end and A's RX path back, 200 us |
| PA drive and delays | From A's page: drive = item 0x045 (channel power byte 0) or 0x056 (otherwise), interpolated at the TX frequency; TX-on / TX-off delays from item 0x0AE, in us, over 2000 read as 0 (UNV). 0x045 is the high row: at 445 MHz 0x045 (3024) gave about 5.6 W, 0x056 (1177) about 2.1 W (**HW**, SDR bench). RadioFoundry's levels use both rows (13.1) |
| DMR keying | Per burst on the shared RF-burst task. The modem's RF TX lead is the shortest 100 us step that has the PA up for the first symbol: 2.4 ms + the TX-on delay - 0.85 ms, rounded up (`gd88_dmr_rf_tx_lead`). A's TX line falls at the last symbol's end and B waits no TX-off delay, so the ramp down starts there (150 us earlier, the VHF carrier was 4.9 dB down at the symbol's end). Bench, UHF, lowest power, into a dummy load: burst on-time 29.66-30.53 ms, ramps 0.75-0.85 ms up and 0.42-0.52 ms down, no AMBE FEC corrections in 162 frames (**HW**). With the 3.1 ms lead and the page's TX-off delay the bursts ran 31.08-31.73 ms. A's bursts are keyed by the key-up rule ([pa-keyup.md](pa-keyup.md)): A plans the drive's points and sends B their times after the TX line's edge (`RX2_TX`, [rx2-link.md](rx2-link.md)); A raises the line 300 us after the RF TX interrupt, and B steps DAC B at the points instead of ramping it. B also holds the PA select and supply until 50 us before the first point (`SUPPLY_AHEAD_US`, pa_gd88b.c): with the supply on at the sequence's start, the PA passed A's carrier at drive 0 from 1.75 ms before the first symbol, 40-44 dB under the level, in the slot before (UHF, **HW**). From 50 us to the point itself the rise is the same; 300 us ahead left a 0.3 ms step at -40 dB. Constants per band (`tools/bench/pa_cal.py`), every power level (13.1) into a dummy load, the dead zone a part of the band's zero point. UHF: no dead zone, knee 95 us, slope 85 us, window 800 us, settle 160 us: the five levels reach -3 dB 0.21-0.25 ms before the first symbol, at most 0.8 dB over their level. VHF: dead zone 44.9 %, knee 252 us, slope 82 us, window 1250 us, settle 160 us: 0.24-0.28 ms before the first symbol. The VHF middle levels overshoot by about 2.3 dB, as with their drive written alone (the PA's own; no ease limit helped). The output comes up 0.4 ms (the lowest level) to 1.2 ms (the top) after its drive on VHF, 0.4 to 0.9 ms on UHF (**HW**, SDR bench). The earliest point is 3.0 ms before the first symbol; the lead is 1.6 ms + the TX-on delay + the first point - 0.85 ms, rounded up (`gd88_dmr_key_rf_tx_lead`), 2.4 ms with a first point at 0.8 ms. The drive of each level and one set over the debug link (RADIO op 36) are in the `core` view (`tx_drive`). B's diag line counts the bursts whose first point had passed when its sequence got there (`late`). Untested on a radio. B's own transmitter (an SFR's repeat) still ramps: it runs the sequence in series, its chip tuned first, and needs 0.9 ms more (`gd88_dmr_own_rf_tx_lead`): with A's lead the first four symbols of every burst were lost on air (**HW**) |
| Start-up levels | Low: PTB20, PTB21, PTB6, PTB3, PTB1, PTB0, PTB15, PTB14, PTB13, PTB5, PTB12, PTB11, PTB8, PTB7, PTA17, PTB10, PTC18, PTB4, PTA15. High: PTB22, PTB18, PTB9 |
| NAND | SPI0, CS PTA18, the same part and map as A's (**HW**). B's calibration pair is in it and at 0x031FE000 of B's program flash |
| Status LEDs | One top LED with two colors: PTA16 red (lit while A transmits) and PTB17 green (receive), high = on (**HW**). While A is in its flash stub they take turns at 2 Hz (`RX2_LED_STUB`: A's stub has no LEDs) |
| ADC | Channel 4 is a carrier level |
| RF chips | The same 3-wire bus pins as A (section 9) |

## 11. RF side (both SoCs)

| Item | Finding |
|---|---|
| Bus | A and B have the same RF chips on the same 3-wire bus pins |
| Fitted parts | **HW** A on the bench: main 0x4819 (BK4819), LO 0x1100 (AT1141); the version string's hardware code reads `DBT` (main BK4819 'B', LO AT1141 'T'). Other units can differ: RadioFoundry reads the chip IDs (register 0) at start-up and takes a BK4819 or an AT1141 as the LO |
| Superhet | The main chip receives at the IF and transmits at f; the second chip is the LO. **A's IF is 45 MHz** with either LO chip; B's is 51.55 MHz. DAC B drives the PA (section 10) |
| BK4819 LO | High-side, f + IF, in registers 0x38 / 0x39 |
| TX deviation (0x40) | **HW** A, SDR bench: bits 11:0 scale the deviation of the tone generator too (its gain in 0x70 does not), about 10 Hz peak per count above about 0x2FA, linear from 0x448 to 0x4D9; the low end is not linear (0x35C gave about 300 Hz). Words in use: 0x34D0 plain (4.8 kHz with the tone), 0x34A8 with CTCSS, 0x348A for DTMF. APRS sets 0x2FA + 0x129 for its 3 kHz (aprs.md 5.2): 2.97-2.99 kHz peak, twist under 0.4 dB |

### 11.1 The AT1141 LO

A PLL / VCO synthesizer on the LO's enable with the BK4819's bus format (8-bit address, bit 7 = read, 16-bit data). No
datasheet is public. The driver is `drivers/rf/at1141`.

| Item | Finding |
|---|---|
| Banks | Register 0x7F = 1 selects a second bank; every table ends with 0x7F = 0 |
| Set-up | A 91-entry table, read back 0x15 / 0x16 / 0x19, the band for 400 MHz, the VCO start (waits 2, 5, 5 and 350 ms), 0x21 bits 10:5 = 10, output level 2 |
| Control (0x30) | 0x4006 standby, 0x4046 on, 0x4002 off for TX. Restart after TX: standby, 10 ms, standby, on |
| Tuning | Divider d (0x16 low byte) and multiplier m (0x15 bit 0: x3 when set, else x2) per octave from 550.4 MHz down, so d x f x (2 or 3) stays at 2.2-3.3 GHz. 0x3F = VCO / 406 250. PLL word in 0x62 (low) / 0x63 (high): VCO / 2 / 26 MHz in 6.26 fixed point, the reference halved when 0x19 bit 6 is set |
| Injection | f + 45 MHz, except **f - 45 MHz from 400 to 440 MHz** |
| Output level (0x26) | 0: 0x7F40; 1-7: one bit from bit 15 down, OR 0x0140; 8 and up: 0x0140 |
| GPIO | 0-3 in 0x2B, 4-7 in 0x2A, a nibble each (0xF = high). The band path is GPIO4 / GPIO5, as with a BK4819 LO |

Each SoC has its own modem, RF pair and NAND.

## 12. The A to B link

UART3 on both SoCs (PTA13 / PTA14, 0x14090000), 115200 8N1, interrupt vector 45. RadioFoundry runs its own protocol on
it ([rx2-link.md](rx2-link.md)).

## 13. The RadioFoundry port

Two boards, one per SoC. Both use `boards/gd88/common`: the main BK4819 with a BK4819 or AT1141 LO, the calibration
page, DMR on the modem and the SPI NAND. Both run on the bench's radio; FM and DMR receive and FM transmit work there.

| | A (`boards/gd88`) | B (`boards/gd88b`) |
|---|---|---|
| Runs | the radio core: UI, codeplug, scan, dual watch, host link on UART1, DMR, GPS | no core: a receiver task under `app_start` |
| Receive | FM and DMR on its own main BK4819 and LO (11.1) | FM and DMR, on what A asks for |
| Transmit | On (`GD88_TX=0` builds a receiver). A's main chip makes the signal; A arms B (`RX2_TX`, the drive and delays from A's page) and raises PTB2 | It keys the PA for A on PTB2's edges while armed (`pa_gd88b.c`). Its own chip transmits only as A's second DMR transmitter: an SFR's repeat ([rx2-link.md](rx2-link.md) 2.4), with A setting its GPIO4 / GPIO5 per burst (section 10) |
| Second receiver | B over UART3 (`board.rx2`, [rx2-link.md](rx2-link.md)). The core keeps B on the dual watch channel that is not selected | - |
| Audio | the chain's source bits (9.3) and PTC18. B's audio gets vfo 1 while A gives it the speaker and B reports it heard (the status frame's `heard`: its squelch open and the speaker its own), so dual watch with both receivers quiet leaves the amplifier off | the source pair to A, muted until A sends `AUDIO` on |
| LEDs | sent to B (`RX2_LED`) | PTA16, PTB17 |
| Storage | settings on the SPI NAND, blocks 1920-1951 | none |
| Calibration | both copies read, never written | the program-flash copy |
| Update | the cable, through RadioFoundry's first stage | through A's stub, which passes the cable to B (rx2-link.md 4): `stubtool.py --board gd88b` |
| Broadcast FM | the RDA5807-class tuner (`CONFIG_BCAST_FM`, on by default, `bcast_gd88.c`): the shared driver on PTA7 / PTA8, its audio on chain bit 6 with the amplifier | - |
| Debug link `CHIP_REG` ([debuglink.md](debuglink.md) 3.24) | chip 0: the main BK4819; chip 1: the LO when it is a BK4819. Page 0, registers 0x00-0x7F | the same, B's own chips, through A (`--rx2`) |

Not supported yet: the IMU, the headset path on its own, the RTC.

### 13.1 Power levels

Five levels, as on the other handhelds: 100 mW, 500 mW, 1 W, 2.5 W, 5 W (`gd88_tx_levels`,
`boards/gd88/common/calibration_gd88.c`), 1 W by default. The low three are fixed drives per band; 2.5 W and 5 W lie
between the page's two rows at the TX frequency, per mille from 0x056 to 0x045 ([power-calibration.md](../testing/power-calibration.md)).
The drives are approximate, from the SDR bench (a 57 dB pad into an SDR, about ±2 dB), and wait for a wattmeter.

| DAC B | 146 MHz | 445 MHz |
|---|---|---|
| 250 | under 1 mW | 3.5 mW |
| 300 | under 1 mW | 15 mW |
| 400 | under 1 mW | 72 mW |
| 500 | 4 mW | 220 mW |
| 700 | 100 mW | 652 mW |
| 900 | 440 mW | 1.17 W |
| 1100 | 1.0 W | 1.83 W |
| 1500 | 2.7 W | 2.89 W |
| 2200 | 6.1 W | 4.44 W |
| 2600 | 8.0 W | 5.16 W |
| 3000 | | 5.62 W |
| 3400 | | 5.57 W (saturated) |

| Level | VHF drive | UHF drive |
|---|---|---|
| zero point (about 20 mW, the key-up's anchor) | 575 | 330 |
| 100 mW | 700 | 426 |
| 500 mW | 929 | 651 |
| 1 W | 1098 | 846 |
| 2.5 W | row 0x056 + 6.3 % (1471 at 146 MHz) | row 0x056 + 10.7 % (1374 at 445 MHz) |
| 5 W | row 0x056 + 38.8 % (1988) | row 0x056 + 72.4 % (2515) |

The page's rows on the bench radio: 146 MHz 1371 (about 2.2 W) and 2960; 445 MHz 1177 (about 2.1 W) and 3024 (about
5.6 W). No cliff at 445 MHz: under the zero point the output stays at a floor of about 0.3 mW down to a drive of 40.
At 146 MHz it stays under 1 mW from 450 down to 250, the lowest drive measured.

## 14. Remaining unknowns for bring-up

The code marks each with `TODO(gd88)`. The first-day plan that settles them is [gd88-bringup.md](../testing/gd88-bringup.md).

1. Which RF chips other units have (the bench's A: BK4819 and AT1141), and B's.
2. ~~The output power at each level.~~ Approximate drives from the SDR bench (13.1); a wattmeter check is open.
3. Chain bit 6 as the broadcast tuner's source, and B's audio source pair.
4. The front panel's navigation keys, whether PTB12 is the knob's contact.
5. DAC C on each SoC. On A it is powered and on its pin, and 0 to 4095 leave the RSSI unchanged at 146, 162 and 461 MHz (**HW**): not receive gain; a frequency trim or a transmit role is left.
6. ~~The NAND's size and B's NAND chip select.~~ TC58CVG1S3HRAIJ, 2048 blocks of 64 2 KB pages on both; B's chip select is PTA18 (section 10).
7. ~~Calibration items 0x602-0x60B~~: unused, section 7.
8. Bootloader contents and the IAP packet format on this radio (not captured).

## Sources

- Radioddity extended manual v3.4 (2025-05-01), sections 4.3, 11.1, 17.2, 17.5, 17.6:
  <https://radioddity.s3.amazonaws.com/2025-05-01_Extended_manual_for_Radioddity_DB25-D_PRO_-_DB40-D_-_GD-88_-_v3.4.pdf>
- Radioddity "How to update firmware on DB25-D and GD-88" v2.2 (2023-03-30):
  <https://radioddity.s3.amazonaws.com/2023-03-30%20How%20to%20update%20firmware%20on%20DB25-D%20and%20GD-88%20v2.2.pdf>
- Radioddity GD-88 product page (in the box: charging base, AC adaptor, programming cable; K1 connection):
  <https://www.radioddity.com/products/radioddity-gd-88>
- HR_C7000 user manual (2016) and software application manual V2: UART and USB memory map, pin list and pin mux,
  PIC table, CLK_MGR USB fields
