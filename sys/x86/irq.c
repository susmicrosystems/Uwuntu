#include "arch/x86/apic.h"
#include "arch/x86/x86.h"
#include "arch/x86/msr.h"
#include "arch/x86/cr.h"

#include <sched.h>
#include <proc.h>
#include <pci.h>
#include <std.h>
#include <cpu.h>
#include <mem.h>

#ifdef __i386__
# define REG_FMT "0x%08" PRIx32
# define IP(ctx) ((ctx)->trapframe->regs.eip)
#else
# define REG_FMT "0x%016" PRIx64
# define IP(ctx) ((ctx)->trapframe->regs.rip)
#endif

#define CS_DPL(cs) ((cs) & 0x3)
#define DPL(ctx) CS_DPL((ctx)->trapframe->regs.cs)

#define PANIC_ON_USER_PF_R 0
#define PANIC_ON_USER_PF_W 0
#define PANIC_ON_USER_GPF  0

static void isr_eoi(uint8_t id);

void handle_divide_by_zero(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("divide by zero @ " REG_FMT "\n", IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
#if 0
			arch_print_regs(&ctx->trapframe->regs);
			panic("divide by zero: " REG_FMT
			      " @ " REG_FMT "\n",
			      ctx->err, IP(ctx));
#endif
			thread_signal(thread, SIGFPE);
			break;
		}
	}
}

void handle_debug(const struct irq_ctx *ctx)
{
	uintptr_t dr6 = getdr6();
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("debug @ " REG_FMT " (" REG_FMT ")\n", IP(ctx), dr6);
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			if (dr6 & (1 << 14))
			{
				if (thread->ptrace_state != PTRACE_ST_ONESTEP)
					panic("debug single step without onestep ptrace\n");
				thread_ptrace_stop(thread, SIGTRAP);
				break;
			}
			arch_print_regs(&ctx->trapframe->regs);
			panic("debug @ " REG_FMT " (" REG_FMT ")\n", IP(ctx), dr6);
			break;
		}
	}
}

void handle_nmi(const struct irq_ctx *ctx)
{
	uint8_t scpa = inb(0x92);
	uint8_t scpb = inb(0x61);
	arch_print_regs(&ctx->trapframe->regs);
	panic("non maskable interrupt @ " REG_FMT " (%02" PRIx8 ", %02" PRIx8 ")\n",
	      IP(ctx), scpa, scpb);
}

void handle_breakpoint(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("breakpoint @ " REG_FMT "\n", IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_signal(thread, SIGTRAP);
			break;
		}
	}
}

void handle_overflow(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("overflow @ " REG_FMT "\n", IP(ctx));
}

void handle_bound_range_exceeded(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("bound range exceeded @ " REG_FMT "\n", IP(ctx));
}

void handle_invalid_opcode(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("invalid opcode @ " REG_FMT "\n", IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_illegal_instruction(thread);
			break;
		}
	}
}

void handle_device_not_available(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("device not available @ " REG_FMT "\n", IP(ctx));
}

void handle_double_fault(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("double fault: " REG_FMT " @ " REG_FMT "\n", ctx->err, IP(ctx));
}

void handle_coprocessor_segment_overrun(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("coprocessor segment overrun @ " REG_FMT "\n", IP(ctx));
}

void handle_invalid_tss(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("invalid tss: " REG_FMT " @ " REG_FMT "\n", ctx->err, IP(ctx));
}

void handle_segment_not_present(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("segment not present: " REG_FMT " @ " REG_FMT "\n",
			      ctx->err, IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_signal(thread, SIGSEGV);
			break;
		}
	}
}

void handle_stack_segment_fault(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("stack segment fault: " REG_FMT " @ " REG_FMT "\n",
			      ctx->err, IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_signal(thread, SIGSEGV);
			break;
		}
	}
}

void handle_general_protection_fault(const struct irq_ctx *ctx)
{
#if PANIC_ON_USER_GPF
	uintptr_t page_addr = getcr2();
#endif
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("general protection fault: " REG_FMT " @ " REG_FMT "\n",
			      ctx->err, IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
#if PANIC_ON_USER_GPF
			arch_print_regs(&ctx->trapframe->regs);
			arch_print_user_stack_trace(thread);
			panic("general protection fault addr " REG_FMT ": " REG_FMT
			      " @ " REG_FMT "\n",
			      page_addr, ctx->err, IP(ctx));
#endif
			thread_signal(thread, SIGSEGV);
			break;
		}
	}
}

void handle_page_fault(const struct irq_ctx *ctx)
{
	uintptr_t page_addr = getcr2();
	if (0)
	{
		arch_print_regs(&ctx->trapframe->regs);
		printf("page fault addr " REG_FMT ": " REG_FMT  " @ " REG_FMT "\n",
		       page_addr, ctx->err, IP(ctx));
	}
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("page fault addr " REG_FMT ": " REG_FMT " @ " REG_FMT "\n",
			      page_addr, ctx->err, IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_fault(thread, page_addr, (ctx->err & 2) ? VM_PROT_W : VM_PROT_R);
			break;
		}
	}
}

void handle_x87_fpe(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("x87 fpe @ " REG_FMT "\n", IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_signal(thread, SIGFPE);
			break;
		}
	}
}

void handle_alignment_check(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("alignment check @ " REG_FMT "\n", IP(ctx));
}

void handle_machine_check(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("machine check @ " REG_FMT "\n", IP(ctx));
}

void handle_simd_fpe(const struct irq_ctx *ctx)
{
	switch (DPL(ctx))
	{
		case 0:
			arch_print_regs(&ctx->trapframe->regs);
			panic("simd fpe @ " REG_FMT "\n", IP(ctx));
			break;
		case 1:
			panic("invalid DPL 1\n");
			break;
		case 2:
			panic("invalid DPL 2\n");
			break;
		case 3:
		{
			struct thread *thread = curcpu()->thread;
			if (!thread)
				panic("DPL 3 without thread\n");
			thread_signal(thread, SIGFPE);
			break;
		}
	}
}

void handle_virtualization_exception(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("virtualization exception @ " REG_FMT "\n", IP(ctx));
}

void handle_control_protection_exception(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("control protection exception @ " REG_FMT "\n", IP(ctx));
}

void handle_hypervisor_injection_exception(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("hypervisor injection exception @ " REG_FMT "\n", IP(ctx));
}

void handle_vmm_communication_exception(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("vmm communication exception @ " REG_FMT "\n", IP(ctx));
}

void handle_security_exception(const struct irq_ctx *ctx)
{
	arch_print_regs(&ctx->trapframe->regs);
	panic("security exception @ " REG_FMT "\n", IP(ctx));
}

void handle_syscall(const struct irq_ctx *ctx)
{
	curcpu()->thread->from_syscall = 1;
	proc_update_loadavg();
#ifdef __i386__
	syscall(ctx->trapframe->regs.eax,
	        ctx->trapframe->regs.ebx,
	        ctx->trapframe->regs.ecx,
	        ctx->trapframe->regs.edx,
	        ctx->trapframe->regs.esi,
	        ctx->trapframe->regs.edi,
	        ctx->trapframe->regs.ebp);
#else
	syscall(ctx->trapframe->regs.rax,
	        ctx->trapframe->regs.rdi,
	        ctx->trapframe->regs.rsi,
	        ctx->trapframe->regs.rdx,
	        ctx->trapframe->regs.r10,
	        ctx->trapframe->regs.r8,
	        ctx->trapframe->regs.r9);
#endif
	cpu_tick();
}

void handle_ipi(const struct irq_ctx *ctx)
{
	(void)ctx;
	if (__atomic_exchange_n(&curcpu()->must_resched, 0, __ATOMIC_SEQ_CST))
		sched_resched();
}

void handle_spurious(const struct irq_ctx *ctx)
{
	(void)ctx;
	panic("spurious interrupt\n");
}

static void handle_irq(uint8_t id)
{
	isr_eoi(id);
	irq_execute(id);
}

void arch_trap_handle(size_t id, struct irq_ctx *ctx)
{
	switch (id)
	{
		case 0x00:
			handle_divide_by_zero(ctx);
			break;
		case 0x01:
			handle_debug(ctx);
			break;
		case 0x02:
			handle_nmi(ctx);
			break;
		case 0x03:
			handle_breakpoint(ctx);
			break;
		case 0x04:
			handle_overflow(ctx);
			break;
		case 0x05:
			handle_bound_range_exceeded(ctx);
			break;
		case 0x06:
			handle_invalid_opcode(ctx);
			break;
		case 0x07:
			handle_device_not_available(ctx);
			break;
		case 0x08:
			handle_double_fault(ctx);
			break;
		case 0x09:
			handle_coprocessor_segment_overrun(ctx);
			break;
		case 0x0A:
			handle_invalid_tss(ctx);
			break;
		case 0x0B:
			handle_segment_not_present(ctx);
			break;
		case 0x0C:
			handle_stack_segment_fault(ctx);
			break;
		case 0x0D:
			handle_general_protection_fault(ctx);
			break;
		case 0x0E:
			handle_page_fault(ctx);
			break;
		case 0x0F:
			panic("invalid trap 0x0F\n");
			break;
		case 0x10:
			handle_x87_fpe(ctx);
			break;
		case 0x11:
			handle_alignment_check(ctx);
			break;
		case 0x12:
			handle_machine_check(ctx);
			break;
		case 0x13:
			handle_simd_fpe(ctx);
			break;
		case 0x14:
			handle_virtualization_exception(ctx);
			break;
		case 0x15:
			handle_control_protection_exception(ctx);
			break;
		case 0x16:
			panic("invalid trap 0x16\n");
			break;
		case 0x17:
			panic("invalid trap 0x17\n");
			break;
		case 0x18:
			panic("invalid trap 0x18\n");
			break;
		case 0x19:
			panic("invalid trap 0x19\n");
			break;
		case 0x1A:
			panic("invalid trap 0x1A\n");
			break;
		case 0x1B:
			panic("invalid trap 0x1B\n");
			break;
		case 0x1C:
			handle_hypervisor_injection_exception(ctx);
			break;
		case 0x1D:
			handle_vmm_communication_exception(ctx);
			break;
		case 0x1E:
			handle_security_exception(ctx);
			break;
		case 0x1F:
			panic("invalid trap 0x1F\n");
			break;
		case IRQ_ID_SYSCALL:
			handle_syscall(ctx);
			break;
		case IRQ_ID_IPI:
			isr_eoi(id);
			handle_ipi(ctx);
			break;
		case IRQ_ID_SPURIOUS:
			handle_spurious(ctx);
			break;
		default:
			handle_irq(id);
			break;
	}
}

static void isr_eoi(uint8_t id)
{
	if (!g_has_apic)
	{
		pic_eoi(id);
		return;
	}
	if (curcpu()->arch.cpuid.kvm_feat & CPUID_KVM_FEAT_PV_EOI)
	{
		uint32_t expected = 1;
		if (__atomic_compare_exchange_n(&curcpu()->arch.kvm_eoi,
		                                &expected, 0, 0,
		                                __ATOMIC_RELAXED,
		                                __ATOMIC_RELAXED))
			return;
	}
	lapic_eoi();
}

static int find_free_irq(size_t *cpuid, uint8_t *irq)
{
	/* XXX test if irq is not part of ISA or PCI IOAPIC redirection */
	struct cpu *cpu;
	CPU_FOREACH(cpu)
	{
		for (uint8_t i = 32; i < 255; ++i)
		{
			if (i == IRQ_ID_SYSCALL
			 || i == IRQ_ID_IPI
			 || i == IRQ_ID_SPURIOUS)
				continue;
			if (!TAILQ_EMPTY(&cpu->irq_handles[i]))
				continue;
			*cpuid = cpu->id;
			*irq = i;
			return 0;
		}
	}
	return -ENOENT;
}

int register_isa_irq(enum isa_irq_id id, irq_fn_t fn, void *userdata,
                     struct irq_handle *handle)
{
	if (!g_has_apic)
	{
		uint8_t irq = g_isa_irq[id];
		register_irq(handle, IRQ_NATIVE, 32 + irq, 0, fn, userdata);
		handle->native.line = irq;
		pic_enable_irq(irq);
		return 0;
	}
	uint8_t ioapic;
	uint8_t line;
	int active_low;
	int level_trigger;
	int ret = mptable_get_isa_irq(id, &ioapic, &line, &active_low,
	                              &level_trigger);
	if (ret)
	{
		active_low = 0;
		level_trigger = 0;
		ioapic = ioapic_default_id(); /* XXX best effort */
		line = g_isa_irq[id];
	}
	register_irq(handle, IRQ_NATIVE, 32 + line, 0, fn, userdata);
	handle->native.ioapic = ioapic;
	handle->native.line = line;
	ioapic_enable_irq(ioapic, line, active_low, level_trigger);
#if 0
	printf("enable IOAPIC IRQ at 0x%" PRIx8 " for ISA %d\n",
	       32 + line, id);
#endif
	return 0;
}

int register_pci_irq(struct pci_device *device, irq_fn_t fn, void *userdata,
                     struct irq_handle *handle)
{
	if (!g_has_apic)
	{
		register_irq(handle, IRQ_NATIVE, 32 + device->header0.int_line, 0, fn, userdata);
		handle->native.line = device->header0.int_line;
		pic_enable_irq(device->header0.int_line);
		return 0;
	}
	size_t cpuid;
	uint8_t irq;
	int ret = find_free_irq(&cpuid, &irq);
	if (ret)
		return ret;
	uint16_t msix_vector;
	if (!pci_enable_msix(device, 0xFEE00000 | (cpuid << 12), irq,
	                     &msix_vector))
	{
#if 0
		printf("enable MSI-X %" PRIu16 " IRQ at CPU %lu IRQ 0x%" PRIx8 " for PCI "
		       "%02" PRIx8 ":%02" PRIx8 ".%" PRIx8 "\n",
		       msix_vector, cpuid, irq, device->bus, device->slot, device->func);
#endif
		register_irq(handle, IRQ_MSIX, irq, cpuid, fn, userdata);
		handle->msix.device = device;
		handle->msix.vector = msix_vector;
		return 0;
	}
	if (!pci_enable_msi(device, 0xFEE00000 | (cpuid << 12), irq))
	{
#if 0
		printf("enable MSI IRQ at CPU %lu IRQ 0x%" PRIx8 " for PCI "
		       "%02" PRIx8 ":%02" PRIx8 ".%" PRIx8 "\n",
		       cpuid, irq, device->bus, device->slot, device->func);
#endif
		register_irq(handle, IRQ_MSI, irq, cpuid, fn, userdata);
		handle->msi.device = device;
		return 0;
	}
	uint8_t ioapic;
	uint8_t line;
	int active_low;
	int level_trigger;
	ret = mptable_get_pci_irq(device, &ioapic, &line,
	                          &active_low, &level_trigger);
	if (ret)
		return ret;
	register_irq(handle, IRQ_NATIVE, 32 + line, 0, fn, userdata);
	handle->native.ioapic = ioapic;
	handle->native.line = line;
	ioapic_enable_irq(ioapic, line, active_low, level_trigger);
#if 0
	printf("enable IOAPIC IRQ at 0x%" PRIx8 " for PCI "
	       "%02" PRIx8 ":%02" PRIx8 ".%" PRIx8 "\n",
	       32 + line, device->bus, device->slot, device->func);
#endif
	return 0;
}

void arch_disable_native_irq(struct irq_handle *handle)
{
	if (g_has_apic)
		ioapic_disable_irq(handle->native.ioapic,
		                   handle->native.line);
	else
		pic_disable_irq(handle->native.line);
}
