#include "fs/procfs/procfs.h"
#include "fs/devfs/devfs.h"
#include "fs/sysfs/sysfs.h"

#include <net/if.h>

#include <multiboot.h>
#include <random.h>
#include <evdev.h>
#include <sched.h>
#include <timer.h>
#include <proc.h>
#include <ksym.h>
#include <kmod.h>
#if WITH_ACPI
#include <acpi.h>
#endif
#include <sma.h>
#include <vfs.h>
#include <ipc.h>
#include <irq.h>
#include <tty.h>
#include <pci.h>
#include <efi.h>
#if WITH_FDT
#include <fdt.h>
#endif
#include <mem.h>

void sock_init(void);
void file_init(void);
void vm_zone_init(void);
void bcdev_init(void);
void sysv_ipc_init(void);
void pty_init_sma(void);
void tty_init(void);
void proc_init(void);
void pipe_init(void);
void net_init(void);
void pci_init_sma(void);
void sock_udp_init(void);
void sock_pfl_stream_init(void);
void sock_pfl_dgram_init(void);
void arp_entry_init(void);
void netpkt_init(void);
void pfls_init(void);
void sock_tcp_init(void);
void sock_raw_init(void);
void vfs_init_sma(void);
void kmod_init(void);
#if WITH_ACPI
void aml_init(void);
#endif
void snd_init(void);
void evdev_init_sma(void);
void vtty_init(void);
void vm_region_init_sma(void);

struct cpu g_cpus[MAXCPU];
size_t g_ncpus;

static struct spinlock smp_spinlock;
static size_t smp_running;
uint8_t *smp_stacks[MAXCPU];

static size_t cpu_sync_count;
static cpumask_t cpu_sync_mask;
static struct spinlock g_kernel_lock = SPINLOCK_INITIALIZER();
static size_t g_panic;

void cpu_sync(cpumask_t *cpumask)
{
	/* XXX
	 * it's not *that* bad, but it's clearly not great either
	 */
	if (g_ncpus == 1)
		return;
	__atomic_store_n(&cpu_sync_count, 1, __ATOMIC_SEQ_CST);
	CPUMASK_CLEAR(&cpu_sync_mask);
	size_t curcpu_id = curcpu()->id;
	struct cpu *cpu;
	CPU_FOREACH(cpu)
	{
		if (cpu->id == curcpu_id)
			continue;
		if (!CPUMASK_GET(cpumask, cpu->id))
		{
			__atomic_add_fetch(&cpu_sync_count, 1, __ATOMIC_SEQ_CST);
			continue;
		}
		CPUMASK_SET(&cpu_sync_mask, cpu->id, 1);
		arch_cpu_ipi(cpu);
	}
	spinlock_unlock(&g_kernel_lock);
	while (__atomic_load_n(&cpu_sync_count, __ATOMIC_SEQ_CST) < g_ncpus)
		arch_spin_yield();
	kernel_lock();
	assert(__atomic_load_n(&cpu_sync_count, __ATOMIC_SEQ_CST) == g_ncpus,
	       "bogus cpu sync cpus count\n");
	__atomic_store_n(&cpu_sync_count, 0, __ATOMIC_SEQ_CST);
}

void cpu_sync_leave(void)
{
	struct cpu *cpu = curcpu();
	if (__atomic_load_n(&cpu_sync_count, __ATOMIC_SEQ_CST)
	 && CPUMASK_GET(&cpu_sync_mask, cpu->id))
	{
		CPUMASK_SET(&cpu_sync_mask, cpu->id, 0);
		__atomic_fetch_add(&cpu_sync_count, 1, __ATOMIC_SEQ_CST);
		kernel_unlock();
		while (__atomic_load_n(&cpu_sync_count, __ATOMIC_SEQ_CST))
			arch_spin_yield();
	}
	else
	{
		kernel_unlock();
	}
}

void cpu_test_panic(void)
{
	if (!__atomic_load_n(&g_panic, __ATOMIC_SEQ_CST))
		return;
	__atomic_fetch_add(&g_panic, 1, __ATOMIC_SEQ_CST);
	dead_loop();
}

void _panic(const char *file, size_t line, const char *fn, const char *fmt, ...)
{
	va_list va_arg;

	arch_disable_interrupts();
	__atomic_add_fetch(&g_panic, 1, __ATOMIC_SEQ_CST);
	cpu_ipi_other(curcpu());
	while (__atomic_load_n(&g_panic, __ATOMIC_SEQ_CST) < g_ncpus);
	va_start(va_arg, fmt);
	vprintf(fmt, va_arg);
	printf("%s@%s:%zu\n", fn, file, line);
	arch_print_stack_trace();
	dead_loop();
}

void kernel_lock(void)
{
	while (!spinlock_trylock(&g_kernel_lock))
	{
		cpu_test_panic();
		arch_spin_yield();
	}
	/* XXX hack: always reload vmem to avoid memory problems
	 * tlb shootdown should be implemented instead
	 * it costs around 300ns (relative to a 400ns getpid() syscall without it)
	 */
	struct thread *thread = curcpu()->thread;
	arch_vm_setspace(thread ? thread->proc->vm_space : NULL);
}

void kernel_unlock(void)
{
	spinlock_unlock(&g_kernel_lock);
}

void cpu_ipi_other(struct cpu *from)
{
	struct cpu *cpu;
	CPU_FOREACH(cpu)
	{
		if (cpu->id == from->id)
			continue;
		arch_cpu_ipi(cpu);
	}
}

static void register_sysfs_nodes(void)
{
	arch_register_sysfs();
	vm_register_sysfs();
	mounts_register_sysfs();
	multiboot_register_sysfs();
	procinfo_register_sysfs();
	loadavg_register_sysfs();
	uptime_register_sysfs();
	cpustat_register_sysfs();
	sma_register_sysfs();
	irq_register_sysfs();
}

static void init_sma(void)
{
	sock_init();
	file_init();
	vm_zone_init();
	vm_region_init_sma();
	bcdev_init();
	sysv_ipc_init();
	pty_init_sma();
	tty_init();
	proc_init();
	pipe_init();
	net_init();
	pci_init_sma();
	sock_udp_init();
	sock_pfl_stream_init();
	sock_pfl_dgram_init();
	arp_entry_init();
	netpkt_init();
	pfls_init();
	sock_tcp_init();
	sock_raw_init();
	vfs_init_sma();
	kmod_init();
#if WITH_ACPI
	aml_init();
#endif
	snd_init();
	evdev_init_sma();
	vtty_init();
}

static ssize_t random_boottime_collect(void *buf, size_t size, void *userdata)
{
	(void)userdata;
	struct timespec ts;
	int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret)
		return ret;
	if (size < 8)
		return 0;
	*(int64_t*)buf = ts.tv_nsec;
	return 8;
}

static void first_cpu_init(void)
{
	arch_paging_init();
	alloc_init();
	init_sma();
	ksym_init();
	if (devfs_init())
		panic("failed to initialize devfs\n");
	if (sysfs_init())
		panic("failed to initialize sysfs\n");
	if (procfs_init())
		panic("failed to initialize procfs\n");
	register_sysfs_nodes();
	pty_init();
#if WITH_ACPI
	if (acpi_init())
		panic("failed to initialize ACPI\n");
#endif
	efi_init();
#if WITH_FDT
	if (fdt_init())
		panic("failed to initialize FDT\n");
#endif
	vfs_init();
	cdev_init();
	arch_device_init();
	evdev_init();
	ipc_init();
	net_loopback_init();
	clock_gettime(CLOCK_MONOTONIC, &g_boottime);
	random_register(random_boottime_collect, NULL);
	random_init();
	pci_init();
}

__attribute__ ((noreturn))
void cpu_boot(void)
{
	uint32_t cpuid = __atomic_fetch_add(&g_ncpus, 1, __ATOMIC_SEQ_CST);
	if (cpuid)
		spinlock_lock(&smp_spinlock);
	struct cpu *cpu = &g_cpus[cpuid];
	cpu->self = cpu;
	cpu->id = cpuid;
	for (size_t i = 0; i < sizeof(cpu->irq_handles) / sizeof(*cpu->irq_handles); ++i)
		TAILQ_INIT(&cpu->irq_handles[i]);
	arch_cpu_boot(cpu);
	if (!cpu->id)
		first_cpu_init();
	if (clock_gettime(CLOCK_MONOTONIC, &cpu->loadavg_time))
		panic("failed to get monotonic clock\n");
	arch_init_copy_zone(&cpu->copy_src_page);
	arch_init_copy_zone(&cpu->copy_dst_page);
	if (!cpu->id)
	{
		sched_init();
		struct thread *initthread;
		if (setup_initthread(&initthread))
			panic("failed to create init thread\n");
		sched_enqueue(initthread);
	}
	if (setup_idlethread())
		panic("failed to create idle thread\n");
	cpu->thread = cpu->idlethread;
	cpu->thread->running_cpuid = cpu->id;
	thread_ref(cpu->thread);
	cpu->trapframe = &cpu->thread->tf_user;
	if (!cpu->id)
	{
		kernel_lock();
		arch_start_smp();
		sched_ipi();
		kernel_unlock();
	}
	else
	{
		__atomic_add_fetch(&smp_running, 1, __ATOMIC_SEQ_CST);
		spinlock_unlock(&smp_spinlock);
	}
	idle_loop();
	panic("post idle loop\n");
	while (1);
}

void cpu_start_smp(size_t ncpu)
{
	spinlock_init(&smp_spinlock);
	spinlock_lock(&smp_spinlock);
	__atomic_store_n(&smp_running, 0, __ATOMIC_SEQ_CST);
	for (size_t i = 0; i < ncpu; ++i)
	{
		struct cpu *cpu = &g_cpus[g_ncpus];
		uint8_t **stackp = &cpu->stack;
		size_t *stack_size = &cpu->stack_size;
		*stack_size = KSTACK_SIZE;
		void *stack = vmalloc(*stack_size);
		if (!stack)
			panic("smp stack allocation failed\n");
		*stackp = (uint8_t*)stack + *stack_size;
		smp_stacks[g_ncpus] = *stackp;
		size_t running_ncpus = __atomic_load_n(&g_ncpus,
		                                       __ATOMIC_SEQ_CST);
		int ret = arch_start_smp_cpu(cpu, i);
		if (ret)
		{
			vfree(stack, *stack_size);
			continue;
		}
		spinlock_unlock(&smp_spinlock);
		while (__atomic_load_n(&smp_running, __ATOMIC_SEQ_CST) != running_ncpus);
		spinlock_lock(&smp_spinlock);
	}
	spinlock_unlock(&smp_spinlock);
	spinlock_destroy(&smp_spinlock);
}

void cpu_tick(void)
{
	struct cpu *cpu = curcpu();
	waitq_check_timeout(); /* XXX move somewhere else */
	timer_check_timeout(); /* XXX move somewhere else */
	if (!cpu->id && cpu->thread && cpu->thread->tf_nest_level < 2)
		sched_tick();
}

void print_stack_trace_entry(size_t id, uintptr_t pc)
{
	printf("[%3zu] 0x%0*zx", id, (int)sizeof(pc) * 2, pc);
	uintptr_t off;
	const char *sym = ksym_find_by_addr(g_kern_ksym_ctx, pc, &off);
	if (sym)
	{
		printf(" [%s+0x%zx]", sym, off);
	}
	else
	{
		struct kmod *kmod = kmod_find_by_addr(pc);
		if (kmod)
		{
			sym = kmod_get_sym(kmod, pc, &off);
			if (sym)
				printf(" [%s:%s+0x%zx]", kmod->info->name,
				       sym, off);
			else
				printf(" [%s:+0x%zx]", kmod->info->name,
				       pc - kmod->elf_info.base_addr);
		}
	}
	printf("\n");
}
