/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 * SPDX-License-Identifier: Apache-2.0
 *
 * GNSS receiver for native_sim. The position comes from the host (see
 * gnss_native_sim_bottom.c); this side publishes it through the GNSS API at
 * the fix rate, like a receiver that streams one fix per interval.
 *
 * The host sources do not say how many satellites were used or what the
 * dilution of precision was, so a satellite fix reports 8 satellites and the
 * HDOP is derived from the horizontal accuracy (HDOP times a 5 m user range
 * error); an estimated fix reports no satellites.
 */

#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L /* gmtime_r */

#include <time.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "gnss_native_sim_bottom.h"

LOG_MODULE_REGISTER(gnss_native_sim, CONFIG_GNSS_LOG_LEVEL);

#define DT_DRV_COMPAT zephyr_native_sim_gnss

/* There is one host location, so one receiver */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "only one zephyr,native-sim-gnss instance is supported");

#define DEFAULT_FIX_INTERVAL_MS 1000
#define MIN_FIX_INTERVAL_MS     100
#define SATS_FOR_GNSS_FIX       8
#define USER_RANGE_ERROR_MM     5
#define MIN_HDOP                500

struct gnss_ns_data {
	const struct device *dev;
	struct k_work_delayable work;
	struct k_mutex lock;
	uint32_t fix_interval_ms;
	bool started;
};

static void gnss_ns_fill(struct gnss_data *out, const struct gnss_ns_fix *fx)
{
	time_t secs = (time_t)(fx->utc_ms / 1000);
	struct tm tm;
	uint32_t hdop;

	*out = (struct gnss_data){0};

	gmtime_r(&secs, &tm);
	out->utc.hour = tm.tm_hour;
	out->utc.minute = tm.tm_min;
	out->utc.millisecond = tm.tm_sec * 1000 + fx->utc_ms % 1000;
	out->utc.month_day = tm.tm_mday;
	out->utc.month = tm.tm_mon + 1;
	out->utc.century_year = tm.tm_year % 100;

	if (fx->kind == GNSS_NS_FIX_NONE) {
		out->info.fix_status = GNSS_FIX_STATUS_NO_FIX;
		out->info.fix_quality = GNSS_FIX_QUALITY_INVALID;
		return;
	}

	out->nav_data.latitude = fx->latitude;
	out->nav_data.longitude = fx->longitude;
	out->nav_data.altitude = fx->altitude;
	out->nav_data.speed = fx->speed;
	out->nav_data.bearing = fx->bearing;

	hdop = fx->accuracy / USER_RANGE_ERROR_MM;
	out->info.hdop = MAX(hdop, MIN_HDOP);

	if (fx->kind == GNSS_NS_FIX_GNSS) {
		out->info.fix_status = GNSS_FIX_STATUS_GNSS_FIX;
		out->info.fix_quality = GNSS_FIX_QUALITY_GNSS_SPS;
		out->info.satellites_cnt = SATS_FOR_GNSS_FIX;
	} else {
		out->info.fix_status = GNSS_FIX_STATUS_ESTIMATED_FIX;
		out->info.fix_quality = GNSS_FIX_QUALITY_ESTIMATED;
	}
}

static void gnss_ns_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct gnss_ns_data *data = CONTAINER_OF(dwork, struct gnss_ns_data, work);
	struct gnss_ns_fix fx;
	struct gnss_data out;

	gnss_ns_bottom_get(&fx);
	gnss_ns_fill(&out, &fx);
	gnss_publish_data(data->dev, &out);

	/* Rejected while gnss_ns_stop() is cancelling, so no fix follows a stop */
	k_work_schedule(dwork, K_MSEC(data->fix_interval_ms));
}

static int gnss_ns_start(const struct device *dev, enum gnss_start_mode mode)
{
	struct gnss_ns_data *data = dev->data;

	if (mode > GNSS_COLD_START) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->started) {
		data->started = true;
		gnss_ns_bottom_start();
		k_work_schedule(&data->work, K_MSEC(data->fix_interval_ms));
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static int gnss_ns_stop(const struct device *dev)
{
	struct gnss_ns_data *data = dev->data;
	struct k_work_sync sync;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->started) {
		data->started = false;
		(void)k_work_cancel_delayable_sync(&data->work, &sync);
		gnss_ns_bottom_stop();
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static int gnss_ns_set_fix_rate(const struct device *dev, uint32_t fix_interval_ms)
{
	struct gnss_ns_data *data = dev->data;

	if (fix_interval_ms < MIN_FIX_INTERVAL_MS) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->fix_interval_ms = fix_interval_ms;
	if (data->started) {
		k_work_reschedule(&data->work, K_MSEC(fix_interval_ms));
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static int gnss_ns_get_fix_rate(const struct device *dev, uint32_t *fix_interval_ms)
{
	struct gnss_ns_data *data = dev->data;

	*fix_interval_ms = data->fix_interval_ms;
	return 0;
}

static DEVICE_API(gnss, gnss_ns_api) = {
	.start = gnss_ns_start,
	.stop = gnss_ns_stop,
	.set_fix_rate = gnss_ns_set_fix_rate,
	.get_fix_rate = gnss_ns_get_fix_rate,
};

static int gnss_ns_init(const struct device *dev)
{
	struct gnss_ns_data *data = dev->data;

	data->dev = dev;
	data->fix_interval_ms = DEFAULT_FIX_INTERVAL_MS;
	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->work, gnss_ns_work_handler);

	/* Streams from power on, like the NMEA receivers */
	return gnss_ns_start(dev, GNSS_HOT_START);
}

#define GNSS_NS_DEFINE(inst)                                                                       \
	static struct gnss_ns_data gnss_ns_data_##inst;                                            \
	DEVICE_DT_INST_DEFINE(inst, gnss_ns_init, NULL, &gnss_ns_data_##inst, NULL, POST_KERNEL,   \
			      CONFIG_GNSS_INIT_PRIORITY, &gnss_ns_api);

DT_INST_FOREACH_STATUS_OKAY(GNSS_NS_DEFINE)
