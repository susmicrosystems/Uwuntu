#ifndef SCHED_H
#define SCHED_H

struct thread;

void sched_init(void);
void sched_run(struct thread *thread);
void sched_test(void);
void sched_tick(void);
void sched_ipi(void);
void sched_resched(void);
void sched_yield(void);
void sched_switch(struct thread *thread);
void sched_enqueue(struct thread *thread);
void sched_dequeue(struct thread *thread);

#endif
