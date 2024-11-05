#include "../_syscall.h"

#include <signal.h>

#ifndef __aarch64__
__attribute__((naked))
#endif
void sigreturn(void)
{
	syscall0_raw(SYS_sigreturn);
}
