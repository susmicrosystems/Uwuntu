#ifndef UTIME_H
#define UTIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct utimbuf
{
	time_t actime;
	time_t modtime;
};

int utime(const char *file, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif
