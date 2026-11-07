/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 * SPDX-License-Identifier: Apache-2.0
 *
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
	/* No position yet, or the source stopped answering */
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
	/* Host wall clock at gnss_ns_bottom_get(), milliseconds since the epoch */
	int64_t utc_ms;
	/* Millimetres above mean sea level */
	int32_t altitude;
	/* Millimetres per second */
	uint32_t speed;
	/* Millidegrees from true north */
	uint32_t bearing;
	/* Horizontal accuracy in millimetres */
	uint32_t accuracy;
	enum gnss_ns_fix_kind kind;
};

/* Starts or resumes polling the host source */
void gnss_ns_bottom_start(void);

/* Pauses polling; the last fix is dropped */
void gnss_ns_bottom_stop(void);

/* Copies the newest fix, stamped with the current host time */
void gnss_ns_bottom_get(struct gnss_ns_fix *fix);

#ifdef __cplusplus
}
#endif

#endif /* GNSS_NATIVE_SIM_BOTTOM_H */
