#include <net/ip4.h>
#include <net/net.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/arp.h>
#include <net/if.h>

#include <errno.h>
#include <sock.h>
#include <std.h>

struct in_addr g_ip4_gateway;

static uint16_t ip_id;

static inline void print_iphdr(const struct ip *iphdr)
{
	printf("version: %" PRIu8 "\n", iphdr->ip_v);
	printf("ihl: %" PRIu8 "\n", iphdr->ip_hl);
	printf("tos: %" PRIu8 "\n", iphdr->ip_tos);
	printf("length: %" PRId16 "\n", ntohs(iphdr->ip_len));
	printf("ident: %" PRIu16 "\n", ntohs(iphdr->ip_id));
	printf("offset: %" PRId16 "\n", ntohs(iphdr->ip_off));
	printf("ttl: %" PRIu8 "\n", iphdr->ip_ttl);
	printf("proto: %" PRIu8 "\n", iphdr->ip_p);
	printf("checksum: %" PRIx16 "\n", ntohs(iphdr->ip_sum));
	printf("src: %u.%u.%u.%u\n", IN_ADDR_PRINTF(&iphdr->ip_src));
	printf("dst: %u.%u.%u.%u\n", IN_ADDR_PRINTF(&iphdr->ip_dst));
}

int ip4_input(struct netif *netif, struct netpkt *pkt)
{
	struct ip *iphdr = (struct ip*)pkt->data;
	uint16_t iplen;

	if (pkt->len < sizeof(*iphdr))
	{
		printf("ip4: packet too short (no iphdr)\n");
		return -EINVAL;
	}
#if 0
	printf("ip input:\n");
	print_iphdr(iphdr);
#endif
	net_raw_queue(AF_INET, pkt);
	/* XXX do some checks */
	if (iphdr->ip_off & IP_RF)
	{
		printf("ip4: unhandlded IP_RF\n");
		return -EINVAL;
	}
	if (iphdr->ip_off & IP_DF)
	{
		printf("ip4: unhandled IP_DF\n");
		return -EINVAL;
	}
	if (iphdr->ip_off & IP_MF)
	{
		printf("ip4: unhandled IP_MF\n");
		return -EINVAL;
	}
	if (iphdr->ip_off & IP_OFFMASK)
	{
		printf("ip4: unhandled fragmentation\n");
		return -EINVAL;
	}
	iplen = ntohs(iphdr->ip_len);
	if (pkt->len < iplen)
	{
		printf("ip4: packet too short (%" PRIu16 " < %" PRIu16 ")\n",
		       (uint16_t)pkt->len, iplen);
		return -EINVAL;
	}
	if (pkt->len > iplen)
	{
		int ret = netpkt_shrink_tail(pkt, pkt->len - iplen);
		if (ret)
			return ret;
	}
	netpkt_advance(pkt, sizeof(*iphdr));
	struct sockaddr_in src;
	struct sockaddr_in dst;
	src.sin_family = AF_INET;
	src.sin_port = 0;
	src.sin_addr = iphdr->ip_src;
	dst.sin_family = AF_INET;
	dst.sin_port = 0;
	dst.sin_addr = iphdr->ip_dst;
	switch (iphdr->ip_p)
	{
		case IPPROTO_UDP:
			return udp_input(netif, pkt,
			                 (struct sockaddr*)&src,
			                 (struct sockaddr*)&dst);
		case IPPROTO_TCP:
			return tcp_input(netif, pkt,
			                 (struct sockaddr*)&src,
			                 (struct sockaddr*)&dst);
		default:
#if 0
			printf("unknown ip proto: %" PRIu8 "\n", iphdr->ip_p);
#endif
			break;
	}
	return 0;
}

int ip4_output(struct sock *sock, struct netpkt *pkt, struct netif *netif,
               struct in_addr src, struct in_addr dst, uint16_t proto)
{
	struct arp_entry *arp_entry = NULL;
	struct netif_addr *netif_addr;
	struct ip *iphdr;
	int ret;

	(void)sock;
	iphdr = netpkt_grow_front(pkt, sizeof(struct ip));
	if (!iphdr)
	{
		ret = -ENOMEM;
		goto end;
	}
	iphdr->ip_v = 4;
	iphdr->ip_hl = 5;
	iphdr->ip_tos = 0;
	iphdr->ip_len = htons(pkt->len);
	iphdr->ip_id = __atomic_add_fetch(&ip_id, 1, __ATOMIC_SEQ_CST);
	iphdr->ip_off = 0;
	iphdr->ip_ttl = 64;
	iphdr->ip_p = proto;
	iphdr->ip_sum = 0;
	iphdr->ip_src = src;
	iphdr->ip_dst = dst;
	iphdr->ip_sum = ip_checksum(iphdr, sizeof(*iphdr), 0);
#if 0
	printf("ip output:\n");
	print_iphdr(iphdr);
#endif
	if (netif->flags & IFF_LOOPBACK)
	{
		ret = netif->op->emit(netif, pkt);
		goto end;
	}
	/* use arp of gateway if dst isn't in netif
	 * XXX it shouldn't be done that way I guess
	 */
	TAILQ_FOREACH(netif_addr, &netif->addrs, chain)
	{
		if (netif_addr->addr.sa_family != AF_INET)
			continue;
		struct sockaddr_in *sin_addr = (struct sockaddr_in*)&netif_addr->addr;
		struct sockaddr_in *sin_mask = (struct sockaddr_in*)&netif_addr->mask;
		if ((sin_addr->sin_addr.s_addr & sin_mask->sin_addr.s_addr)
		 == (dst.s_addr & sin_mask->sin_addr.s_addr))
			break;
	}
	arp_entry = arp_fetch(netif_addr ? dst : g_ip4_gateway);
	if (!arp_entry)
	{
		ret = -ENOMEM;
		goto end;
	}
	if (arp_entry->state != ARP_ENTRY_RESOLVED)
	{
		arp_resolve(arp_entry, netif, pkt);
		ret = 0;
		goto end;
	}
	ret = ether_output(netif, pkt, &arp_entry->ether, ETHERTYPE_IP);

end:
	if (arp_entry)
		arp_free(arp_entry);
	return ret;
}

int ip4_setopt(struct sock *sock, int level, int opt, const void *uval,
               socklen_t len)
{
	(void)sock;
	(void)uval;
	(void)len;
	if (level != IPPROTO_IP)
		return -EINVAL;
	switch (opt)
	{
		case IP_HDRINCL:
			/* XXX */
			return 0;
	}
	return -EINVAL;
}

int ip4_getopt(struct sock *sock, int level, int opt, void *uval,
               socklen_t *ulen)
{
	(void)sock;
	(void)uval;
	(void)ulen;
	if (level != IPPROTO_IP)
		return -EINVAL;
	switch (opt)
	{
		case IP_HDRINCL:
			/* XXX */
			return 0;
	}
	return -EINVAL;
}

uint16_t ip_checksum(const void *data, size_t size, uint32_t init)
{
	uint32_t result;
	uint16_t *tmp;

	tmp = (uint16_t*)data;
	result = init;
	while (size > 1)
	{
		result += *(tmp++);
		size -= 2;
	}
	if (size)
		result += *((uint8_t*)tmp);
	while (result > 0xFFFF)
		result = ((result >> 16) & 0xFFFF) + (result & 0xFFFF);
	return (~((uint16_t)result));
}

struct netif *ip4_get_dst_netif(struct in_addr *dst,
                                struct netif_addr **netif_addr)
{
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr = *dst;
	struct netif *netif = netif_from_net((struct sockaddr*)&sin, netif_addr);
	if (netif)
		return netif;
	sin.sin_addr = g_ip4_gateway;
	netif = netif_from_net((struct sockaddr*)&sin, netif_addr);
	if (!netif)
		return NULL;
	*dst = g_ip4_gateway;
	return netif;
}
