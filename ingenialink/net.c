/*
 * MIT License
 *
 * Copyright (c) 2017 Ingenia-CAT S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "net.h"

#include <string.h>

#include "osal/clock.h"

#include "ingenialink/err.h"
#include "ingenialink/utils.h"

/*******************************************************************************
 * Private
 ******************************************************************************/

/**
 * Process asynchronous statusword messages.
 *
 * @param [in] net
 *	IngeniaLink network instance.
 * @param [in] frame
 *	IngeniaLink frame.
 */
void process_statusword(il_net_t *net, il_frame_t *frame)
{
	uint16_t idx;
	uint8_t sidx;

	idx = il_frame__get_idx(frame);
	sidx = il_frame__get_sidx(frame);

	if (idx == STATUSWORD_IDX && sidx == STATUSWORD_SIDX) {
		size_t i;
		uint8_t id;
		uint16_t sw;

		id = il_frame__get_id(frame);
		sw = __swap_16(*(uint16_t *)il_frame__get_data(frame));

		osal_mutex_lock(net->sw_subs.lock);

		for (i = 0; i < net->sw_subs.cnt; i++) {
			if (net->sw_subs.subs[i].id == id) {
				void *ctx;

				ctx = net->sw_subs.subs[i].ctx;
				net->sw_subs.subs[i].cb(ctx, sw);
			}
		}

		osal_mutex_unlock(net->sw_subs.lock);
	}
}

/**
 * Process synchronous messages.
 *
 * @param [in] net
 *	IngeniaLink network instance.
 * @param [in] frame
 *	IngeniaLink frame.
 */
void process_sync(il_net_t *net, il_frame_t *frame)
{
	osal_mutex_lock(net->sync.lock);

	if (!net->sync.complete) {
		uint8_t id = il_frame__get_id(frame);
		uint16_t idx = il_frame__get_idx(frame);
		uint8_t sidx = il_frame__get_sidx(frame);
		size_t sz = il_frame__get_sz(frame);

		if (((net->sync.id == id) || (net->sync.id == 0)) &&
		    (net->sync.idx == idx) && (net->sync.sidx == sidx) &&
		    (net->sync.sz >= sz)) {
			void *data = il_frame__get_data(frame);

			memcpy(net->sync.buf, data, sz);

			if (net->sync.recvd)
				*net->sync.recvd = sz;

			net->sync.complete = 1;
			osal_cond_signal(net->sync.cond);
		}
	}

	osal_mutex_unlock(net->sync.lock);
}

/**
 * Process reception buffer.
 *
 * @param [in] net
 *	IngeniaLink network instance.
 * @param [in] rbuf
 *	Reception buffer.
 * @param [in, out] cnt
 *	Buffer contents size.
 * @param [in, out] frame
 *	IngeniaLink frame.
 */
void process_rbuf(il_net_t *net, uint8_t *rbuf, size_t *cnt, il_frame_t *frame)
{
	size_t i;

	for (i = 0; *cnt; i++) {
		int r;

		(*cnt)--;

		/* push to the frame (and update its state) */
		r = il_frame__push(frame, rbuf[i]);
		if (r < 0) {
			/* likely garbage, reset keeping current */
			il_frame__reset(frame);
			(void)il_frame__push(frame, rbuf[i]);

			continue;
		}

		/* validate */
		if (frame->state == IL_FRAME_STATE_COMPLETE) {
			if (il_frame__is_resp(frame)) {
				process_statusword(net, frame);
				process_sync(net, frame);
			}

			il_frame__reset(frame);
		}
	}
}

/**
 * Listener thread.
 *
 * @param [in] args
 *	IngeniaLink network (il_net_t *).
 */
int listener(void *args)
{
	il_net_t *net = args;

	il_frame_t frame = IL_FRAME_INIT_DEF;

	uint8_t rbuf[IL_FRAME_MAX_SZ];
	size_t rbuf_cnt = 0;

	while (!net->stop) {
		int r;
		size_t rbuf_free, rbuf_added;

		/* process current buffer content */
		process_rbuf(net, rbuf, &rbuf_cnt, &frame);

		/* read more bytes */
		rbuf_free = sizeof(rbuf) - rbuf_cnt;

		r = ser_read(net->ser, &rbuf[rbuf_cnt], rbuf_free, &rbuf_added);
		if (r == SER_EEMPTY) {
			r = ser_read_wait(net->ser);
			if (r == SER_ETIMEDOUT)
				continue;
			else if (r < 0)
				goto err;
		} else if ((r < 0) || ((r == 0) && (rbuf_added == 0))) {
			goto err;
		} else {
			rbuf_cnt += rbuf_added;
		}
	}

	return 0;

err:
	osal_mutex_lock(net->state_lock);
	net->state = IL_NET_STATE_FAULTY;
	osal_mutex_unlock(net->state_lock);

	return IL_EFAIL;
}

/**
 * Monitor event callback.
 */
void on_ser_evt(void *ctx, ser_dev_evt_t evt, const ser_dev_t *dev)
{
	il_net_dev_mon_t *mon = ctx;

	if (evt == SER_DEV_EVT_ADDED)
		mon->on_evt(mon->ctx, IL_NET_DEV_EVT_ADDED, dev->path);
	else
		mon->on_evt(mon->ctx, IL_NET_DEV_EVT_REMOVED, dev->path);
}

/*******************************************************************************
 * Internal
 ******************************************************************************/

int il_net__write(il_net_t *net, uint8_t id, uint16_t idx, uint8_t sidx,
		  const void *buf, size_t sz)
{
	int r;
	il_frame_t frame;

	/* check network state */
	if (il_net_state_get(net) != IL_NET_STATE_OPERATIVE) {
		ilerr__set("Network is not operative");
		return IL_ESTATE;
	}

	osal_mutex_lock(net->lock);

	il_frame__init(&frame, id, idx, sidx, buf, sz);

	r = ser_write(net->ser, frame.buf, frame.sz, NULL);
	if (r < 0)
		ilerr__ser(r);

	osal_mutex_unlock(net->lock);

	return r;
}

int il_net__read(il_net_t *net, uint8_t id, uint16_t idx, uint8_t sidx,
		 void *buf, size_t sz, size_t *recvd, int timeout)
{
	int r;
	il_frame_t frame;

	/* check network state */
	if (il_net_state_get(net) != IL_NET_STATE_OPERATIVE) {
		ilerr__set("Network is not operative");
		return IL_ESTATE;
	}

	osal_mutex_lock(net->lock);

	/* register synchronous transfer */
	osal_mutex_lock(net->sync.lock);

	net->sync.id = id;
	net->sync.idx = idx;
	net->sync.sidx = sidx;
	net->sync.buf = buf;
	net->sync.sz = sz;
	net->sync.recvd = recvd;
	net->sync.complete = 0;

	/* send synchronous read petition */
	il_frame__init(&frame, id, idx, sidx, NULL, 0);

	r = ser_write(net->ser, frame.buf, frame.sz, NULL);
	if (r < 0) {
		ilerr__ser(r);
		goto unlock;
	}

	osal_mutex_unlock(net->sync.lock);

	/* wait for response */
	osal_mutex_lock(net->sync.lock);

	if (!net->sync.complete) {
		r = osal_cond_wait(net->sync.cond, net->sync.lock, timeout);
		if (r == OSAL_ETIMEDOUT) {
			ilerr__set("Reception timed out");
			r = IL_ETIMEDOUT;
		} else if (r < 0) {
			ilerr__set("Reception failed");
			r = IL_EFAIL;
		}
	} else {
		r = 0;
	}

unlock:
	net->sync.complete = 1;

	osal_mutex_unlock(net->sync.lock);
	osal_mutex_unlock(net->lock);

	return r;
}

int il_net__sw_subscribe(il_net_t *net, uint8_t id,
			 il_net_sw_subscriber_cb_t cb, void *ctx)
{
	int r = 0;

	osal_mutex_lock(net->sw_subs.lock);

	/* increase array if no space left */
	if (net->sw_subs.cnt == net->sw_subs.sz) {
		size_t sz;
		il_net_sw_subscriber_t *subs;

		/* double in size on each realloc */
		sz = 2 * net->sw_subs.sz * sizeof(*subs);
		subs = realloc(net->sw_subs.subs, sz);
		if (!subs) {
			ilerr__set("Subscribers re-allocation failed");
			r = IL_ENOMEM;
			goto unlock;
		}

		net->sw_subs.subs = subs;
		net->sw_subs.sz = sz;
	}

	net->sw_subs.subs[net->sw_subs.cnt].id = id;
	net->sw_subs.subs[net->sw_subs.cnt].cb = cb;
	net->sw_subs.subs[net->sw_subs.cnt].ctx = ctx;

	net->sw_subs.cnt++;

unlock:
	osal_mutex_unlock(net->sw_subs.lock);

	return r;
}

void il_net__sw_unsubscribe(il_net_t *net, uint8_t id)
{
	size_t i;

	osal_mutex_lock(net->sw_subs.lock);

	for (i = 0; i < net->sw_subs.cnt; i++) {
		if (net->sw_subs.subs[i].id == id) {
			/* move last to the current position, decrease */
			net->sw_subs.subs[i] =
				net->sw_subs.subs[net->sw_subs.cnt - 1];
			net->sw_subs.cnt--;
			break;
		}
	}

	osal_mutex_unlock(net->sw_subs.lock);
}

/*******************************************************************************
 * Public
 ******************************************************************************/

il_net_t *il_net_create(const char *port)
{
	il_net_t *net;
	ser_opts_t sopts = SER_OPTS_INIT;
	int r;
	uint8_t val;

	/* validate options */
	if (!port) {
		ilerr__set("Invalid port (NULL)");
		return NULL;
	}

	/* allocate net */
	net = malloc(sizeof(*net));
	if (!net) {
		ilerr__set("Network allocation failed");
		return NULL;
	}

	net->lock = osal_mutex_create();
	if (!net->lock) {
		ilerr__set("Network lock allocation failed");
		goto cleanup_net;
	}

	/* initialize network state */
	net->state_lock = osal_mutex_create();
	if (!net->state_lock) {
		ilerr__set("Network state lock allocation failed");
		goto cleanup_lock;
	}

	net->state = IL_NET_STATE_OPERATIVE;

	/* initialize synchronous transfers context */
	net->sync.lock = osal_mutex_create();
	if (!net->sync.lock) {
		ilerr__set("Network sync lock allocation failed");
		goto cleanup_state_lock;
	}

	net->sync.cond = osal_cond_create();
	if (!net->sync.cond) {
		ilerr__set("Network sync condition allocation failed");
		goto cleanup_sync_lock;
	}

	net->sync.complete = 1;

	/* initialize statusword update subscribers */
	net->sw_subs.subs = malloc(sizeof(*net->sw_subs.subs) * SW_SUBS_SZ_DEF);
	if (!net->sw_subs.subs) {
		ilerr__set("Network statusword subscribers allocation failed");
		goto cleanup_sync_cond;
	}

	net->sw_subs.lock = osal_mutex_create();
	if (!net->sw_subs.lock) {
		ilerr__set("Network statusword lock allocation failed");
		goto cleanup_sw_subs_subs;
	}

	net->sw_subs.cnt = 0;
	net->sw_subs.sz = SW_SUBS_SZ_DEF;

	/* allocate serial port */
	net->ser = ser_create();
	if (!net->ser) {
		ilerr__set("Serial port allocation failed (%s)", sererr_last());
		goto cleanup_sw_subs_lock;
	}

	/* open serial port */
	sopts.port = port;
	sopts.baudrate = BAUDRATE_DEF;
	sopts.timeouts.rd = TIMEOUT_RD_DEF;
	sopts.timeouts.wr = TIMEOUT_WR_DEF;

	r = ser_open(net->ser, &sopts);
	if (r < 0) {
		ilerr__set("Serial port open failed (%s)", sererr_last());
		goto cleanup_ser;
	}

	/* QUIRK: drive may not be operative immediately */
	osal_clock_sleep_ms(INIT_WAIT_TIME);

	/* send ascii message to force binary */
	r = ser_write(net->ser, MSG_A2B, sizeof(MSG_A2B) - 1, NULL);
	if (r < 0) {
		ilerr__set("Binary configuration failed (%s)", sererr_last());
		goto close_ser;
	}

	/* send the same message in binary (will flush if already on binary) */
	val = 1;
	r = il_net__write(net, 0, UARTCFG_BIN_IDX, UARTCFG_BIN_SIDX, &val,
			  sizeof(val));
	if (r < 0)
		goto close_ser;

	/* start listener thread */
	net->stop = 0;

	net->listener = osal_thread_create(listener, net);
	if (!net->listener) {
		ilerr__set("Listener thread creation failed");
		goto close_ser;
	}

	return net;

close_ser:
	ser_close(net->ser);

cleanup_ser:
	ser_destroy(net->ser);

cleanup_sw_subs_lock:
	osal_mutex_destroy(net->sw_subs.lock);

cleanup_sw_subs_subs:
	free(net->sw_subs.subs);

cleanup_sync_cond:
	osal_cond_destroy(net->sync.cond);

cleanup_sync_lock:
	osal_mutex_destroy(net->sync.lock);

cleanup_state_lock:
	osal_mutex_destroy(net->state_lock);

cleanup_lock:
	osal_mutex_destroy(net->lock);

cleanup_net:
	free(net);

	return NULL;
}

void il_net_destroy(il_net_t *net)
{
	/* validate net */
	if (!net)
		return;

	/* free resources */
	net->stop = 1;
	osal_thread_join(net->listener, NULL);

	ser_close(net->ser);

	osal_mutex_destroy(net->sw_subs.lock);
	free(net->sw_subs.subs);

	osal_cond_destroy(net->sync.cond);
	osal_mutex_destroy(net->sync.lock);

	osal_mutex_destroy(net->state_lock);

	osal_mutex_destroy(net->lock);

	ser_destroy(net->ser);
	free(net);
}

il_net_state_t il_net_state_get(il_net_t *net)
{
	il_net_state_t state;

	if (!net)
		return IL_NET_STATE_UNKNOWN;

	osal_mutex_lock(net->state_lock);
	state = net->state;
	osal_mutex_unlock(net->state_lock);

	return state;
}

il_net_dev_list_t *il_net_dev_list_get()
{
	il_net_dev_list_t *lst = NULL;
	il_net_dev_list_t *prev;

	ser_dev_list_t *ser_devs;
	ser_dev_list_t *ser_dev;

	/* obtain all serial ports */
	ser_devs = ser_dev_list_get();
	if (!ser_devs)
		return NULL;

	/* create the device list */
	ser_dev_list_foreach(ser_dev, ser_devs) {
		/* allocate new list entry */
		prev = lst;
		lst = malloc(sizeof(*lst));
		if (!lst) {
			il_net_dev_list_destroy(prev);
			break;
		}

		lst->next = prev;

		/* store port */
		strncpy(lst->port, ser_dev->dev.path, sizeof(lst->port));
	}

	ser_dev_list_destroy(ser_devs);

	return lst;
}

void il_net_dev_list_destroy(il_net_dev_list_t *lst)
{
	il_net_dev_list_t *curr;

	curr = lst;
	while (curr) {
		il_net_dev_list_t *tmp;

		tmp = curr->next;
		free(curr);
		curr = tmp;
	}
}

il_net_dev_mon_t *il_net_dev_mon_create()
{
	il_net_dev_mon_t *mon;

	/* allocate monitor */
	mon = malloc(sizeof(*mon));
	if (!mon) {
		ilerr__set("Monitor allocation failed");
		return NULL;
	}

	mon->running = 0;

	return mon;
}

int il_net_dev_mon_start(il_net_dev_mon_t *mon, il_net_dev_on_evt_t on_evt,
			 void *ctx)
{
	/* validate arguments */
	if (!mon) {
		ilerr__set("Invalid monitor (NULL)");
		return IL_EFAULT;
	}

	if (mon->running) {
		ilerr__set("Monitor already running");
		return IL_EALREADY;
	}

	if (!on_evt) {
		ilerr__set("Invalid callback (NULL)");
		return IL_EINVAL;
	}

	/* store context and bring up monitor */
	mon->ctx = ctx;
	mon->on_evt = on_evt;
	mon->mon = ser_dev_monitor_init(on_ser_evt, mon);
	if (!mon->mon) {
		ilerr__set("Could not initialize monitor (%s)", sererr_last());
		return IL_EFAIL;
	}

	mon->running = 1;

	return 0;
}

void il_net_dev_mon_stop(il_net_dev_mon_t *mon)
{
	/* validate arguments */
	if (!mon)
		return;

	if (mon->running) {
		ser_dev_monitor_stop(mon->mon);
		mon->running = 0;
	}
}

void il_net_dev_mon_destroy(il_net_dev_mon_t *mon)
{
	/* validate arguments */
	if (!mon)
		return;

	il_net_dev_mon_stop(mon);

	free(mon);
}

il_net_axes_list_t *il_net_axes_list_get(il_net_t *net,
					 il_net_axes_on_found_t on_found,
					 void *ctx)
{
	int r;
	uint8_t id;
	il_frame_t frame;

	il_net_axes_list_t *lst = NULL;
	il_net_axes_list_t *prev;

	/* validate network */
	if (!net) {
		ilerr__set("Invalid network (NULL)");
		return NULL;
	}

	/* check network state */
	if (il_net_state_get(net) != IL_NET_STATE_OPERATIVE) {
		ilerr__set("Network is not operative");
		return NULL;
	}

	osal_mutex_lock(net->lock);

	/* register synchronous transfer */
	osal_mutex_lock(net->sync.lock);

	net->sync.id = 0;
	net->sync.idx = UARTCFG_ID_IDX;
	net->sync.sidx = UARTCFG_ID_SIDX;
	net->sync.buf = &id;
	net->sync.sz = sizeof(id);
	net->sync.recvd = NULL;
	net->sync.complete = 0;

	il_frame__init(&frame, 0, UARTCFG_ID_IDX, UARTCFG_ID_SIDX, NULL, 0);

	/* broadcast "read node id" */
	r = ser_write(net->ser, frame.buf, frame.sz, NULL);
	if (r < 0) {
		ilerr__ser(r);
		goto unlock;
	}

	osal_mutex_unlock(net->sync.lock);

	/* wait for responses */
	osal_mutex_lock(net->sync.lock);

	while (r == 0) {
		if (net->sync.complete) {
			net->sync.complete = 0;

			/* allocate new list entry */
			prev = lst;
			lst = malloc(sizeof(*lst));
			if (!lst) {
				il_net_axes_list_destroy(prev);
				break;
			}

			lst->next = prev;
			lst->id = id;

			if (on_found)
				on_found(ctx, id);
		} else {
			r = osal_cond_wait(net->sync.cond, net->sync.lock,
					   SCAN_TIMEOUT);
		}
	}

	osal_mutex_unlock(net->sync.lock);

unlock:
	osal_mutex_unlock(net->lock);

	return lst;
}

void il_net_axes_list_destroy(il_net_axes_list_t *lst)
{
	il_net_axes_list_t *curr;

	curr = lst;
	while (curr) {
		il_net_axes_list_t *tmp;

		tmp = curr->next;
		free(curr);
		curr = tmp;
	}
}
