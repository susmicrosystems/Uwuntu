#ifndef SYS_USER_H
#define SYS_USER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__i386__)

struct user_regs_struct
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;
	uint32_t esp;
	uint32_t ebp;
	uint32_t eip;
	uint32_t cs;
	uint32_t ds;
	uint32_t es;
	uint32_t fs;
	uint32_t gs;
	uint32_t ss;
	uint32_t ef;
};

#elif defined(__x86_64__)

struct user_regs_struct
{
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rsp;
	uint64_t rbp;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rip;
	uint64_t cs;
	uint64_t ds;
	uint64_t es;
	uint64_t fs;
	uint64_t gs;
	uint64_t ss;
	uint64_t rf;
};

#elif defined (__aarch64__)

struct user_regs_struct
{
	uint64_t x[32];
	uint64_t pc;
	uint64_t psr;
};

#elif defined (__arm__)

struct user_regs_struct
{
	uint32_t r[16];
	uint32_t cpsr;
};

#elif defined(__riscv_xlen) && __riscv_xlen == 32

struct user_regs_struct
{
	uint32_t r[32];
};

#elif defined(__riscv_xlen) && __riscv_xlen == 64

struct user_regs_struct
{
	uint64_t r[32];
};

#else
# error "unknown arch"
#endif

#ifdef __cplusplus
}
#endif

#endif
