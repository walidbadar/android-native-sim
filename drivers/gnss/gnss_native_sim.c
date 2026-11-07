/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>

#include "cmdline.h"
#include "posix_native_task.h"
#include "gnss_native_sim_bottom.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gnss_native_sim, CONFIG_GNSS_LOG_LEVEL);

#define DT_DRV_COMPAT zephyr_native_sim_gnss

/* There is one host location, so one receiver */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "only one zephyr,native-sim-gnss instance is supported");

/* The shortest fix interval accepted, as on 10 Hz receivers */
#define MIN_FIX_INTERVAL_MS 100
#define USER_RANGE_ERROR_MM 5
#define MIN_HDOP            500

struct gnss_ns_data {
	const struct device *dev;
	struct k_work_delayable work;
	struct k_mutex lock;
	uint32_t fix_interval_ms;
	bool started;
	/* --<node>_position from the command line, NULL when not given */
	char *position_arg;
};

static void gnss_ns_fill(struct gnss_data *out, const struct gnss_ns_fix *fx)
{
	uint32_t hdop;

	*out = (struct gnss_data){0};

	out->utc.hour = fx->utc_hour;
	out->utc.minute = fx->utc_minute;
	out->utc.millisecond = fx->utc_millisecond;
	out->utc.month_day = fx->utc_month_day;
	out->utc.month = fx->utc_month;
	out->utc.century_year = fx->utc_century_year;

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

/* Every start mode is accepted; the host has no almanac to keep or drop */
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

/* Only changes how often the newest host position is published */
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
	data->fix_interval_ms = CONFIG_GNSS_NATIVE_SIM_FIX_INTERVAL_MS;
	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->work, gnss_ns_work_handler);

	if (gnss_ns_bottom_init(data->position_arg, CONFIG_GNSS_NATIVE_SIM_POLL_PERIOD_MS,
				CONFIG_GNSS_NATIVE_SIM_MAX_FIX_AGE_MS) < 0) {
		LOG_ERR("%s is not <lat>,<lon>[,<alt>] in degrees and metres",
			data->position_arg);
		return -EINVAL;
	}

	/* Streams from power on, like the NMEA receivers */
	return gnss_ns_start(dev, GNSS_HOT_START);
}

#define GNSS_NS_DEFINE(inst)                                                                       \
	static struct gnss_ns_data gnss_ns_data_##inst;                                            \
	DEVICE_DT_INST_DEFINE(inst, gnss_ns_init, NULL, &gnss_ns_data_##inst, NULL, POST_KERNEL,   \
			      CONFIG_GNSS_INIT_PRIORITY, &gnss_ns_api);

DT_INST_FOREACH_STATUS_OKAY(GNSS_NS_DEFINE)

#define INST_NAME(inst) DEVICE_DT_NAME(DT_DRV_INST(inst))

#define GNSS_NS_COMMAND_LINE_OPTS(inst)                                                            \
	{                                                                                          \
		.option = INST_NAME(inst) "_position",                                             \
		.name = "lat,lon[,alt]",                                                           \
		.type = 's',                                                                       \
		.dest = (void *)&gnss_ns_data_##inst.position_arg,                                 \
		.descript = "Report this fixed position (degrees, metres above sea level) from "   \
			    INST_NAME(inst) " instead of the host location",                       \
	},

static void gnss_ns_options(void)
{
	static struct args_struct_t options[] = {
		DT_INST_FOREACH_STATUS_OKAY(GNSS_NS_COMMAND_LINE_OPTS)
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(options);
}

NATIVE_TASK(gnss_ns_options, PRE_BOOT_1, 1);
