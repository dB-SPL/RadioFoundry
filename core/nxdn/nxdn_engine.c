// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// NXDN conventional voice on a channel (nxdn_engine.h).
#include "nxdn_engine.h"
#include "config.h"

#include <string.h>

const air_mode_t *nxdn_engine_mode(bool rate9600)
{
	return rate9600 ? &nxdn_air_mode_9600 : &nxdn_air_mode;
}

uint32_t nxdn_engine_fit(const hal_air_t *air, bool rate9600)
{
	return air_session_fit(air, nxdn_engine_mode(rate9600));
}

// ── Receive (provider's task) ──
static void view_begin(nxdn_engine_t *e) { e->seq++; }
static void view_end(nxdn_engine_t *e) { e->seq++; }

// The squelch on top of the RAN (TS 1-B 4.1.7): every call, or only the ones to our group, to all or to us. Nothing
// is played before the VCALL is in (the call layer holds the voice too)
static bool wanted(const nxdn_engine_t *e, const nxdn_call_t *c)
{
	if (!c->have_vcall)
		return false;
	if (!e->cfg.rx_gid)
		return true;
	if (c->vcall.call_type == NXDN_CALL_INDIVIDUAL)
		return c->vcall.dst == e->cfg.unit_id;
	return c->vcall.dst == e->cfg.rx_gid || c->vcall.dst == NXDN_ID_ALL;
}

static uint16_t key_for(const nxdn_engine_t *e, uint8_t cipher, uint8_t id)
{
#if CONFIG_NXDN_SCRAMBLER
	if (cipher == NXDN_CIPHER_SCRAMBLE && id < NXDN_KEY_IDS && e->hooks.traffic_key) {
		uint16_t key = e->hooks.traffic_key(e->cfg.system, cipher, id, e->ctx);
		if (key <= NXDN_SCRAMBLE_KEY_MAX)
			return key;
	}
#else
	(void)e;
	(void)cipher;
	(void)id;
#endif
	return 0;
}

static void rx_fields(nxdn_engine_t *e, const nxdn_call_t *c, bool start)
{
	if (start || c->vcall.cipher != e->view.rx.vcall.cipher || c->vcall.key_id != e->view.rx.vcall.key_id)
		e->rx.scramble_key = key_for(e, c->vcall.cipher, c->vcall.key_id);
	bool play = wanted(e, c) && !c->iv && (c->vcall.cipher == NXDN_CIPHER_NONE || e->rx.scramble_key);

	view_begin(e);
	e->view.rx = *c;
	e->view.rx_active = true;
	e->view.rx_played = play;
	if (start)
		e->view.rx_calls++;
	view_end(e);
	if (play && !e->played) {
		e->played = true;
		if (e->hooks.rx_start)
			e->hooks.rx_start(c, e->ctx);
	} else if (play) {
		if (e->hooks.rx_update)
			e->hooks.rx_update(c, e->ctx);
	} else if (e->played) {
		// A field that came in late says the call is not for us after all (or it turned encrypted)
		e->played = false;
		if (e->hooks.rx_end)
			e->hooks.rx_end(e->ctx);
	}
}

static void on_start(const nxdn_call_t *c, void *ctx)
{
	nxdn_engine_t *e = ctx;

	e->played = false;
	rx_fields(e, c, true);
}

static void on_update(const nxdn_call_t *c, void *ctx)
{
	rx_fields(ctx, c, false);
}

static void on_voice(const uint8_t ehr[NXDN_EHR_BYTES], void *ctx)
{
	nxdn_engine_t *e = ctx;

	// The squelch settings changed inside this call: it is judged again with the fields it has
	if (e->recheck) {
		e->recheck = false;
		rx_fields(e, &e->rx.call, false);
	}
	if (e->played && e->hooks.rx_voice)
		e->hooks.rx_voice(ehr, e->ctx);
}

static void on_end(void *ctx)
{
	nxdn_engine_t *e = ctx;

	view_begin(e);
	e->view.rx_active = false;
	e->view.rx_played = false;
	view_end(e);
	if (e->played && e->hooks.rx_end)
		e->hooks.rx_end(e->ctx);
	e->played = false;
}

static const nxdn_call_sink_t sink = { on_start, on_update, on_voice, on_end };

#if CONFIG_NXDN_DATA
static void data_drop(nxdn_engine_t *e, uint8_t reason)
{
	if (e->hooks.data_dropped)
		e->hooks.data_dropped(reason, e->ctx);
}

static void engine_frame(const nxdn_rx_frame_t *f, void *ctx)
{
	nxdn_engine_t *e = ctx;
	uint8_t channel[NXDN_DATA_BYTES];
	uint16_t length;

	if (f->lich < 0 || NXDN_LICH_USC(f->lich) != NXDN_USC_UDCH) {
		e->data_rx.blocks = 0;
		e->voice_frame(f, &e->rx);
		return;
	}
	if (nxdn_data_get(f->hard, f->soft, &e->rx.rx.work, channel) < 0) {
		e->data_rx.blocks = 0;
		data_drop(e, NXDN_DROP_CHANNEL_CRC);
		return;
	}
	if (e->cfg.rx_ran != NXDN_RAN_ANY && NXDN_SR_RAN(channel[0]) != e->cfg.rx_ran) {
		e->data_rx.blocks = 0;
		data_drop(e, NXDN_DROP_RAN);
		return;
	}
	nxdn_data_result_t result = nxdn_data_receive(&e->data_rx, e->cfg.rate9600, (uint8_t)f->lich, channel, &length);
	switch (result) {
	case NXDN_DATA_BAD_HEADER: data_drop(e, NXDN_DROP_HEADER); return;
	case NXDN_DATA_BAD_SEQUENCE: data_drop(e, NXDN_DROP_SEQUENCE); return;
	case NXDN_DATA_BAD_CRC: data_drop(e, NXDN_DROP_MESSAGE_CRC); return;
	case NXDN_DATA_COMPLETE: break;
	default: return;
	}
	const nxdn_vcall_t *address = &e->data_rx.address;
	uint16_t target = address->call_type == NXDN_CALL_INDIVIDUAL ? e->cfg.unit_id : e->cfg.rx_gid;

	if (address->dst != target &&
	    !(address->call_type == NXDN_CALL_GROUP && address->dst == NXDN_ID_ALL))
		data_drop(e, NXDN_DROP_DESTINATION);
	else if (e->hooks.data_received)
		e->hooks.data_received(&e->data_rx, length, e->cfg.rate9600, e->ctx);
}

static void engine_lost(void *ctx)
{
	nxdn_engine_t *e = ctx;

	if (e->data_rx.blocks)
		data_drop(e, NXDN_DROP_SYNC);
	e->data_rx.blocks = 0;
	e->voice_lost(&e->rx);
}

static void data_fill(nxdn_engine_t *e)
{
	while (!nxdn_engine_data_queued(e)) {
		uint16_t space = air_session_tx_space(&e->session);
		uint16_t total = e->data_index ? NXDN_FRAME_SYMS : NXDN_PREAMBLE_SYMS;
		uint16_t count = total - e->data_offset;
		const uint8_t *symbols = e->data_index ? e->data_tx.frame : nxdn_preamble;

		if (!space)
			return;
		if (count > space)
			count = space;
		if (e->data_index && !e->data_offset) {
			if (e->data_index <= e->head_delay_frames)
				nxdn_data_head_delay(&e->data_tx.packet, (uint8_t)(e->head_delay_frames - e->data_index),
						     e->data_tx.frame);
			else
				nxdn_data_frame(&e->data_tx.packet, e->data_tx.bytes, e->cfg.rate9600,
						(uint8_t)(e->data_index - e->head_delay_frames - 1), e->data_tx.frame);
		}
		if (!air_session_tx(&e->session, symbols, e->data_offset, count))
			return;
		e->data_offset += count;
		if (e->data_offset == total) {
			e->data_offset = 0;
			e->data_index++;
		}
	}
	air_session_tx_finish(&e->session);
}

static void data_result(nxdn_engine_t *e, nxdn_data_tx_result_t result)
{
	uint8_t pending = NXDN_DATA_TX_PENDING;

	__atomic_compare_exchange_n(&e->data_result, &pending, (uint8_t)result, false,
				    __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

nxdn_data_tx_result_t nxdn_engine_data_result(const nxdn_engine_t *e)
{
	return __atomic_load_n(&e->data_result, __ATOMIC_ACQUIRE);
}

bool nxdn_engine_data_stage(nxdn_engine_t *e, const uint8_t *bytes, uint16_t length, uint16_t dst, bool group,
			    uint16_t head_delay_frames)
{
	nxdn_vcall_t address = { .call_type = e->cfg.tx_call_type, .src = e->cfg.unit_id, .dst = e->cfg.tx_dst };

	if (dst) {
		address.dst = dst;
		address.call_type = group ? NXDN_CALL_GROUP : NXDN_CALL_INDIVIDUAL;
	}
	if (head_delay_frames > NXDN_DATA_MAX_HEAD_DELAY || e->data_sending || e->cfg.tx_inhibit || e->cfg.tx_cipher != NXDN_CIPHER_NONE ||
	    !air_session_is_open(&e->session) || air_session_tx_state(&e->session) != AIR_TX_IDLE || !(e->session.fit & AIR_FIT_TX) ||
	    !nxdn_data_prepare(&e->data_tx.packet, e->data_tx.bytes, sizeof e->data_tx.bytes, &address, e->cfg.tx_ran, bytes, length))
		return false;
	e->data_index = e->data_offset = 0;
	e->head_delay_frames = head_delay_frames;
	e->data_underruns = e->session.stats.tx_underruns;
	e->data_starts = e->session.stats.tx_chunks_started;
	__atomic_store_n(&e->data_result, NXDN_DATA_TX_PENDING, __ATOMIC_RELAXED);
	e->data_sending = true;
	return true;
}
#endif

// ── Transmit ──
// Symbols to the session while its queue has room. A voice frame the encoder has not made yet is waited for as
// long as the air can spare: once less than the provider's lead plus one more chunk is queued, silence goes instead
static void tx_fill(void *ctx)
{
	nxdn_engine_t *e = ctx;
#if CONFIG_NXDN_DATA
	if (e->data_sending) {
		data_fill(e);
		return;
	}
#endif
	const hal_air_t *air = e->session.air;
	uint16_t lead = (uint16_t)(((uint32_t)air->tx_lead_ms * e->mode->profile.sym_rate) / 1000 + air->tx_chunk_syms);

	for (;;) {
		uint16_t first, n = nxdn_call_tx_ready(&e->tx, &first), space;

		if (nxdn_call_tx_done(&e->tx)) {
			air_session_tx_finish(&e->session);
			return;
		}
		if (e->tx_stop && !e->tx_ended) {
			e->tx_ended = true;
			nxdn_call_tx_end(&e->tx, e->cfg.silence);
			continue;
		}
		if (!n) {
			uint8_t ehr[NXDN_EHR_BYTES];

			// (not done and nothing ready: a frame waits for its next voice frame)
			if (e->hooks.tx_voice && e->hooks.tx_voice(ehr, e->ctx) && nxdn_call_tx_voice(&e->tx, ehr))
				continue;
			if (!e->hooks.tx_voice ||
				   (air_session_tx_state(&e->session) != AIR_TX_PRIMING && air_session_tx_queued(&e->session) < lead)) {
				// (before the transmitter is on nothing is lost by waiting: the preamble and header are queued)
				if (e->hooks.tx_voice)
					e->view.tx_filler++;
				if (!nxdn_call_tx_voice(&e->tx, e->cfg.silence))
					return;
			} else {
				return;
			}
			continue;
		}
		space = air_session_tx_space(&e->session);
		if (!space)
			return;
		if (n > space)
			n = space;
		if (!air_session_tx(&e->session, nxdn_call_tx_syms(&e->tx), first, n))
			return;
		nxdn_call_tx_sent(&e->tx, n);
	}
}

#if CONFIG_NXDN_DATA
static void tx_done(void *ctx)
{
	nxdn_engine_t *e = ctx;

	// A partial receive cannot continue across a local transmission.
	e->data_rx.blocks = 0;
	if (e->data_sending) {
		bool complete = e->session.finished && nxdn_engine_data_queued(e) &&
			e->session.stats.tx_chunks_started != e->data_starts &&
			e->session.stats.tx_underruns == e->data_underruns;
		data_result(e, complete ? NXDN_DATA_TX_COMPLETED : NXDN_DATA_TX_FAILED);
	}
}
#endif

static const air_session_ops_t ops = {
	.tx_fill = tx_fill,
#if CONFIG_NXDN_DATA
	.tx_done = tx_done,
#endif
};

bool nxdn_engine_begin(nxdn_engine_t *e, const hal_air_t *air, const nxdn_engine_cfg_t *cfg,
		       const nxdn_engine_hooks_t *hooks, void *ctx)
{
	memset(e, 0, sizeof *e);
	e->cfg = *cfg;
	e->hooks = *hooks;
	e->ctx = ctx;
	e->mode = nxdn_engine_mode(cfg->rate9600);
	nxdn_call_rx_init(&e->rx, cfg->rx_ran, &sink, e);
#if CONFIG_NXDN_DATA
	e->voice_frame = e->rx.rx.frame;
	e->voice_lost = e->rx.rx.lost;
	e->rx.rx.frame = engine_frame;
	e->rx.rx.lost = engine_lost;
	e->rx.rx.ctx = e;
#endif
	return air_session_open(&e->session, air, e->mode, &e->rx.rx, e->frame, e->soft, NXDN_FRAME_SYMS, &ops, e);
}

void nxdn_engine_end(nxdn_engine_t *e)
{
	air_session_close(&e->session);
}

void nxdn_engine_retuned(nxdn_engine_t *e)
{
	air_session_retuned(&e->session);
#if CONFIG_NXDN_DATA
	e->data_rx.blocks = 0;
#endif
}

void nxdn_engine_set(nxdn_engine_t *e, const nxdn_engine_cfg_t *cfg)
{
	bool rate = e->cfg.rate9600;

	e->cfg = *cfg;
	e->cfg.rate9600 = rate;		// the mode is the session's: a new rate is a new session
	e->rx.rx.ran = cfg->rx_ran;
	e->rx.scramble_key = key_for(e, e->rx.call.vcall.cipher, e->rx.call.vcall.key_id);
	e->recheck = true;
}

bool nxdn_engine_tx_prepare(nxdn_engine_t *e)
{
	const nxdn_engine_cfg_t *cfg = &e->cfg;
	uint8_t cipher = cfg->tx_cipher;
	uint8_t id = cfg->tx_key_id;

	if (cfg->tx_inhibit)
		return false;
#if CONFIG_NXDN_DATA
	if (e->data_sending)
		return cfg->tx_cipher == NXDN_CIPHER_NONE && air_session_tx_prime(&e->session);
#endif
	uint16_t key = key_for(e, cipher, id);
	if (cipher != NXDN_CIPHER_NONE && !key)
		return false;
	if (!air_session_is_open(&e->session) || !(e->session.fit & AIR_FIT_TX) || !e->cfg.silence)
		return false;
	if (air_session_tx_state(&e->session) == AIR_TX_IDLE) {
		nxdn_vcall_t v = {
			.msg = NXDN_MSG_VCALL,
			.cc_opt = e->cfg.emergency ? NXDN_CC_EMERGENCY : 0,
			.call_type = e->cfg.tx_call_type,
			.voice_opt = e->cfg.rate9600 ? NXDN_VOICE_9600_EHR : NXDN_VOICE_4800_EHR,
			.src = e->cfg.unit_id,
			.dst = e->cfg.tx_dst,
			.cipher = cipher,
			.key_id = cipher == NXDN_CIPHER_NONE ? 0 : id,
		};

		e->tx_stop = e->tx_ended = false;
		// A subscriber unit sends inbound, direct or through a repeater (table 5.2-2)
		nxdn_call_tx_begin(&e->tx, &v, e->cfg.tx_ran, NXDN_DIR_INBOUND, e->cfg.rate9600);
		e->tx.scramble_key = key;
	}
	return air_session_tx_prime(&e->session);
}

bool nxdn_engine_tx_go(nxdn_engine_t *e)
{
	if (!air_session_tx_go(&e->session)) {
#if CONFIG_NXDN_DATA
		if (e->data_sending)
			data_result(e, NXDN_DATA_TX_FAILED);
#endif
		return false;
	}
	e->view.tx_calls++;
	return true;
}

void nxdn_engine_tx_stop(nxdn_engine_t *e)
{
#if CONFIG_NXDN_DATA
	if (e->data_sending) {
		nxdn_engine_tx_abort(e);
		return;
	}
#endif
	e->tx_stop = true;
}

void nxdn_engine_tx_abort(nxdn_engine_t *e)
{
#if CONFIG_NXDN_DATA
	if (e->data_sending)
		data_result(e, NXDN_DATA_TX_FAILED);
#endif
	air_session_tx_abort(&e->session);
}

air_tx_state_t nxdn_engine_tx_state(const nxdn_engine_t *e)
{
	return air_session_tx_state(&e->session);
}

void nxdn_engine_tx_clear(nxdn_engine_t *e)
{
	air_session_tx_clear(&e->session);
#if CONFIG_NXDN_DATA
	if (air_session_tx_state(&e->session) == AIR_TX_IDLE)
		e->data_sending = false;
#endif
}

void nxdn_engine_view(const nxdn_engine_t *e, nxdn_engine_view_t *out)
{
	uint8_t seq;

	// The provider's task writes the view in a few instructions: read again if it did meanwhile
	do {
		seq = e->seq;
		*out = e->view;
	} while ((seq & 1) || seq != e->seq);
	out->tx = (uint8_t)air_session_tx_state(&e->session);
}
