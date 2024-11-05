#ifndef NET_IF_H
#define NET_IF_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ 16

#define IFF_LOOPBACK  (1 << 0)
#define IFF_UP        (1 << 1)
#define IFF_BROADCAST (1 << 2)

struct ifreq
{
	char ifr_name[IFNAMSIZ];
	union
	{
		struct sockaddr ifr_addr;
		struct sockaddr ifr_netmask;
		struct sockaddr ifr_hwaddr;
		uint16_t ifr_flags;
	};
};

struct ifconf
{
	int ifc_len;
	union
	{
		char *ifc_buf;
		struct ifreq *ifc_req;
	};
};

#ifdef __cplusplus
}
#endif

#endif
