#ifndef TIMER_H
#define TIMER_H

#include <queue.h>
#include <time.h>

struct timer;

typedef void (*timer_cb_t)(struct timer *timer);

struct timer
{
	struct timespec timeout;
	timer_cb_t cb;
	void *userdata;
	TAILQ_ENTRY(timer) chain;
};

void timer_check_timeout(void);
void timer_add(struct timer *timer, struct timespec timeout, timer_cb_t cb,
               void *userdata);
void timer_remove(struct timer *timer);

#endif
