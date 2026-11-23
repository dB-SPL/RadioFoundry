// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// NXDN conventional voice on a channel (docs/spec/nxdn.md 7): the call layer (nxdn.h) on an air interface session
// (core/air/air_session.h). The radio core begins it on an NXDN channel and ends it on leaving; in between it
// receives calls, hands their voice frames to a sink, and transmits when the radio core says so. No RTOS call: the
// radio core's task and the provider's task use it as air_session.h describes, voice frames cross to and from the
// audio side through the caller's hooks.
#ifndef RFOUNDRY_NXDN_ENGINE_H
#define RFOUNDRY_NXDN_ENGINE_H

#include "config.h"
#include "nxdn.h"
#include "../air/air_session.h"

typedef struct {
	// Receive, from the provider's task. start: a call we take began (its fields may still fill in: update). voice:
	// a 72-bit VCH as on air, descrambled; its FEC (the same as DMR's voice frames) is the voice path's
	void (*rx_start)(const nxdn_call_t *c, void *ctx);
	void (*rx_update)(const nxdn_call_t *c, void *ctx);
	void (*rx_voice)(const uint8_t ehr[NXDN_EHR_BYTES], void *ctx);
	void (*rx_end)(void *ctx);
	// Transmit, from the provider's task: the next 72-bit voice frame with its FEC, false when none is ready. NULL:
	// every frame is `silence`
	bool (*tx_voice)(uint8_t ehr[NXDN_EHR_BYTES], void *ctx);
#if CONFIG_NXDN_SCRAMBLER
	// Resolve a key when a call needs it. Zero means unavailable.
	uint16_t (*traffic_key)(uint8_t system, uint8_t cipher, uint8_t id, void *ctx);
#endif
#if CONFIG_NXDN_DATA
	// The packet is borrowed for this callback and contains length verified user bytes.
	void (*data_received)(const nxdn_data_packet_t *packet, uint16_t length, bool rate9600, void *ctx);
	void (*data_dropped)(uint8_t reason, void *ctx);
#endif
} nxdn_engine_hooks_t;

typedef struct {
	bool rate9600;		// 9600 bit/s, 12.5 kHz (EHR voice); else 4800 bit/s, 6.25 kHz
	uint8_t rx_ran;		// NXDN_RAN_ANY: every system's calls
	uint8_t tx_ran;
	// Which calls of our RAN are played: 0 every one (RAN squelch), or only group calls to this group, calls to all
	// (NXDN_ID_ALL) and individual calls to unit_id (TS 1-B 4.1.7)
	uint16_t rx_gid;
	uint16_t unit_id;	// ours: the source of what we send
	uint8_t tx_call_type;	// NXDN_CALL_GROUP or NXDN_CALL_INDIVIDUAL
	uint16_t tx_dst;	// the group or unit we call
	bool emergency;
	bool tx_inhibit;
	uint8_t tx_cipher;
	uint8_t tx_key_id;
#if CONFIG_NXDN_SCRAMBLER
	uint8_t system;		// traffic-key system for this channel
#endif
	const uint8_t *silence;	// a voice frame of silence (NXDN_EHR_BYTES), from the vocoder
} nxdn_engine_cfg_t;

typedef struct {
	bool rx_active;		// a call is being received (whether or not it is played)
	bool rx_played;
	nxdn_call_t rx;		// its fields; kept after it ends (the last call heard)
	uint8_t tx;		// air_tx_state_t
	uint32_t rx_calls, tx_calls;
	uint32_t tx_filler;	// silence frames sent because the encoder had none ready
} nxdn_engine_view_t;

#if CONFIG_NXDN_DATA
typedef enum {
	NXDN_DATA_TX_PENDING,
	NXDN_DATA_TX_COMPLETED,
	NXDN_DATA_TX_FAILED,
} nxdn_data_tx_result_t;
#endif

typedef struct {
	air_session_t session;
	const air_mode_t *mode;
	nxdn_call_rx_t rx;
	union {
		nxdn_call_tx_t tx;
#if CONFIG_NXDN_DATA
		struct {
			nxdn_data_tx_t packet;
			uint8_t bytes[NXDN_DATA_TX_BYTES];
			uint8_t frame[NXDN_FRAME_BYTES];
		} data_tx;
#endif
	};
	nxdn_engine_cfg_t cfg;
	nxdn_engine_hooks_t hooks;
	void *ctx;
	volatile bool tx_stop;		// the radio core let go: end at the next frame boundary
	bool tx_ended;			// ... and the call layer knows
	bool played;			// the call in hand passes the squelch
	volatile bool recheck;		// nxdn_engine_set changed what passes it
	volatile uint8_t seq;		// odd while the provider's task writes view
	nxdn_engine_view_t view;
	uint8_t frame[NXDN_FRAME_BYTES];	// the framer's frame buffer ...
	int8_t soft[NXDN_FRAME_SYMS];		// ... and its soft values, for the Viterbi decoder
#if CONFIG_NXDN_DATA
	nxdn_data_packet_t data_rx;
	void (*voice_frame)(const nxdn_rx_frame_t *, void *);
	void (*voice_lost)(void *);
	uint16_t data_index, head_delay_frames;
	uint8_t data_offset;
	uint8_t data_result;
	uint32_t data_underruns, data_starts;
	bool data_sending;
#endif
} nxdn_engine_t;
#if CONFIG_NXDN_DATA
enum {
	NXDN_DROP_CHANNEL_CRC = 1,
	NXDN_DROP_HEADER,
	NXDN_DROP_SEQUENCE,
	NXDN_DROP_MESSAGE_CRC,
	NXDN_DROP_RAN,
	NXDN_DROP_DESTINATION,
	NXDN_DROP_SYNC,
};
static inline bool nxdn_engine_data_queued(const nxdn_engine_t *e)
{
	// Preamble, header, data blocks and release.
	return e->data_index > e->head_delay_frames + e->data_tx.packet.blocks + 2;
}
bool nxdn_engine_data_stage(nxdn_engine_t *e, const uint8_t *bytes, uint16_t length, uint16_t dst, bool group,
			    uint16_t head_delay_frames);
static inline bool nxdn_engine_data_stage_to(nxdn_engine_t *e, const uint8_t *bytes, uint16_t length,
					    uint16_t dst, bool group)
{
	return nxdn_engine_data_stage(e, bytes, length, dst, group, 0);
}
// Current staged request. Check before clearing the TX engine.
nxdn_data_tx_result_t nxdn_engine_data_result(const nxdn_engine_t *e);
#endif

// The mode for a rate
const air_mode_t *nxdn_engine_mode(bool rate9600);
// What the board's provider gives NXDN at a rate: AIR_FIT_xxx, 0 = this board can't
uint32_t nxdn_engine_fit(const hal_air_t *air, bool rate9600);
// Start on the tuned channel (the board in its digital mode, its DMR stack stopped). False: no provider, no fit
bool nxdn_engine_begin(nxdn_engine_t *e, const hal_air_t *air, const nxdn_engine_cfg_t *cfg,
		       const nxdn_engine_hooks_t *hooks, void *ctx);
void nxdn_engine_end(nxdn_engine_t *e);
// The receiver is on a new frequency: what it held is dropped
void nxdn_engine_retuned(nxdn_engine_t *e);
// Change what is received and sent while running (a RAN or group change that needs no retune; not the rate). Not
// while transmitting
void nxdn_engine_set(nxdn_engine_t *e, const nxdn_engine_cfg_t *cfg);

// Transmit (the radio core's task). prepare: the preamble and first frame are queued; then hal_radio_t.tx(), then
// go. stop: the operator let go: the frame on air is finished and a release frame sent. state: AIR_TX_DONE = switch
// the transmitter off (hal_radio_t.tx_off), then clear
bool nxdn_engine_tx_prepare(nxdn_engine_t *e);
bool nxdn_engine_tx_go(nxdn_engine_t *e);
void nxdn_engine_tx_stop(nxdn_engine_t *e);
void nxdn_engine_tx_abort(nxdn_engine_t *e);
air_tx_state_t nxdn_engine_tx_state(const nxdn_engine_t *e);
void nxdn_engine_tx_clear(nxdn_engine_t *e);

// A copy of the state for the radio core's task
void nxdn_engine_view(const nxdn_engine_t *e, nxdn_engine_view_t *out);

#endif
