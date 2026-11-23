// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// NXDN TS 1-D: keyed scrambling for EHR voice and a conventional data fragment.
#ifndef RFOUNDRY_NXDN_SCRAMBLE_H
#define RFOUNDRY_NXDN_SCRAMBLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// frame and vch are positions 0..3 in the superframe, including positions occupied by FACCH1.
// bits are 49 unpacked bits, in vocoder order. Invalid arguments leave them untouched.
bool nxdn_scramble_ehr(uint16_t key, uint8_t frame, uint8_t vch, uint8_t bits[49]);

// Transform an EHR frame around its FEC. Returns false for an uncorrectable frame or invalid key/position.
bool nxdn_scramble_vch(uint16_t key, uint8_t frame, uint8_t vch, uint8_t ehr[9]);

// XOR a packed, MSB-first data fragment (including the message CRC and pad bytes).
// Start the generator once for the fragment. Invalid arguments leave the buffer untouched.
bool nxdn_scramble_data(uint16_t key, uint8_t *bytes, size_t length);

#endif
