/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interface between the native_sim GNSS driver and its host side. Plain C
 * types only: this header is shared by the Zephyr and the host builds.
 */

#ifndef GNSS_NATIVE_SIM_BOTTOM_H
#define GNSS_NATIVE_SIM_BOTTOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gnss_ns_fix_kind {
	/* No position: none yet, the source stopped answering, or its fix is too old */
	GNSS_NS_FIX_NONE,
	/* Satellite fix: Android's gps provider, or a fixed position */
	GNSS_NS_FIX_GNSS,
	/* Estimated without satellites: Android's fused or network provider */
	GNSS_NS_FIX_ESTIMATED,
};

struct gnss_ns_fix {
	/* Nanodegrees */
	int64_t latitude;
	int64_t longitude;
	/* Millimetres above mean sea level */
	int32_t altitude;
	/* Millimetres per second */
	uint32_t speed;
	/* Millidegrees from true north */
	uint32_t bearing;
	/* Horizontal accuracy in millimetres */
	uint32_t accuracy;
	enum gnss_ns_fix_kind kind;
	/* Host wall clock, UTC, at gnss_ns_bottom_get() */
	uint16_t utc_millisecond; /* Within the minute */
	uint8_t utc_hour;
	uint8_t utc_minute;
	uint8_t utc_month_day;
	uint8_t utc_month;
	uint8_t utc_century_year;
};

/*
 * Choose the source. fixed_position is "<lat>,<lon>[,<alt>]" (degrees, metres
 * above sea level) to report that position, or NULL to read the host location
 * every poll_ms. A host fix older than max_age_ms is reported as no fix; 0
 * keeps fixes of any age. Returns 0, or -1 when fixed_position is not valid.
 */
int gnss_ns_bottom_init(const char *fixed_position, unsigned int poll_ms,
			unsigned int max_age_ms);

/* Starts or resumes reading the host source */
void gnss_ns_bottom_start(void);

/* Pauses reading; the last fix is dropped */
void gnss_ns_bottom_stop(void);

/* Copies the newest fix, stamped with the current host time */
void gnss_ns_bottom_get(struct gnss_ns_fix *fix);

#ifdef __cplusplus
}
#endif

#endif /* GNSS_NATIVE_SIM_BOTTOM_H */
