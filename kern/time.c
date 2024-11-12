#include <errno.h>
#include <time.h>
#include <file.h>
#include <std.h>
#include <uio.h>
#include <vfs.h>

struct timespec g_boottime;

static const struct clock_source *clock_monotonic;
static const struct clock_source *clock_realtime;

static const struct clock_source *get_source(int clock)
{
	switch (clock)
	{
		case CLOCK_REALTIME:
			return clock_realtime;
		case CLOCK_MONOTONIC:
			return clock_monotonic;
		default:
			return NULL;
	}
}

static int set_source(int clock, const struct clock_source *source)
{
	switch (clock)
	{
		case CLOCK_REALTIME:
			clock_realtime = source;
			return 0;
		case CLOCK_MONOTONIC:
			clock_monotonic = source;
			return 0;
		default:
			return -EINVAL;
	}
}

int clock_register(int clock, const struct clock_source *source)
{
	const struct clock_source *current = get_source(clock);
	if (!current)
		return set_source(clock, source);
	if (current->precision < source->precision)
		return -EINVAL;
	return set_source(clock, source);
}

int clock_getres(int clock, struct timespec *ts)
{
	const struct clock_source *source = get_source(clock);
	if (!source)
		return -EINVAL;
	return source->getres(ts);
}

int clock_gettime(int clock, struct timespec *ts)
{
	const struct clock_source *source = get_source(clock);
	if (!source)
		return -EINVAL;
	return source->gettime(ts);
}

int clock_settime(int clock, struct timespec *ts)
{
	const struct clock_source *source = get_source(clock);
	if (!source)
		return -EINVAL;
	return source->settime(ts);
}

time_t realtime_seconds(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts))
		return 0;
	return ts.tv_sec;
}

void spinsleep(struct timespec *duration)
{
	struct timespec begin;
	struct timespec end;
	struct timespec diff;
	clock_gettime(CLOCK_MONOTONIC, &begin);
	while (1)
	{
		clock_gettime(CLOCK_MONOTONIC, &end);
		timespec_diff(&diff, &end, &begin);
		if (timespec_cmp(&diff, duration) >= 0)
			return;
	}
}

static ssize_t uptime_read(struct file *file, struct uio *uio)
{
	(void)file;
	size_t count = uio->count;
	off_t off = uio->off;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	struct timespec diff;
	timespec_diff(&diff, &ts, &g_boottime);
	uprintf(uio, "%" PRIu64 ".%09" PRIu64 "\n", diff.tv_sec, diff.tv_nsec);
	uio->off = off + count - uio->count;
	return count - uio->count;
}

static const struct file_op uptime_fop =
{
	.read = uptime_read,
};

int uptime_register_sysfs(void)
{
	return sysfs_mknode("uptime", 0, 0, 0444, &uptime_fop, NULL);
}
