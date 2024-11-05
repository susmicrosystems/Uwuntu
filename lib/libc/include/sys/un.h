#ifndef SYS_UN_H
#define SYS_UN_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sockaddr_un
{
	sa_family_t sun_family;
	char sun_path[108];
};

#ifdef __cplusplus
}
#endif

#endif
