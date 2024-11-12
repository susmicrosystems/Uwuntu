#include "arch/aarch64/gicv2.h"
#include "arch/aarch64/psci.h"

#include <arch/asm.h>
#include <arch/csr.h>

#include <endian.h>
#include <proc.h>
#include <cpu.h>
#include <fdt.h>
#include <mem.h>

__attribute__((noreturn))
void context_switch(const struct trapframe *trapframe, uintptr_t trap_sp);
int pl031_init_fdt(struct fdt_node *node);
int pl011_init_fdt(struct fdt_node *node);
void timer_init(void);
void setup_interrupt_handlers(void);
void setup_trap_stack(void *sp);

extern uintptr_t trap_vector;
uint32_t kern_l1t_page;

static inline void early_printf_uart(const char *s, size_t n)
{
	for (size_t i = 0; i < n; ++i)
	{
		if (s[i] == '\n')
			*(char*)0x09000000 = '\r';
		*(char*)0x09000000 = s[i];
	}
}

static void probe_gic_node(struct fdt_node *node)
{
	if (!fdt_check_compatible(node, "arm,cortex-a15-gic"))
		gicv2_init_fdt(node);
}

static void probe_node(struct fdt_node *node)
{
	if (!strcmp(node->name, "cpus"))
	{
		struct fdt_node *cpu;
		TAILQ_FOREACH(cpu, &node->children, chain)
		{
			if (strncmp(cpu->name, "cpu@", 4))
				continue;
			int id = atoi(&cpu->name[4]);
			if (!id) /* assume cpu 0 is the boot one (and handle atoi error by the way.. ) */
				continue;
			struct fdt_prop *enable_method = fdt_get_prop(cpu, "enable-method");
			if (!enable_method
			 || !enable_method->len
			 || strcmp((char*)enable_method->data, "psci"))
				continue;
			struct fdt_prop *reg = fdt_get_prop(cpu, "reg");
			if (!reg
			 || reg->len != 4)
				continue;
			psci_add_cpu(be32toh(*(uint32_t*)reg->data), id);
		}
	}
	if (!fdt_check_compatible(node, "arm,pl011"))
		pl011_init_fdt(node);
	if (!fdt_check_compatible(node, "arm,pl031"))
		pl031_init_fdt(node);
	if (!fdt_check_compatible(node, "arm,psci-0.2")
	 || !fdt_check_compatible(node, "arm,psci-1.0")
	 || !fdt_check_compatible(node, "arm,psci"))
		psci_init_fdt(node);
}

static void arm_device_probe(void)
{
	struct fdt_node *node;
	TAILQ_FOREACH(node, &fdt_nodes, chain)
	{
		struct fdt_node *child;
		/* GIC must be enabled first to allow other device to register interrupts */
		TAILQ_FOREACH(child, &node->children, chain)
			probe_gic_node(child);
		TAILQ_FOREACH(child, &node->children, chain)
			probe_node(child);
	}
}

void arch_cpu_boot(struct cpu *cpu)
{
	if (!cpu->id)
	{
		kern_l1t_page = get_ttbr0();
		g_early_printf = early_printf_uart;
	}
	set_cpacr(get_cpacr() | (3 << (10 * 2))); /* enable cp10 */
	set_cpacr(get_cpacr() | (3 << (11 * 2))); /* enable cp11 */
	set_cpacr(get_cpacr() & ~(1 << 31)); /* disable ASEDIS */
	set_cpacr(get_cpacr() & ~(1 << 30)); /* disable D32DIS */
	set_fpexc((1 << 30)); /* enable vfp */
	set_tpidrprw((uintptr_t)cpu);
	setup_trap_stack(&cpu->arch.tmp_trap_stack[sizeof(cpu->arch.tmp_trap_stack)]);
	set_vbar(&trap_vector);
	if (cpu->id)
	{
		gicv2_enable_gicc();
		setup_interrupt_handlers();
	}
}

void arch_device_init(void)
{
	arm_device_probe();
	gicv2_enable_gicc();
	gicv2_enable_gicd();
	setup_interrupt_handlers();
	timer_init();
}

void arch_print_user_stack_trace(struct thread *thread)
{
	size_t i = 1;
	uintptr_t fp = thread->tf_user.regs.r[11];
	printf("[%3u] FP=0x%08zx PC=0x%08zx\n",
	       0, fp, (uintptr_t)thread->tf_user.regs.r[15]);
	while (fp > 4)
	{
		uintptr_t v[2];
		int ret = vm_copyin(thread->proc->vm_space, v, (void*)(fp - 4), sizeof(v));
		if (ret)
		{
			printf("[%3zu] invalid FP=%08zx\n", i, fp);
			break;
		}
		printf("[%3zu] FP=0x%08zx PC=0x%08zx\n", i, fp, v[1]);
		fp = v[0];
		++i;
	}
}

void arch_print_stack_trace(void)
{
	uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
	size_t i = 0;
	while (fp >= VADDR_KERN_BEGIN)
	{
		print_stack_trace_entry(i, ((uintptr_t*)fp)[0]);
		fp = ((uintptr_t*)fp)[-1];
		++i;
	}
}

void arch_print_regs(const struct user_regs *regs)
{
	printf("R00: %08" PRIx32 " R01: %08" PRIx32 "\n"
	       "R02: %08" PRIx32 " R03: %08" PRIx32 "\n"
	       "R04: %08" PRIx32 " R05: %08" PRIx32 "\n"
	       "R06: %08" PRIx32 " R07: %08" PRIx32 "\n"
	       "R08: %08" PRIx32 " R09: %08" PRIx32 "\n"
	       "R10: %08" PRIx32 " R11: %08" PRIx32 "\n"
	       "R12: %08" PRIx32 " SP : %08" PRIx32 "\n"
	       "LR : %08" PRIx32 " PC : %08" PRIx32 "\n"
	       "CPSR: %08" PRIx32 "\n",
	       regs->r[0], regs->r[1],
	       regs->r[2], regs->r[3],
	       regs->r[4], regs->r[5],
	       regs->r[6], regs->r[7],
	       regs->r[8], regs->r[9],
	       regs->r[10], regs->r[11],
	       regs->r[12], regs->r[13],
	       regs->r[14], regs->r[15],
	       regs->cpsr);
}

static void init_trapframe(struct thread *thread)
{
	memset(thread->tf_user.fpu_data, 0, sizeof(thread->tf_user.fpu_data));
	memset(&thread->tf_user.regs, 0, sizeof(thread->tf_user.regs));
	thread->tf_user.regs.r[15] = (uint32_t)thread->proc->entrypoint;
}

void arch_init_trapframe_kern(struct thread *thread)
{
	init_trapframe(thread);
	thread->tf_user.regs.cpsr = PSR_M_SVC | PSR_I | PSR_F;
}

void arch_init_trapframe_user(struct thread *thread)
{
	init_trapframe(thread);
	thread->tf_user.regs.cpsr = PSR_M_USR;
}

#define ARCH_GET_SET_REG(name, reg) \
void arch_set_##name(struct trapframe *tf, uintptr_t val) \
{ \
	tf->regs.reg = val; \
} \
uintptr_t arch_get_##name(struct trapframe *tf) \
{ \
	return tf->regs.reg; \
}

ARCH_GET_SET_REG(stack_pointer, r[13]);
ARCH_GET_SET_REG(instruction_pointer, r[15]);
ARCH_GET_SET_REG(frame_pointer, r[11]);
ARCH_GET_SET_REG(syscall_retval, r[0]);
ARCH_GET_SET_REG(argument0, r[0]);
ARCH_GET_SET_REG(argument1, r[1]);
ARCH_GET_SET_REG(argument2, r[2]);
ARCH_GET_SET_REG(argument3, r[3]);
ARCH_GET_SET_REG(return_address, r[14]);

#undef ARCH_GET_SET_REG

int arch_validate_user_trapframe(struct trapframe *tf)
{
	if ((tf->regs.cpsr & 0x1F) != PSR_M_USR)
		return -EPERM;
	if (tf->regs.cpsr & (PSR_I | PSR_F)) /* XXX A-bit should be disabled */
		return -EPERM;
	return 0;
}

void arch_cpu_ipi(struct cpu *cpu)
{
	gicv2_sgi(cpu, IRQ_ID_IPI);
}

void arch_disable_interrupts(void)
{
	cpsid_aif();
}

void arch_enable_interrupts(void)
{
	cpsie_aif();
}

void arch_wait_for_interrupt(void)
{
	wfi();
}

void arch_save_fpu(void *dst)
{
	struct user_fpu *fpu = dst;
	__asm__ volatile ("vstm %0, {d0-d15}" : : "r"(&fpu->d[0]));
	__asm__ volatile ("vstm %0, {d16-d31}" : : "r"(&fpu->d[16]));
	__asm__ volatile ("vmrs %0, fpscr" : "=r"(fpu->fpscr));
}

void arch_load_fpu(const void *src)
{
	const struct user_fpu *fpu = src;
	__asm__ volatile ("vldm %0, {d0-d15}" : : "r"(&fpu->d[0]));
	__asm__ volatile ("vldm %0, {d16-d31}" : : "r"(&fpu->d[16]));
	__asm__ volatile ("vmsr fpscr, %0" : : "r"(fpu->fpscr));
}

int arch_register_sysfs(void)
{
	return 0;
}

void arch_set_tls_addr(uintptr_t addr)
{
	set_tpidruro(addr);
}

void arch_set_singlestep(struct thread *thread)
{
	(void)thread;
	/* XXX */
}

void arch_set_trap_stack(void *ptr)
{
	curcpu()->arch.trap_stack = (uintptr_t)ptr;
}

void arch_trap_return(void)
{
	struct cpu *cpu = curcpu();
	context_switch(cpu->trapframe, cpu->arch.trap_stack);
}
