#ifndef SYS_UIO_H
#define SYS_UIO_H

#include <sys/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOV_MAX 1024

struct iovec
{
	void *iov_base;
	size_t iov_len;
};

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}
#endif

#endif
