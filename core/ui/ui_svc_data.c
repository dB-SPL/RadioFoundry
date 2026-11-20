// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The ui_svc.h calls for text messages, call alert and radio check, on the DMR data service's API
// (core/dmr/dmr_data_api.h, safe from any task). Its events are polled (dmr_data_serial) and handed to the UI as
// ui_notify events, which become banners. Built only with the data core; otherwise the weak stubs stay.
#include "config.h"

#if CONFIG_DMR_DATA_CORE || CONFIG_NXDN_DATA
#include <string.h>
#include "ui_svc.h"
#include "dmr/dmr_data_api.h"
#include "codeplug/codeplug.h"
#include "hal_time.h"
#include "radio/radio.h"
#include "radio/radio_nxdn.h"

uint32_t ui_svc_feat_data(void)
{
	return (CONFIG_DMR_SMS ? UI_FEAT_SMS : 0) | (CONFIG_DMR_SIGNALING ? UI_FEAT_SIGNALING : 0) |
	       (CONFIG_ZONE_SHARE ? UI_FEAT_ZONE_SHARE : 0);
}

static void contact_name(uint32_t id, bool group, char *out, size_t len)
{
	contact_t c;
	out[0] = 0;
	if (codeplug_contact_find(id, group, &c)) {
		strncpy(out, c.name, len - 1);
		out[len - 1] = 0;
	}
}

static dmrDataMessage_t dmr_message; // UI-task scratch

#if CONFIG_NXDN_DATA
static uint32_t tx_destination;

bool ui_svc_nxdn_target(uint16_t *dst, bool *group)
{
	radio_nxdn_settings_t run;

	if (!radio_nxdn_in_force(&run))
		return false;
	*dst = run.tx_dst;
	*group = !run.unit_call;
	return true;
}

bool ui_svc_nxdn_send(uint32_t dst, bool group, const char *text)
{
	size_t length = strlen(text);

	if (!dst || dst > UINT16_MAX || length > UI_MSG_NXDN_MAX || tx_destination ||
	    !radio_req_nxdn_text_to((const uint8_t *)text, (uint16_t)length, (uint16_t)dst, group))
		return false;
	tx_destination = dst;
	return true;
}

static void nxdn_poll(void)
{
	static radio_nxdn_text_t message;

	while (radio_nxdn_text_pop(&message)) {
		uint16_t handle = sms_inbox_add_nxdn(message.src, message.at_ms, message.text);
		ui_event_t ev = { .kind = UI_EV_MESSAGE, .id = message.src, .index = handle };

		contact_name(message.src, false, ev.name, sizeof ev.name);
		strncpy(ev.text, message.text, sizeof ev.text - 1);
		ui_notify(&ev);
	}
	if (!tx_destination)
		return;
	uint8_t state = radio_nxdn_text_result();

	if (state == RADIO_NXDN_TEXT_SENT || state == RADIO_NXDN_TEXT_FAILED) {
		ui_event_t ev = { .kind = UI_EV_MESSAGE_SENT, .id = tx_destination,
				  .ok = state == RADIO_NXDN_TEXT_SENT, .index = 2 };

		tx_destination = 0;
		ui_notify(&ev);
	}
}
#endif

uint16_t ui_svc_msg_handle(uint16_t row)
{
#if CONFIG_NXDN_DATA
	return sms_inbox_handle(row);
#else
	return row;
#endif
}

uint16_t ui_svc_msg_count(void)
{
#if CONFIG_NXDN_DATA
	return (uint16_t)sms_inbox_total(false);
#else
	return (uint16_t)sms_inbox_count();
#endif
}

uint16_t ui_svc_msg_unread(void)
{
#if CONFIG_NXDN_DATA
	return (uint16_t)sms_inbox_total(true);
#else
	return (uint16_t)sms_inbox_unread();
#endif
}

bool ui_svc_msg_get(uint16_t i, ui_msg_t *out)
{
#if CONFIG_NXDN_DATA
	if (i < SMS_INBOX_HANDLE_FLAG) {
		i = sms_inbox_handle(i);
		if (!i)
			return false;
	}
#endif
	const dmrDataMessage_t *m = &dmr_message;
	if (!sms_inbox_get(i, &dmr_message))
		return false;
	memset(out, 0, sizeof *out);
	out->src = m->src;
	out->group = m->group;
	out->unread = m->unread;
	out->nxdn = m->format == SMS_INBOX_NXDN_FORMAT;
	out->age_ms = hal_millis() - m->receivedAt;
	contact_name(m->src, false, out->name, sizeof out->name);
	strncpy(out->text, m->text, UI_MSG_TEXT_MAX);
	out->text[UI_MSG_TEXT_MAX] = 0;
	return true;
}

void ui_svc_msg_read(uint16_t i)
{
	sms_inbox_mark_read(i);
}

bool ui_svc_msg_delete(uint16_t i)
{
	return sms_inbox_delete(i);
}

bool ui_svc_msg_send(uint32_t dst, bool group, const char *text)
{
	// A message to a person asks for the acknowledgment ("Delivered") where its format has one; a group message
	// can't. The format (ETSI IP text, ETSI short data or TMS) is the radio's setting (dmrDataSettings_t smsFormat)
	return sms_send(dst, group, text, !group, DMR_SMS_FORMAT_DEFAULT);
}

static uint32_t check_sent_ms;

_Static_assert((int)UI_ARS_OFF == (int)DMR_ARS_STATE_OFF &&
	       (int)UI_ARS_REGISTERING == (int)DMR_ARS_STATE_REGISTERING &&
	       (int)UI_ARS_REGISTERED == (int)DMR_ARS_STATE_REGISTERED &&
	       (int)UI_ARS_REFUSED == (int)DMR_ARS_STATE_REFUSED &&
	       (int)UI_ARS_NO_ANSWER == (int)DMR_ARS_STATE_NO_ANSWER, "ui_svc.h UI_ARS_*");

uint8_t ui_svc_ars_state(void)
{
	return (uint8_t)dmr_data_ars_state();
}

bool ui_svc_radio_check(uint32_t id)
{
	check_sent_ms = hal_millis();
	return dmr_signal_radio_check(id);
}

bool ui_svc_call_alert(uint32_t id)
{
	return dmr_signal_call_alert(id);
}

void ui_svc_poll(void)
{
#if CONFIG_NXDN_DATA
	nxdn_poll();
#endif
#if CONFIG_DMR_EMERGENCY
	ui_svc_emergency_poll();
#endif
	// The event's own serial, not dmr_data_serial(): that one also moves when a message is read or deleted, and the
	// last event would come again ("Message from" over "Message deleted", with its tone)
	static uint32_t seen;
	dmrDataEvent_t d = dmr_data_last_event();
	if (d.serial == seen)
		return;
	seen = d.serial;
	ui_event_t ev;
	memset(&ev, 0, sizeof ev);
	ev.id = d.id;
	switch (d.kind) {
	case DMR_DATA_EV_SMS_RECEIVED: {
		ev.kind = UI_EV_MESSAGE;
		if (sms_inbox_get(0, &dmr_message)) {
#if CONFIG_NXDN_DATA
			ev.index = dmr_message.handle;
#endif
			strncpy(ev.text, dmr_message.text, sizeof ev.text - 1);
		}
		break;
	}
	case DMR_DATA_EV_SMS_SENT:
	case DMR_DATA_EV_SMS_FAILED:
		ev.kind = UI_EV_MESSAGE_SENT;
		ev.ok = d.kind == DMR_DATA_EV_SMS_SENT;
		ev.index = (ev.ok && !sms_send_confirmed()) ? 1 : 0;	// on air, but nothing confirms delivery
		break;
	case DMR_DATA_EV_CALL_ALERT:
		ev.kind = UI_EV_CALL_ALERT;
		break;
	case DMR_DATA_EV_CALL_ALERT_ACKED:
	case DMR_DATA_EV_CALL_ALERT_FAILED:
		ev.kind = UI_EV_CALL_ALERT_RESULT;
		ev.ok = d.kind == DMR_DATA_EV_CALL_ALERT_ACKED;
		break;
	case DMR_DATA_EV_RADIO_CHECK_OK:
	case DMR_DATA_EV_RADIO_CHECK_FAILED:
		ev.kind = UI_EV_RADIO_CHECK;
		ev.ok = d.kind == DMR_DATA_EV_RADIO_CHECK_OK;
		ev.ms = d.atMs - check_sent_ms;
		break;
#if CONFIG_ZONE_SHARE
	case DMR_DATA_EV_ZONE_OFFER:
	case DMR_DATA_EV_ZONE_SENT:
	case DMR_DATA_EV_ZONE_FAILED:
		// ui_svc_zone.c fills these whole (the offer's text, a talkgroup's name)
		if (ui_svc_zone_event(d.kind, d.id, &ev))
			ui_notify(&ev);
		return;
#endif
	default:
		return;		// ARS changes and checks of us stay off the screen
	}
	contact_name(d.id, false, ev.name, sizeof ev.name);
	ui_notify(&ev);
}
#endif
