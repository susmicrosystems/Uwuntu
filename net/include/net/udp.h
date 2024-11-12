#ifndef NET_UDP_H
#define NET_UDP_H

#include <types.h>

struct sockaddr;
struct netpkt;
struct netif;
struct sock;

struct udphdr
{
	uint16_t uh_sport;
	uint16_t uh_dport;
	uint16_t uh_ulen;
	uint16_t uh_sum;
};

int udp_open(int domain, struct sock **sock);

int udp_input(struct netif *netif, struct netpkt *pkt, struct sockaddr *src,
              struct sockaddr *dst);

#endif
