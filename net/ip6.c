#include <net/ip6.h>
#include <net/net.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/if.h>

#include <errno.h>
#include <sock.h>

struct in6_addr g_inet6_gateway;

int ip6_input(struct netif *netif, struct netpkt *pkt)
{
	struct ip6 *ip6hdr = (struct ip6*)pkt->data;
	if (pkt->len < sizeof(*ip6hdr))
		return -EINVAL;
	net_raw_queue(AF_INET6, pkt);
	/* XXX */
	(void)netif;
	return -EAFNOSUPPORT;
}

int ip6_output(struct sock *sock, struct netpkt *pkt, struct netif *netif,
               struct in6_addr *src, struct in6_addr *dst, uint16_t proto)
{
	(void)sock;
	(void)pkt;
	(void)netif;
	(void)src;
	(void)dst;
	(void)proto;
	/* XXX */
	return -EAFNOSUPPORT;
}

int ip6_setopt(struct sock *sock, int level, int opt, const void *uval,
               socklen_t len)
{
	(void)sock;
	(void)uval;
	(void)len;
	if (level != IPPROTO_IPV6)
		return -EINVAL;
	switch (opt)
	{
		case IPV6_HDRINCL:
			/* XXX */
			return 0;
	}
	return -EINVAL;
}

int ip6_getopt(struct sock *sock, int level, int opt, void *uval,
               socklen_t *ulen)
{
	(void)sock;
	(void)uval;
	(void)ulen;
	if (level != IPPROTO_IPV6)
		return -EINVAL;
	switch (opt)
	{
		case IPV6_HDRINCL:
			/* XXX */
			return 0;
	}
	return -EINVAL;
}
