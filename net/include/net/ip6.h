#ifndef NET_IP6_H
#define NET_IP6_H

#include <types.h>

#define IPV6_HDRINCL 1

#define IN6ADDR_ANY_INIT ((struct in6_addr){0})

struct netpkt;
struct netif;
struct sock;

struct in6_addr
{
	uint8_t s6_addr[16];
};

struct ip6
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint32_t ip6_v : 4;
	uint32_t ip6_tc : 8;
	uint32_t ip6_flow : 20;
#else
	uint32_t ip6_flow : 20;
	uint32_t ip6_tc : 8;
	uint32_t ip6_v : 4;
#endif
	uint16_t ip6_plen;
	uint8_t ip6_nxt;
	uint8_t ip6_hops;
	struct in6_addr ip6_src;
	struct in6_addr ip6_dst;
};

struct sockaddr_in6
{
	sa_family_t sin6_family;
	uint16_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

extern struct in6_addr g_ip6_gateway;

int ip6_input(struct netif *netif, struct netpkt *pkt);
int ip6_output(struct sock *sock, struct netpkt *pkt, struct netif *netif,
               struct in6_addr *src, struct in6_addr *dst, uint16_t proto);

int ip6_setopt(struct sock *sock, int level, int opt, const void *uval,
               socklen_t len);
int ip6_getopt(struct sock *sock, int level, int opt, void *uval,
               socklen_t *ulen);

#endif
