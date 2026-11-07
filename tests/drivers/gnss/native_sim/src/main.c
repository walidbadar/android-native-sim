/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runs with -gnss_position (see prj.conf), so no phone is involved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

static const struct device *const gnss = DEVICE_DT_GET(DT_NODELABEL(gnss));

static K_SEM_DEFINE(fix_sem, 0, 1);
static atomic_t fix_count;
static struct gnss_data last;

static void gnss_cb(const struct device *dev, const struct gnss_data *data)
{
	last = *data;
	atomic_inc(&fix_count);
	k_sem_give(&fix_sem);
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(gnss)), gnss_cb);

static int fixes_during(k_timeout_t period)
{
	atomic_set(&fix_count, 0);
	k_sleep(period);
	return atomic_get(&fix_count);
}

ZTEST(gnss_native_sim, test_fixed_position)
{
	zassert_ok(k_sem_take(&fix_sem, K_MSEC(1500)), "no fix published");

	zassert_equal(last.nav_data.latitude, 52370216000LL);
	zassert_equal(last.nav_data.longitude, -4895168000LL);
	zassert_equal(last.nav_data.altitude, 12500);
	zassert_equal(last.info.fix_status, GNSS_FIX_STATUS_GNSS_FIX);
	zassert_equal(last.info.fix_quality, GNSS_FIX_QUALITY_GNSS_SPS);
	zassert_equal(last.info.satellites_cnt, 8);
	zassert_equal(last.info.hdop, 500);
	zassert_between_inclusive(last.utc.month, 1, 12);
	zassert_between_inclusive(last.utc.month_day, 1, 31);
	zassert_true(last.utc.millisecond < 61000);
}

ZTEST(gnss_native_sim, test_fix_rate)
{
	uint32_t interval;

	zassert_ok(gnss_get_fix_rate(gnss, &interval));
	zassert_equal(interval, 1000);
	zassert_equal(gnss_set_fix_rate(gnss, 50), -EINVAL);

	zassert_ok(gnss_set_fix_rate(gnss, 200));
	zassert_ok(gnss_get_fix_rate(gnss, &interval));
	zassert_equal(interval, 200);
	zassert_between_inclusive(fixes_during(K_MSEC(1000)), 4, 6);

	zassert_ok(gnss_set_fix_rate(gnss, 1000));
}

ZTEST(gnss_native_sim, test_stop_start)
{
	zassert_equal(gnss_start(gnss, GNSS_COLD_START + 1), -EINVAL);

	zassert_ok(gnss_stop(gnss));
	zassert_equal(fixes_during(K_MSEC(2500)), 0, "fix published while stopped");

	k_sem_reset(&fix_sem);
	zassert_ok(gnss_start(gnss, GNSS_HOT_START));
	zassert_ok(k_sem_take(&fix_sem, K_MSEC(1500)), "no fix after restart");
	zassert_equal(last.info.fix_status, GNSS_FIX_STATUS_GNSS_FIX);
}

ZTEST_SUITE(gnss_native_sim, NULL, NULL, NULL, NULL, NULL);
