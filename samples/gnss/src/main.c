/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>

#define NANO 1000000000LL

static void print_coord(const char *label, int64_t nanodeg, char pos, char neg)
{
	char hemi = (nanodeg < 0) ? neg : pos;
	int64_t abs = (nanodeg < 0) ? -nanodeg : nanodeg;

	printf("%s %lld.%06lld %c", label, abs / NANO, (abs % NANO) / 1000, hemi);
}

static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	const struct navigation_data *nav = &data->nav_data;
	const struct gnss_time *t = &data->utc;

	if (data->info.fix_status == GNSS_FIX_STATUS_NO_FIX) {
		printf("no fix (%u satellites)\n", data->info.satellites_cnt);
		return;
	}

	printf("%02u:%02u:%02u UTC  ", t->hour, t->minute, t->millisecond / 1000);
	print_coord("lat", nav->latitude, 'N', 'S');
	print_coord("  lon", nav->longitude, 'E', 'W');
	printf("  alt %s%d.%d m  speed %u.%u km/h  sats %u  hdop %u.%03u\n",
	       (nav->altitude < 0) ? "-" : "", abs(nav->altitude) / 1000,
	       (abs(nav->altitude) % 1000) / 100,
	       nav->speed * 36 / 10000, (nav->speed * 36 / 1000) % 10,
	       data->info.satellites_cnt, data->info.hdop / 1000, data->info.hdop % 1000);
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), gnss_data_cb);

int main(void)
{
	printf("Waiting for GNSS fixes\n");
	return 0;
}
