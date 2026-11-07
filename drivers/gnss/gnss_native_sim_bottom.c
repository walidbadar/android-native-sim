/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host side of the native_sim GNSS driver: finds out where the host is.
 *
 * With -gnss_position=<lat>,<lon>[,<alt>] the position is fixed. Otherwise it
 * comes from an Android phone: the adb shell user may read the system
 * location state, so "dumpsys location" gives the newest fix of each provider
 * without any app on the phone. It runs directly when the executable is
 * itself on the phone as the shell user, and through adb otherwise (from a
 * laptop over USB, or from Termux over wireless debugging).
 *
 * dumpsys takes a while, through adb even more, so a host thread polls it
 * once a second and the driver only ever reads the newest result.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <nsi_cmdline.h>
#include <nsi_tasks.h>
#include <nsi_tracing.h>

#include "gnss_native_sim_bottom.h"

#define POLL_INTERVAL_S 1

static char *position_arg;

static bool fixed_source;
static struct gnss_ns_fix fixed_fix;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool running;
static bool poller_started;
static struct gnss_ns_fix latest;

struct android_fix {
	double lat;
	double lon;
	double alt;
	double speed;
	double bearing;
	double hacc;
	bool valid;
};

static int64_t to_nano(double deg)
{
	return (int64_t)(deg * 1e9 + ((deg < 0) ? -0.5 : 0.5));
}

static uint32_t to_milli(double v)
{
	return (v <= 0.0) ? 0 : (uint32_t)(v * 1000.0 + 0.5);
}

static int64_t host_utc_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static double attr(const char *s, const char *key, double def)
{
	const char *p = strstr(s, key);

	return (p != NULL) ? strtod(p + strlen(key), NULL) : def;
}

/*
 * Lines look like
 *   Location[gps 52.370216,4.895168 hAcc=3.8 et=+2d1h alt=41.2 vel=0.4 bear=87.0 ...]
 * Keeps the first fix of each provider; dumpsys lists the newest first.
 */
static void parse_dump(FILE *f, struct android_fix *gps, struct android_fix *fused,
		       struct android_fix *network)
{
	char line[512];

	while (fgets(line, sizeof(line), f) != NULL) {
		char provider[16];
		struct android_fix fx = {0};
		char *p = strstr(line, "Location[");
		char *end;

		if (p == NULL) {
			continue;
		}
		p += strlen("Location[");
		end = strchr(p, ']');
		if (end == NULL || sscanf(p, "%15s %lf,%lf", provider, &fx.lat, &fx.lon) != 3) {
			continue;
		}
		*end = '\0';
		fx.alt = attr(p, "alt=", 0.0);
		fx.speed = attr(p, "vel=", 0.0);
		fx.bearing = attr(p, "bear=", 0.0);
		fx.hacc = attr(p, "hAcc=", 10.0);
		fx.valid = true;

		if (strcmp(provider, "gps") == 0 && !gps->valid) {
			*gps = fx;
		} else if (strcmp(provider, "fused") == 0 && !fused->valid) {
			*fused = fx;
		} else if (strcmp(provider, "network") == 0 && !network->valid) {
			*network = fx;
		}
	}
}

/* Runs argv with its stdout on a pipe; no shell involved, Android has no /bin/sh */
static FILE *run_cmd(char *const argv[], pid_t *pid)
{
	int fds[2];

	if (pipe(fds) < 0) {
		return NULL;
	}
	*pid = fork();
	if (*pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return NULL;
	}
	if (*pid == 0) {
		int null = open("/dev/null", O_WRONLY);

		dup2(fds[1], STDOUT_FILENO);
		if (null >= 0) {
			dup2(null, STDERR_FILENO);
		}
		close(fds[0]);
		close(fds[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	return fdopen(fds[0], "r");
}

/*
 * The location dump is readable by the adb shell user. When the binary already
 * runs as that user (started through "adb shell") dumpsys works directly;
 * anywhere else it has to go through adb, which Termux can point at the phone
 * itself over wireless debugging.
 */
static bool read_android(struct gnss_ns_fix *out)
{
	static int source = -1;
	char *direct[] = {"dumpsys", "location", NULL};
	/* adb picks the device from ANDROID_SERIAL when several are connected */
	char *via_adb[] = {"adb", "shell", "dumpsys", "location", NULL};
	char **cmds[] = {direct, via_adb};

	for (int i = (source < 0) ? 0 : source; i < 2; i++) {
		struct android_fix gps = {0}, fused = {0}, network = {0};
		const struct android_fix *fx;
		pid_t pid;
		FILE *f = run_cmd(cmds[i], &pid);

		if (f == NULL) {
			continue;
		}
		parse_dump(f, &gps, &fused, &network);
		fclose(f);
		waitpid(pid, NULL, 0);

		if (!gps.valid && !fused.valid && !network.valid) {
			continue;
		}
		if (source != i) {
			source = i;
			nsi_print_trace("gnss_native_sim: reading the location %s\n",
					(i == 0) ? "with dumpsys" : "through adb");
		}
		fx = gps.valid ? &gps : fused.valid ? &fused : &network;
		*out = (struct gnss_ns_fix){
			.latitude = to_nano(fx->lat),
			.longitude = to_nano(fx->lon),
			.altitude = (int32_t)(fx->alt * 1000.0),
			.speed = to_milli(fx->speed),
			.bearing = to_milli(fx->bearing) % 360000,
			.accuracy = to_milli(fx->hacc),
			.kind = gps.valid ? GNSS_NS_FIX_GNSS : GNSS_NS_FIX_ESTIMATED,
		};
		return true;
	}
	return false;
}

static void *poller(void *arg)
{
	bool had_fix = true;

	(void)arg;

	for (;;) {
		struct gnss_ns_fix fx = {0};
		bool ok;

		pthread_mutex_lock(&lock);
		while (!running) {
			pthread_cond_wait(&wake, &lock);
		}
		pthread_mutex_unlock(&lock);

		ok = read_android(&fx);

		pthread_mutex_lock(&lock);
		if (running) {
			latest = fx;
		}
		pthread_mutex_unlock(&lock);

		if (ok != had_fix) {
			had_fix = ok;
			if (ok) {
				nsi_print_trace("gnss_native_sim: location available again\n");
			} else {
				nsi_print_warning("gnss_native_sim: no location from dumpsys or adb\n");
			}
		}
		sleep(POLL_INTERVAL_S);
	}
	return NULL;
}

void gnss_ns_bottom_start(void)
{
	pthread_t thread;

	if (fixed_source) {
		return;
	}

	pthread_mutex_lock(&lock);
	running = true;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);

	if (!poller_started) {
		poller_started = true;
		if (pthread_create(&thread, NULL, poller, NULL) != 0) {
			nsi_print_error_and_exit("gnss_native_sim: cannot start the poll thread\n");
		}
		pthread_detach(thread);
	}
}

void gnss_ns_bottom_stop(void)
{
	pthread_mutex_lock(&lock);
	running = false;
	memset(&latest, 0, sizeof(latest));
	pthread_mutex_unlock(&lock);
}

void gnss_ns_bottom_get(struct gnss_ns_fix *fix)
{
	if (fixed_source) {
		*fix = fixed_fix;
	} else {
		pthread_mutex_lock(&lock);
		*fix = latest;
		pthread_mutex_unlock(&lock);
	}
	fix->utc_ms = host_utc_ms();
}

static void gnss_ns_options(void)
{
	static struct args_struct_t options[] = {
		{.option = "gnss_position", .name = "lat,lon[,alt]", .type = 's',
		 .dest = &position_arg,
		 .descript = "Report this fixed position (degrees, metres above sea level) "
			     "instead of reading an Android phone's location"},
		ARG_TABLE_ENDMARKER,
	};

	nsi_add_command_line_opts(options);
}

NSI_TASK(gnss_ns_options, PRE_BOOT_1, 10);

static void gnss_ns_parse_position(void)
{
	double lat, lon, alt = 0.0;
	int len = -1;
	int n;

	if (position_arg == NULL) {
		return;
	}

	/* len ends up past the last number read, so trailing junk is caught */
	n = sscanf(position_arg, "%lf,%lf%n,%lf%n", &lat, &lon, &len, &alt, &len);
	if ((n != 2 && n != 3) || len < 0 || position_arg[len] != '\0' ||
	    lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
		nsi_print_error_and_exit("gnss_native_sim: -gnss_position=%s is not "
					 "<lat>,<lon>[,<alt>] in degrees and metres\n",
					 position_arg);
	}

	fixed_source = true;
	fixed_fix = (struct gnss_ns_fix){
		.latitude = to_nano(lat),
		.longitude = to_nano(lon),
		.altitude = (int32_t)(alt * 1000.0),
		.accuracy = 1000,
		.kind = GNSS_NS_FIX_GNSS,
	};
}

NSI_TASK(gnss_ns_parse_position, PRE_BOOT_2, 10);
