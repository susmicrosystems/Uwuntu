#include "../_syscall.h"

#include <sys/stat.h>
#include <fcntl.h>

int lchmod(const char *pathname, mode_t mode)
{
	return syscall4(SYS_fchmodat, AT_FDCWD, (uintptr_t)pathname, mode,
	                AT_SYMLINK_NOFOLLOW);
}
