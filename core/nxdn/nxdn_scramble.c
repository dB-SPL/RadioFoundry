// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include "nxdn_scramble.h"
#include "dmr/ambe/ambeFec.h"

// S0 is the output; S0 XOR S1 feeds S14 (TS 1-D figure 5.3-1).
static uint16_t step(uint16_t state)
{
	return (uint16_t)((state >> 1) | (((state ^ (state >> 1)) & 1u) << 14));
}

bool nxdn_scramble_data(uint16_t key, uint8_t *bytes, size_t length)
{
	if (!bytes || !key || key > 0x7fff)
		return false;
	uint16_t state = key;

	for (size_t i = 0; i < length; i++) {
		uint8_t mask = 0;

		for (uint8_t bit = 0; bit < 8; bit++) {
			mask = (uint8_t)(mask << 1 | (state & 1u));
			state = step(state);
		}
		bytes[i] ^= mask;
	}
	return true;
}

bool nxdn_scramble_vch(uint16_t key, uint8_t frame, uint8_t vch, uint8_t ehr[9])
{
	uint8_t bits[AMBE_PARAM_BITS];
	ambe_fec_result_t result;

	if (!ehr || !key || key > 0x7fff || frame > 3 || vch > 3)
		return false;
	ambeFecDecode(ehr, bits, &result);
	if (result.c0_failed || !nxdn_scramble_ehr(key, frame, vch, bits))
		return false;
	ambeFecEncode(bits, ehr);
	return true;
}

bool nxdn_scramble_ehr(uint16_t key, uint8_t frame, uint8_t vch, uint8_t bits[49])
{
	uint16_t state = key;
	unsigned offset;

	if (!bits || key == 0 || key > 0x7fff || frame > 3 || vch > 3)
		return false;
	offset = (unsigned)(frame * 4u + vch) * 49u;
	while (offset--)
		state = step(state);
	for (unsigned i = 0; i < 49; i++) {
		bits[i] ^= (uint8_t)(state & 1u);
		state = step(state);
	}
	return true;
}
