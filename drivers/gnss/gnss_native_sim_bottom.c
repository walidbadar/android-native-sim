/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <nsi_tasks.h>
#include <nsi_tracing.h>

#include "gnss_native_sim_bottom.h"

/* The newest fix of each provider, then the phone's uptime in seconds since boot */
#define DUMP_CMD "dumpsys location; cat /proc/uptime"

/* No answer from the host for this many poll periods means the location is lost */
#define LOST_AFTER_POLLS 3

/* Assumed when a fix does not say how accurate it is, in metres */
#define DEFAULT_ACCURACY_M 10.0

static unsigned int poll_ms;
static unsigned int max_age_ms;

static bool fixed_source;
static struct gnss_ns_fix fixed_fix;

static atomic_bool running;
static atomic_bool exiting;
static sem_t kick;
static bool poller_started;
static pthread_t poller_thread;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct gnss_ns_fix latest;
/* Monotonic time of the newest read, milliseconds */
static int64_t latest_at_ms;

struct android_fix {
	double lat;
	double lon;
	double alt;
	double speed;
	double bearing;
	double hacc;
	/* Phone uptime when the fix was taken, -1 when not printed */
	int64_t et_ms;
	bool valid;
};

struct dump {
	struct android_fix gps;
	struct android_fix fused;
	struct android_fix network;
	/* Phone uptime now, -1 when not printed */
	int64_t uptime_ms;
};

/* The command being read, killed at exit so no adb is left behind */
static volatile pid_t child_pid;

/*
 * Runs argv with its stdout on a pipe, returned as a stream, or NULL when it
 * cannot run. No shell is involved on this side: argv[0] is searched in PATH.
 */
static FILE *host_open(char *const argv[], void **ctx)
{
	int fds[2];
	pid_t pid;
	FILE *out;

	if (pipe(fds) < 0) {
		return NULL;
	}
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return NULL;
	}
	if (pid == 0) {
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
	child_pid = pid;
	out = fdopen(fds[0], "r");
	if (out == NULL) {
		close(fds[0]);
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
		}
		child_pid = 0;
	}
	*ctx = (void *)(intptr_t)pid;
	return out;
}

static void host_close(FILE *out, void *ctx)
{
	pid_t pid = (pid_t)(intptr_t)ctx;

	fclose(out);
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
	}
	child_pid = 0;
}

/* Stops the poll thread, and the command it may be reading, before the runner exits */
static void gnss_ns_bottom_cleanup(void)
{
	atomic_store(&exiting, true);
	atomic_store(&running, false);
	if (child_pid > 0) {
		kill(child_pid, SIGTERM);
	}
	if (poller_started) {
		sem_post(&kick);
		pthread_join(poller_thread, NULL);
	}
}

NSI_TASK(gnss_ns_bottom_cleanup, ON_EXIT_PRE, 10);

static int64_t to_nano(double deg)
{
	return (int64_t)(deg * 1e9 + ((deg < 0) ? -0.5 : 0.5));
}

static uint32_t to_milli(double v)
{
	return (v <= 0.0) ? 0 : (uint32_t)(v * 1000.0 + 0.5);
}

static int64_t monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Sleeps in short slices, so that an exit is not held up by a whole poll period */
static void sleep_ms(unsigned int ms)
{
	while (ms > 0 && !atomic_load(&exiting)) {
		unsigned int slice = (ms < 100) ? ms : 100;
		struct timespec ts = {.tv_nsec = (long)slice * 1000000L};

		while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
		}
		ms -= slice;
	}
}

static double attr(const char *s, const char *key, double def)
{
	const char *p = strstr(s, key);

	return (p != NULL) ? strtod(p + strlen(key), NULL) : def;
}

/*
 * A duration as Android prints it, "+1d2h3m4s567ms" with any of the parts,
 * to milliseconds; -1 when it is not one.
 */
static int64_t parse_duration(const char *s)
{
	int64_t total = 0;
	bool neg = false;

	if (*s == '+') {
		s++;
	} else if (*s == '-') {
		neg = true;
		s++;
	}
	if (!isdigit((unsigned char)*s)) {
		return -1;
	}
	while (isdigit((unsigned char)*s)) {
		char *end;
		int64_t v = strtoll(s, &end, 10);

		if (strncmp(end, "ms", 2) == 0) {
			total += v;
			end += 2;
		} else if (*end == 'd') {
			total += v * 86400000LL;
			end++;
		} else if (*end == 'h') {
			total += v * 3600000LL;
			end++;
		} else if (*end == 'm') {
			total += v * 60000LL;
			end++;
		} else if (*end == 's') {
			total += v * 1000LL;
			end++;
		} else {
			return -1;
		}
		s = end;
	}
	return neg ? -total : total;
}

/*
 * Lines look like
 *   last location=Location[gps 52.370216,4.895168 hAcc=3.8 et=+2d1h alt=41.2 vel=0.4 bear=87.0 ...]
 * and the output ends with the "<uptime> <idle>" line of /proc/uptime. Keeps
 * the first fix of each provider, which is the newest.
 */
static void parse_dump(FILE *f, struct dump *d)
{
	char line[512];

	while (fgets(line, sizeof(line), f) != NULL) {
		char provider[16];
		struct android_fix fx = {0};
		char *p = strstr(line, "Location[");
		char *end;
		const char *et;

		if (p == NULL) {
			double up, idle;
			int len = -1;

			if (sscanf(line, "%lf %lf%n", &up, &idle, &len) == 2 && len > 0 &&
			    (line[len] == '\n' || line[len] == '\0')) {
				d->uptime_ms = (int64_t)(up * 1000.0);
			}
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
		fx.hacc = attr(p, "hAcc=", DEFAULT_ACCURACY_M);
		et = strstr(p, "et=");
		fx.et_ms = (et != NULL) ? parse_duration(et + 3) : -1;
		fx.valid = true;

		if (strcmp(provider, "gps") == 0 && !d->gps.valid) {
			d->gps = fx;
		} else if (strcmp(provider, "fused") == 0 && !d->fused.valid) {
			d->fused = fx;
		} else if (strcmp(provider, "network") == 0 && !d->network.valid) {
			d->network = fx;
		}
	}
}

/* A fix is usable when it is fresh enough, or when its age cannot be told */
static bool usable(const struct android_fix *fx, int64_t uptime_ms)
{
	if (!fx->valid) {
		return false;
	}
	if (max_age_ms == 0 || uptime_ms < 0 || fx->et_ms < 0) {
		return true;
	}
	return uptime_ms - fx->et_ms <= (int64_t)max_age_ms;
}

/*
 * The location dump is readable by the adb shell user. When the executable
 * already runs as that user (started through "adb shell") dumpsys works
 * directly; anywhere else it has to go through adb, which Termux can point at
 * the phone itself over wireless debugging. The source that answered last is
 * tried first.
 */
static bool read_android(struct gnss_ns_fix *out)
{
	static int last_source = -1;
	static char *const direct[] = {"sh", "-c", DUMP_CMD, NULL};
	/* adb picks the device from ANDROID_SERIAL when several are connected */
	static char *const via_adb[] = {"adb", "shell", DUMP_CMD, NULL};
	static char *const *const cmds[] = {direct, via_adb};
	static const char *const names[] = {"with dumpsys", "through adb"};

	for (int n = 0; n < 2; n++) {
		int i = (last_source < 0) ? n : (last_source + n) % 2;
		struct dump d = {.uptime_ms = -1};
		const struct android_fix *fx;
		void *ctx;
		FILE *f = host_open(cmds[i], &ctx);

		if (f == NULL) {
			continue;
		}
		parse_dump(f, &d);
		host_close(f, ctx);

		if (usable(&d.gps, d.uptime_ms)) {
			fx = &d.gps;
		} else if (usable(&d.fused, d.uptime_ms)) {
			fx = &d.fused;
		} else if (usable(&d.network, d.uptime_ms)) {
			fx = &d.network;
		} else {
			continue;
		}

		if (last_source != i) {
			last_source = i;
			nsi_print_trace("gnss_native_sim: reading the location %s\n", names[i]);
		}
		*out = (struct gnss_ns_fix){
			.latitude = to_nano(fx->lat),
			.longitude = to_nano(fx->lon),
			.altitude = (int32_t)(fx->alt * 1000.0),
			.speed = to_milli(fx->speed),
			.bearing = to_milli(fx->bearing) % 360000,
			.accuracy = to_milli(fx->hacc),
			.kind = (fx == &d.gps) ? GNSS_NS_FIX_GNSS : GNSS_NS_FIX_ESTIMATED,
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

		while (!atomic_load(&running)) {
			if (atomic_load(&exiting)) {
				return NULL;
			}
			while (sem_wait(&kick) != 0) {
				if (errno != EINTR) {
					nsi_print_warning("gnss_native_sim: sem_wait() failed: %s\n",
							  strerror(errno));
					return NULL;
				}
			}
		}
		if (atomic_load(&exiting)) {
			return NULL;
		}

		ok = read_android(&fx);

		pthread_mutex_lock(&lock);
		if (atomic_load(&running)) {
			latest = fx;
			latest_at_ms = monotonic_ms();
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
		sleep_ms(poll_ms);
	}
	return NULL;
}

static void stamp_utc(struct gnss_ns_fix *fix)
{
	struct timespec ts;
	struct tm tm;

	clock_gettime(CLOCK_REALTIME, &ts);
	gmtime_r(&ts.tv_sec, &tm);
	fix->utc_hour = tm.tm_hour;
	fix->utc_minute = tm.tm_min;
	fix->utc_millisecond = tm.tm_sec * 1000 + ts.tv_nsec / 1000000;
	fix->utc_month_day = tm.tm_mday;
	fix->utc_month = tm.tm_mon + 1;
	fix->utc_century_year = tm.tm_year % 100;
}

int gnss_ns_bottom_init(const char *fixed_position, unsigned int poll, unsigned int max_age)
{
	double lat, lon, alt = 0.0;
	int len = -1;
	int n;

	poll_ms = poll;
	max_age_ms = max_age;

	if (fixed_position == NULL) {
		return 0;
	}

	/* len ends up past the last number read, so trailing junk is caught */
	n = sscanf(fixed_position, "%lf,%lf%n,%lf%n", &lat, &lon, &len, &alt, &len);
	if ((n != 2 && n != 3) || len < 0 || fixed_position[len] != '\0' || lat < -90.0 ||
	    lat > 90.0 || lon < -180.0 || lon > 180.0) {
		return -1;
	}

	fixed_source = true;
	fixed_fix = (struct gnss_ns_fix){
		.latitude = to_nano(lat),
		.longitude = to_nano(lon),
		.altitude = (int32_t)(alt * 1000.0),
		.accuracy = 1000,
		.kind = GNSS_NS_FIX_GNSS,
	};
	return 0;
}

void gnss_ns_bottom_start(void)
{
	if (fixed_source) {
		return;
	}

	if (!poller_started) {
		poller_started = true;
		if (sem_init(&kick, 0, 0) != 0 ||
		    pthread_create(&poller_thread, NULL, poller, NULL) != 0) {
			nsi_print_error_and_exit("gnss_native_sim: cannot start the poll thread\n");
		}
	}

	atomic_store(&running, true);
	sem_post(&kick);
}

void gnss_ns_bottom_stop(void)
{
	atomic_store(&running, false);
	pthread_mutex_lock(&lock);
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
		/* A source that stopped answering must not keep its last fix alive */
		if (monotonic_ms() - latest_at_ms > (int64_t)LOST_AFTER_POLLS * poll_ms) {
			fix->kind = GNSS_NS_FIX_NONE;
		}
		pthread_mutex_unlock(&lock);
	}
	stamp_utc(fix);
}
