#define TF_FPU (0)
#define TF_REGS (TF_FPU + 3072)
#define TF_EAX (TF_REGS + 4 * 0)
#define TF_EBX (TF_REGS + 4 * 1)
#define TF_ECX (TF_REGS + 4 * 2)
#define TF_EDX (TF_REGS + 4 * 3)
#define TF_ESI (TF_REGS + 4 * 4)
#define TF_EDI (TF_REGS + 4 * 5)
#define TF_ESP (TF_REGS + 4 * 6)
#define TF_EBP (TF_REGS + 4 * 7)
#define TF_EIP (TF_REGS + 4 * 8)
#define TF_CS  (TF_REGS + 4 * 9)
#define TF_DS  (TF_REGS + 4 * 10)
#define TF_ES  (TF_REGS + 4 * 11)
#define TF_FS  (TF_REGS + 4 * 12)
#define TF_GS  (TF_REGS + 4 * 13)
#define TF_SS  (TF_REGS + 4 * 14)
#define TF_EF  (TF_REGS + 4 * 15)

.global arch_waitq_sleep
.type arch_waitq_sleep, %function
arch_waitq_sleep:
	pop %ecx
	mov %gs:4, %eax
	mov %ebx, TF_EBX(%eax)
	mov %esi, TF_ESI(%eax)
	mov %edi, TF_EDI(%eax)
	mov %esp, TF_ESP(%eax)
	mov %ebp, TF_EBP(%eax)
	mov %ecx, TF_EIP(%eax)
	pushf
	pop %ebx
	mov %ebx, TF_EF(%eax)
	mov $0x08, %ebx
	mov %ebx, TF_CS(%eax)
	mov $0x10, %ebx
	mov %ebx, TF_DS(%eax)
	mov %ebx, TF_ES(%eax)
	mov %ebx, TF_SS(%eax)
	mov $0x28, %ebx
	mov %ebx, TF_FS(%eax)
	mov $0x30, %ebx
	mov %ebx, TF_GS(%eax)
	jmp waitq_sleep_return
