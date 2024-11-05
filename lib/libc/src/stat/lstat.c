#include "../_syscall.h"

#include <sys/stat.h>
#include <fcntl.h>

int lstat(const char *pathname, struct stat *statbuf)
{
	return syscall4(SYS_fstatat, AT_FDCWD, (uintptr_t)pathname,
	                (uintptr_t)statbuf, AT_SYMLINK_NOFOLLOW);
}
