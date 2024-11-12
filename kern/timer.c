#include <spinlock.h>
#include <timer.h>
#include <std.h>

static struct spinlock timers_lock = SPINLOCK_INITIALIZER();
static TAILQ_HEAD(, timer) timers = TAILQ_HEAD_INITIALIZER(timers);

void timer_check_timeout(void)
{
	struct timespec cur;
	clock_gettime(CLOCK_MONOTONIC, &cur);
	spinlock_lock(&timers_lock);
	while (1)
	{
		struct timer *timer = TAILQ_FIRST(&timers);
		if (!timer)
			break;
		if (timespec_cmp(&cur, &timer->timeout) < 0)
			break;
		TAILQ_REMOVE(&timers, timer, chain);
		spinlock_unlock(&timers_lock);
		timer->cb(timer);
		spinlock_lock(&timers_lock);
	}
	spinlock_unlock(&timers_lock);
}

void timer_add(struct timer *timer, struct timespec timeout, timer_cb_t cb,
               void *userdata)
{
	timer->timeout = timeout;
	timer->cb = cb;
	timer->userdata = userdata;
	struct timer *it;
	spinlock_lock(&timers_lock);
	TAILQ_FOREACH(it, &timers, chain)
	{
		if (timespec_cmp(&timer->timeout, &it->timeout) < 0)
		{
			TAILQ_INSERT_BEFORE(it, timer, chain);
			spinlock_unlock(&timers_lock);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&timers, timer, chain);
	spinlock_unlock(&timers_lock);
}

void timer_remove(struct timer *timer)
{
	spinlock_lock(&timers_lock);
	TAILQ_REMOVE(&timers, timer, chain);
	spinlock_unlock(&timers_lock);
}
