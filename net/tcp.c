#include <net/ip6.h>
#include <net/ip4.h>
#include <net/net.h>
#include <net/tcp.h>
#include <net/if.h>

#include <pipebuf.h>
#include <random.h>
#include <sock.h>
#include <sma.h>
#include <std.h>

struct sock_tcp
{
	struct sock *sock;
	union
	{
		struct
		{
			struct pipebuf outbuf;
			struct pipebuf inbuf;
			int errno; /* for async wait */
			uint32_t lisn; /* local isn */
			uint32_t risn; /* remote isn */
			uint32_t lseq; /* local seq */
			uint32_t rseq; /* remote seq */
			uint32_t lack; /* local ack */
			uint32_t rack; /* remote ack */
			TAILQ_ENTRY(sock_tcp) srv_chain;
		} clt;
		struct
		{
			TAILQ_HEAD(, sock_tcp) queue;
		} srv;
	};
	TAILQ_ENTRY(sock_tcp) chain;
};

struct tcp4_pseudohdr
{
	struct in_addr src;
	struct in_addr dst;
	uint8_t zero;
	uint8_t proto;
	uint16_t len;
} __attribute__ ((packed));

struct tcp6_pseudohdr
{
	struct in6_addr src;
	struct in6_addr dst;
	uint32_t len;
	uint8_t zero[3];
	uint8_t proto;
} __attribute__ ((packed));

static struct spinlock ip4_tcp_socks_lock = SPINLOCK_INITIALIZER();
static TAILQ_HEAD(, sock_tcp) ip4_tcp_socks = TAILQ_HEAD_INITIALIZER(ip4_tcp_socks);
static struct spinlock ip6_tcp_socks_lock = SPINLOCK_INITIALIZER();
static TAILQ_HEAD(, sock_tcp) ip6_tcp_socks = TAILQ_HEAD_INITIALIZER(ip6_tcp_socks);

static struct sma sock_tcp_sma;

static uint16_t ephemeral_start = 49152;
static uint16_t ephemeral_end = 65535;
static uint16_t ephemeral_cur = 0;

static uint16_t tcp_checksum(const struct netpkt *pkt,
                             const struct sockaddr *src,
                             const struct sockaddr *dst);
static int find_ephemeral_port(struct sock *sock);
static int has_matching_sock(struct sockaddr *addr);

static int send_pkt(struct sock_tcp *sock_tcp, struct netpkt *pkt);
static int forge_syn(struct sock_tcp *sock_tcp, struct netpkt **pkt);
static int forge_ack(struct sock_tcp *sock_tcp, struct netpkt **pkt);
static int forge_data(struct sock_tcp *sock_tcp, size_t bytes,
                      struct netpkt **pkt);
static int forge_synack(struct sock_tcp *sock_tcp, struct netpkt **pkt);
static int send_data(struct sock_tcp *sock_tcp);

void sock_tcp_init(void)
{
	sma_init(&sock_tcp_sma, sizeof(struct sock_tcp), NULL, NULL, "sock_tcp");
}

static inline void print_tcphdr(const struct tcphdr *tcphdr)
{
	printf("sport: %" PRIu16 "\n", htons(tcphdr->th_sport));
	printf("dport: %" PRIu16 "\n", htons(tcphdr->th_dport));
	printf("seq: %" PRIu32 "\n", htonl(tcphdr->th_seq));
	printf("ack: %" PRIu32 "\n", htonl(tcphdr->th_ack));
	printf("off: %" PRIu8 "\n", tcphdr->th_off);
	printf("flags: 0x%02" PRIx8, tcphdr->th_flags);
	if (tcphdr->th_flags & TH_FIN)
		printf(" FIN");
	if (tcphdr->th_flags & TH_SYN)
		printf(" SYN");
	if (tcphdr->th_flags & TH_RST)
		printf(" RST");
	if (tcphdr->th_flags & TH_PUSH)
		printf(" PUSH");
	if (tcphdr->th_flags & TH_ACK)
		printf(" ACK");
	if (tcphdr->th_flags & TH_URG)
		printf(" URG");
	printf("\n");
	printf("win: %" PRIu16 "\n", htons(tcphdr->th_win));
	printf("urp: 0x%04" PRIx16 "\n", htons(tcphdr->th_urp));
}

static int init_clt(struct sock_tcp *sock_tcp)
{
	struct sock *sock = sock_tcp->sock;
	int ret;

	ret = pipebuf_init(&sock_tcp->clt.outbuf, PAGE_SIZE * 16,
	                   &sock->mutex, NULL, &sock->wwaitq);
	if (ret)
		return ret;
	sock_tcp->clt.outbuf.nreaders = 1;
	sock_tcp->clt.outbuf.nwriters = 1;
	ret = pipebuf_init(&sock_tcp->clt.inbuf, PAGE_SIZE * 16,
	                   &sock->mutex, &sock->rwaitq, NULL);
	if (ret)
	{
		pipebuf_destroy(&sock_tcp->clt.outbuf);
		return ret;
	}
	sock_tcp->clt.inbuf.nreaders = 1;
	sock_tcp->clt.inbuf.nwriters = 1;
	return 0;
}

static void destroy_clt(struct sock_tcp *sock_tcp)
{
	pipebuf_destroy(&sock_tcp->clt.outbuf);
	pipebuf_destroy(&sock_tcp->clt.inbuf);
}

ssize_t tcp_send(struct sock *sock, struct msghdr *msg, int flags)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	struct uio uio;
	ssize_t ret;
	ssize_t bytes;

	(void)flags;
	if (msg->msg_name)
		return -EISCONN;
	sock_lock(sock);
	if (sock->state == SOCK_ST_CLOSED)
	{
		ret = -EPIPE;
		goto end;
	}
	if (sock->state != SOCK_ST_CONNECTED)
	{
		ret = -ENOTCONN;
		goto end;
	}
	uio_from_msghdr(&uio, msg);
	bytes = pipebuf_write_locked(&sock_tcp->clt.outbuf, &uio,
	                             (flags & MSG_DONTWAIT) ? 0 : 1,
	                             (sock->rcv_timeo.tv_sec
	                           || sock->rcv_timeo.tv_nsec)
	                            ? &sock->rcv_timeo : NULL);
	if (bytes < 0)
	{
		ret = bytes;
		goto end;
	}
	ret = send_data(sock_tcp);
	if (ret)
		goto end;
	ret = bytes;

end:
	sock_unlock(sock);
	return ret;
}

ssize_t tcp_recv(struct sock *sock, struct msghdr *msg, int flags)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	struct uio uio;
	ssize_t ret;

	sock_lock(sock);
	if (sock->state != SOCK_ST_CONNECTED
	 && sock->state != SOCK_ST_CLOSED)
	{
		ret = -ENOTCONN;
		goto end;
	}
	uio_from_msghdr(&uio, msg);
	ret = pipebuf_read_locked(&sock_tcp->clt.inbuf, &uio,
	                          (flags & MSG_DONTWAIT) ? 0 : 1,
	                          (sock->snd_timeo.tv_sec
	                        || sock->snd_timeo.tv_nsec)
	                         ? &sock->rcv_timeo : NULL);
	if (ret < 0)
		goto end;
	/* XXX update window size ? */

end:
	sock_unlock(sock);
	return ret;
}

int tcp_poll(struct sock *sock, struct poll_entry *entry)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	int ret = 0;

	sock_lock(sock);
	if (sock->state != SOCK_ST_CONNECTED
	 && sock->state != SOCK_ST_CLOSED)
	{
		ret = -ENOTCONN;
		goto end;
	}
	if (sock->state == SOCK_ST_CLOSED)
		ret |= POLLHUP;
	/* XXX better masks */
	ret |= pipebuf_poll_locked(&sock_tcp->clt.inbuf,
	                           entry->events & ~POLLOUT);
	ret |= pipebuf_poll_locked(&sock_tcp->clt.outbuf,
	                           entry->events & ~POLLIN);
	if (ret)
		goto end;
	entry->file_head = &sock->poll_entries;
	ret = poller_add(entry);

end:
	sock_unlock(sock);
	return ret;
}

int tcp_find_local_address(struct sock *sock, const struct sockaddr *addr)
{
	struct netif_addr *netif_addr;
	struct netif *netif = NULL;
	int ret;

	switch (sock->domain)
	{
		case AF_INET:
		{
			struct sockaddr_in sin;
			sin.sin_family = AF_INET;
			sin.sin_addr = ((struct sockaddr_in*)addr)->sin_addr;
			netif = ip4_get_dst_netif(&sin.sin_addr, &netif_addr);
			if (!netif)
				return -EADDRNOTAVAIL;
			sock->src_addr.sin.sin_addr = ((struct sockaddr_in*)&netif_addr->addr)->sin_addr;
			sock->src_addr.sin.sin_family = AF_INET;
			ret = find_ephemeral_port(sock);
			if (ret)
				return ret;
			ret = 0;
			break;
		}
		case AF_INET6:
			ret = -EAFNOSUPPORT; /* XXX */
			break;
		default:
			ret = -EAFNOSUPPORT;
			break;
	}
	if (netif)
		netif_free(netif);
	return ret;
}

int tcp_connect(struct sock *sock, const struct sockaddr *addr,
                socklen_t addrlen)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	struct netpkt *pkt = NULL;
	ssize_t ret;

	(void)addrlen;
	sock_lock(sock);
	if (sock->state != SOCK_ST_NONE)
	{
		ret = -EISCONN;
		goto end;
	}
	ret = random_get(&sock_tcp->clt.lisn, sizeof(sock_tcp->clt.lisn));
	if (ret < 0)
		goto end;
	if (ret != sizeof(sock_tcp->clt.lisn))
	{
		ret = -ENOMEM; /* XXX */
		goto end;
	}
	switch (sock->domain)
	{
		case AF_INET:
			sock->dst_addr.sin = *(struct sockaddr_in*)addr;
			if (!sock->dst_addr.sin.sin_port)
			{
				ret = -EINVAL;
				goto end;
			}
			sock->dst_addrlen = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			sock->dst_addr.sin6 = *(struct sockaddr_in6*)addr;
			if (!sock->dst_addr.sin6.sin6_port)
			{
				ret = -EINVAL;
				goto end;
			}
			sock->dst_addrlen = sizeof(struct sockaddr_in6);
			break;
		default:
			ret = -EAFNOSUPPORT;
			goto end;
	}
	if (!sock->src_addrlen)
	{
		ret = tcp_find_local_address(sock, addr);
		if (ret)
			goto end;
	}
	ret = init_clt(sock_tcp);
	if (ret)
	{
		destroy_clt(sock_tcp);
		goto end;
	}
	ret = forge_syn(sock_tcp, &pkt);
	if (ret)
	{
		destroy_clt(sock_tcp);
		goto end;
	}
	ret = send_pkt(sock_tcp, pkt);
	if (ret)
	{
		destroy_clt(sock_tcp);
		goto end;
	}
	sock->state = SOCK_ST_CONNECTING;
	ret = waitq_wait_tail_mutex(&sock->wwaitq, &sock->mutex, NULL);
	if (ret)
	{
		destroy_clt(sock_tcp);
		goto end;
	}
	if (sock_tcp->clt.errno)
	{
		sock->state = SOCK_ST_NONE;
		ret = sock_tcp->clt.errno;
		destroy_clt(sock_tcp);
		goto end;
	}
	ret = 0;

end:
	if (ret < 0 && pkt)
		netpkt_free(pkt);
	sock_unlock(sock);
	return ret;
}

int tcp_bind(struct sock *sock, const struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	(void)addrlen;
	sock_lock(sock);
	if (sock->state != SOCK_ST_NONE)
	{
		ret = -EISCONN;
		goto end;
	}
	switch (sock->domain)
	{
		case AF_INET:
		{
			const struct sockaddr_in *sin = (const struct sockaddr_in*)addr;
			/* XXX more ip check */
			sock->src_addr.sin = *sin;
			if (!sock->src_addr.sin.sin_port)
			{
				ret = find_ephemeral_port(sock);
				if (ret)
					goto end;
			}
			else
			{
				if (has_matching_sock(&sock->src_addr.sa))
				{
					ret = -EADDRINUSE;
					goto end;
				}
			}
			sock->src_addrlen = sizeof(*sin);
			ret = 0;
			break;
		}
		case AF_INET6:
		{
			const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)addr;
			/* XXX more ip check */
			sock->src_addr.sin6 = *sin6;
			if (!sock->src_addr.sin6.sin6_port)
			{
				ret = find_ephemeral_port(sock);
				if (ret)
					goto end;
			}
			else
			{
				if (has_matching_sock(&sock->src_addr.sa))
				{
					ret = -EADDRINUSE;
					goto end;
				}
			}
			sock->src_addrlen = sizeof(*sin6);
			ret = 0;
			break;
		}
		default:
			ret = -EAFNOSUPPORT;
			break;
	}

end:
	sock_unlock(sock);
	return ret;
}

int tcp_listen(struct sock *sock, int backlog)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	int ret;

	if (backlog <= 0)
		return -EINVAL;
	sock_lock(sock);
	if (sock->state != SOCK_ST_NONE)
	{
		ret = -EISCONN;
		goto end;
	}
	if (!sock->src_addrlen)
	{
		switch (sock->domain)
		{
			case AF_INET:
				sock->src_addr.sin.sin_family = AF_INET;
				sock->src_addr.sin.sin_addr.s_addr = INADDR_ANY;
				sock->src_addr.sin.sin_port = 0;
				break;
			case AF_INET6:
				/* XXX */
				ret = -EAFNOSUPPORT;
				goto end;
			default:
				ret = -EAFNOSUPPORT;
				goto end;
		}
		ret = find_ephemeral_port(sock);
		if (ret)
			goto end;
	}
	TAILQ_INIT(&sock_tcp->srv.queue);
	sock->state = SOCK_ST_LISTENING;
	ret = 0;

end:
	sock_unlock(sock);
	return ret;
}

int tcp_accept(struct sock *sock, struct sock **child)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	struct sock_tcp *child_tcp;
	int ret;

	sock_lock(sock);
	if (sock->state != SOCK_ST_LISTENING)
	{
		ret = -EISCONN;
		goto end;
	}
	while (TAILQ_EMPTY(&sock_tcp->srv.queue))
	{
		ret = waitq_wait_tail_mutex(&sock->rwaitq, &sock->mutex, NULL);
		if (ret)
			goto end;
	}
	child_tcp = TAILQ_FIRST(&sock_tcp->srv.queue);
	TAILQ_REMOVE(&sock_tcp->srv.queue, child_tcp, clt.srv_chain);
	*child = child_tcp->sock;
	ret = 0;

end:
	sock_unlock(sock);
	return ret;
}

int tcp_release(struct sock *sock)
{
	struct sock_tcp *sock_tcp = sock->userdata;

	switch (sock->domain)
	{
		case AF_INET:
			spinlock_lock(&ip4_tcp_socks_lock);
			TAILQ_REMOVE(&ip4_tcp_socks, sock_tcp, chain);
			spinlock_unlock(&ip4_tcp_socks_lock);
			break;
		case AF_INET6:
			spinlock_lock(&ip6_tcp_socks_lock);
			TAILQ_REMOVE(&ip6_tcp_socks, sock_tcp, chain);
			spinlock_unlock(&ip6_tcp_socks_lock);
			break;
		default:
			panic("unknown domain\n");
	}
	switch (sock->state)
	{
		case SOCK_ST_CONNECTING:
		case SOCK_ST_CONNECTED:
		case SOCK_ST_CLOSING:
		case SOCK_ST_CLOSED:
			destroy_clt(sock_tcp);
			break;
		case SOCK_ST_NONE:
			break;
		case SOCK_ST_LISTENING:
		{
			struct sock_tcp *child = TAILQ_FIRST(&sock_tcp->srv.queue);
			while (child)
			{
				TAILQ_REMOVE(&sock_tcp->srv.queue, child, clt.srv_chain);
				sock_free(child->sock);
				child = TAILQ_FIRST(&sock_tcp->srv.queue);
			}
			break;
		}
	}
	sma_free(&sock_tcp_sma, sock_tcp);
	return 0;
}

int tcp_setopt(struct sock *sock, int level, int opt, const void *uval,
               socklen_t len)
{
	int ret;

	sock_lock(sock);
	switch (level)
	{
		case SOL_SOCKET:
			ret = sock_sol_setopt(sock, level, opt, uval, len);
			break;
		case IPPROTO_IP:
			if (sock->domain == AF_INET)
				ret = ip4_setopt(sock, level, opt, uval, len);
			else
				ret = -EINVAL;
			break;
		case IPPROTO_IPV6:
			if (sock->domain == AF_INET6)
				ret = ip6_setopt(sock, level, opt, uval, len);
			else
				ret = -EINVAL;
			break;
		default:
			ret = -EINVAL;
			break;
	}
	sock_unlock(sock);
	return ret;
}

int tcp_getopt(struct sock *sock, int level, int opt, void *uval,
               socklen_t *ulen)
{
	int ret;

	sock_lock(sock);
	switch (level)
	{
		case SOL_SOCKET:
			ret = sock_sol_getopt(sock, level, opt, uval, ulen);
			break;
		case IPPROTO_IP:
			if (sock->domain == AF_INET)
				ret = ip4_getopt(sock, level, opt, uval, ulen);
			else
				ret = -EINVAL;
			break;
		case IPPROTO_IPV6:
			if (sock->domain == AF_INET6)
				ret = ip6_getopt(sock, level, opt, uval, ulen);
			else
				ret = -EINVAL;
			break;
		default:
			ret = -EINVAL;
			break;
	}
	sock_unlock(sock);
	return ret;
}

int tcp_ioctl(struct sock *sock, unsigned long request, uintptr_t data)
{
	return sock_sol_ioctl(sock, request, data);
}

int tcp_shutdown(struct sock *sock, int how)
{
	(void)sock;
	(void)how;
	/* XXX */
	return 0;
}

static const struct sock_op tcp_op =
{
	.release = tcp_release,
	.send = tcp_send,
	.recv = tcp_recv,
	.poll = tcp_poll,
	.connect = tcp_connect,
	.bind = tcp_bind,
	.listen = tcp_listen,
	.accept = tcp_accept,
	.setopt = tcp_setopt,
	.getopt = tcp_getopt,
	.ioctl = tcp_ioctl,
	.shutdown = tcp_shutdown,
};

int tcp_open(int domain, struct sock **sock)
{
	struct sock_tcp *sock_tcp = sma_alloc(&sock_tcp_sma, M_ZERO);
	if (!sock_tcp)
		return -ENOMEM;
	int ret = sock_new(domain, SOCK_STREAM, IPPROTO_TCP, &tcp_op, sock);
	if (ret)
	{
		sma_free(&sock_tcp_sma, sock_tcp);
		return ret;
	}
	sock_tcp->sock = *sock;
	(*sock)->userdata = sock_tcp;
	switch (domain)
	{
		case AF_INET:
			spinlock_lock(&ip4_tcp_socks_lock);
			TAILQ_INSERT_TAIL(&ip4_tcp_socks, sock_tcp, chain);
			spinlock_unlock(&ip4_tcp_socks_lock);
			break;
		case AF_INET6:
			spinlock_lock(&ip6_tcp_socks_lock);
			TAILQ_INSERT_TAIL(&ip6_tcp_socks, sock_tcp, chain);
			spinlock_unlock(&ip6_tcp_socks_lock);
			break;
		default:
			panic("unknown domain\n");
	}
	return 0;
}

static uint16_t tcp4_checksum(const struct netpkt *pkt,
                              const struct in_addr src,
                              const struct in_addr dst)
{
	struct tcp4_pseudohdr phdr;
	uint32_t result;

	phdr.src = src;
	phdr.dst = dst;
	phdr.zero = 0;
	phdr.proto = IPPROTO_TCP;
	phdr.len = ntohs(pkt->len);

	result  = ((uint16_t*)&phdr)[0];
	result += ((uint16_t*)&phdr)[1];
	result += ((uint16_t*)&phdr)[2];
	result += ((uint16_t*)&phdr)[3];
	result += ((uint16_t*)&phdr)[4];
	result += ((uint16_t*)&phdr)[5];

	return ip_checksum(pkt->data, pkt->len, result);
}

static uint16_t tcp6_checksum(const struct netpkt *pkt,
                              const struct in6_addr *src,
                              const struct in6_addr *dst)
{
	struct tcp6_pseudohdr phdr;
	uint32_t result;

	phdr.src = *src;
	phdr.dst = *dst;
	phdr.len = ntohl(pkt->len);
	phdr.zero[0] = 0;
	phdr.zero[1] = 0;
	phdr.zero[2] = 0;
	phdr.proto = IPPROTO_TCP;

	result = 0;
	for (size_t i = 0; i < sizeof(phdr) / 2; ++i)
		result += ((uint16_t*)&phdr)[i];

	return ip_checksum(pkt->data, pkt->len, result);
}

static uint16_t tcp_checksum(const struct netpkt *netpkt,
                             const struct sockaddr *src,
                             const struct sockaddr *dst)
{
	switch (src->sa_family)
	{
		case AF_INET:
			return tcp4_checksum(netpkt,
			                     ((struct sockaddr_in*)src)->sin_addr,
			                     ((struct sockaddr_in*)dst)->sin_addr);
		case AF_INET6:
			return tcp6_checksum(netpkt,
			                     &((struct sockaddr_in6*)src)->sin6_addr,
			                     &((struct sockaddr_in6*)dst)->sin6_addr);
		default:
			panic("unknown family\n");
			return 0;
	}
}

static void tcp4_find_input_sockets(struct sock_tcp **both_match,
                                    struct sock_tcp **dst_match,
                                    const struct sockaddr *src,
                                    const struct sockaddr *dst,
                                    const struct tcphdr *tcphdr)
{
	struct sock_tcp *sock;

	spinlock_lock(&ip4_tcp_socks_lock);
	TAILQ_FOREACH(sock, &ip4_tcp_socks, chain)
	{
		struct sockaddr_in *src_sin = &sock->sock->src_addr.sin;
		struct sockaddr_in *dst_sin = &sock->sock->dst_addr.sin;
		if (!sock->sock->src_addrlen
		 || src_sin->sin_family != AF_INET
		 || (src_sin->sin_addr.s_addr != INADDR_ANY
		  && src_sin->sin_addr.s_addr != ((struct sockaddr_in*)dst)->sin_addr.s_addr)
		 || src_sin->sin_port != tcphdr->th_dport)
			continue;
		if (sock->sock->dst_addrlen
		 && dst_sin->sin_family == AF_INET
		 && dst_sin->sin_addr.s_addr == ((struct sockaddr_in*)src)->sin_addr.s_addr
		 && dst_sin->sin_port == tcphdr->th_sport)
		{
			*both_match = sock;
			sock_ref(sock->sock);
			break;
		}
		if (!*dst_match)
		{
			*dst_match = sock;
			sock_ref(sock->sock);
		}
	}
	spinlock_unlock(&ip4_tcp_socks_lock);
}

static void tcp6_find_input_sockets(struct sock_tcp **both_match,
                                    struct sock_tcp **dst_match,
                                    const struct sockaddr *src,
                                    const struct sockaddr *dst,
                                    const struct tcphdr *tcphdr)
{
	static const struct in6_addr in6_any = IN6ADDR_ANY_INIT;
	struct sock_tcp *sock;

	spinlock_lock(&ip6_tcp_socks_lock);
	TAILQ_FOREACH(sock, &ip6_tcp_socks, chain)
	{
		struct sockaddr_in6 *src_sin6 = &sock->sock->src_addr.sin6;
		struct sockaddr_in6 *dst_sin6 = &sock->sock->dst_addr.sin6;
		if (!sock->sock->src_addrlen
		 || src_sin6->sin6_family != AF_INET6
		 || (memcmp(&src_sin6->sin6_addr, &in6_any, sizeof(src_sin6->sin6_addr))
		  && memcmp(&src_sin6->sin6_addr, &((struct sockaddr_in6*)dst)->sin6_addr, sizeof(src_sin6->sin6_addr)))
		 || src_sin6->sin6_port != tcphdr->th_dport)
			continue;
		if (sock->sock->dst_addrlen
		 && dst_sin6->sin6_family == AF_INET6
		 && !memcmp(&dst_sin6->sin6_addr, &((struct sockaddr_in6*)src)->sin6_addr, sizeof(dst_sin6->sin6_addr))
		 && dst_sin6->sin6_port == tcphdr->th_sport)
		{
			*both_match = sock;
			sock_ref(sock->sock);
			break;
		}
		if (!*dst_match)
		{
			*dst_match = sock;
			sock_ref(sock->sock);
		}
	}
	spinlock_unlock(&ip6_tcp_socks_lock);
}

static int find_input_socket(const struct sockaddr *src,
                             const struct sockaddr *dst,
                             const struct tcphdr *tcphdr,
                             struct sock_tcp **sock_tcp)
{
	struct sock_tcp *both_match = NULL;
	struct sock_tcp *dst_match = NULL;

	switch (src->sa_family)
	{
		case AF_INET:
			tcp4_find_input_sockets(&both_match, &dst_match,
			                        src, dst, tcphdr);
			break;
		case AF_INET6:
			tcp6_find_input_sockets(&both_match, &dst_match,
			                        src, dst, tcphdr);
			break;
		default:
			return -EAFNOSUPPORT;
	}
	if (both_match)
	{
		*sock_tcp = both_match;
		if (dst_match)
			sock_free(dst_match->sock);
	}
	else if (dst_match)
	{
		*sock_tcp = dst_match;
	}
	else
	{
		*sock_tcp = NULL;
	}
	return 0;
}

static int handle_synack(struct sock *sock, struct netpkt *pkt)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	struct tcphdr *tcphdr = pkt->data;
	struct netpkt *pkt_ack = NULL;
	int ret;

	if (tcphdr->th_flags & TH_RST)
	{
		sock->state = SOCK_ST_NONE;
		destroy_clt(sock_tcp);
		ret = -ECONNREFUSED;
		goto end;
	}
	if (tcphdr->th_flags != (TH_SYN | TH_ACK))
	{
		printf("tcp: SYN answer isn't SYN | ACK\n");
		ret = -EINVAL;
		goto end;
	}
	if (htonl(tcphdr->th_ack) != sock_tcp->clt.lisn + 1)
	{
		printf("tcp: invalid SYN | ACK ack\n");
		ret = -EINVAL;
		goto end;
	}
	sock_tcp->clt.lseq = sock_tcp->clt.lisn + 1;
	sock_tcp->clt.rack = htonl(tcphdr->th_ack);
	sock_tcp->clt.risn = htonl(tcphdr->th_seq);
	sock_tcp->clt.rseq = sock_tcp->clt.risn + 1;
	ret = forge_ack(sock_tcp, &pkt_ack);
	if (ret)
		goto end;
	ret = send_pkt(sock_tcp, pkt_ack);
	if (ret)
	{
		sock->state = SOCK_ST_NONE;
		destroy_clt(sock_tcp);
	}
	else
	{
		sock->state = SOCK_ST_CONNECTED;
	}

end:
	if (ret < 0 && pkt_ack)
		netpkt_free(pkt_ack);
	sock_tcp->clt.errno = ret;
	waitq_broadcast(&sock->wwaitq, 0);
	return ret;
}

static int handle_pkt(struct sock *sock, struct netpkt *pkt)
{
	struct sock_tcp *sock_tcp = sock->userdata;
	struct tcphdr *tcphdr = pkt->data;
	uint32_t seq;
	uint32_t ack;
	int ret;

	seq = ntohl(tcphdr->th_seq);
	ack = ntohl(tcphdr->th_ack);
	if (tcphdr->th_flags & TH_ACK)
	{
		if (ack < sock_tcp->clt.rack)
		{
			 /* XXX handle seq overflow */
			printf("tcp: received ACK lower than previous one\n");
			return -EINVAL;
		}
		sock_tcp->clt.rack = ack;
		/* XXX mvoe ack pointer to commit sent data */
	}
	if (seq < sock_tcp->clt.rseq)
	{
		printf("tcp: received seq lower than previous one\n");
		return -EINVAL;
	}
	if (pkt->len > tcphdr->th_off * 4)
	{
		size_t payload_size = pkt->len - tcphdr->th_off * 4;
		size_t avail = ringbuf_write_size(&sock_tcp->clt.inbuf.ringbuf);
		if (avail < payload_size)
		{
			printf("tcp: received too much data (%zu > %zu)\n",
			       payload_size, avail);
			return -EINVAL;
		}
		ringbuf_write(&sock_tcp->clt.inbuf.ringbuf,
		              &((uint8_t*)pkt->data)[tcphdr->th_off * 4],
		              payload_size);
		sock_tcp->clt.rseq += payload_size;
		poller_broadcast(&sock->poll_entries, POLLIN);
		waitq_broadcast(&sock->rwaitq, 0);
		struct netpkt *pkt_ack = NULL;
		ret = forge_ack(sock_tcp, &pkt_ack);
		if (ret)
		{
			if (pkt_ack)
				netpkt_free(pkt_ack);
			return ret;
		}
		ret = send_pkt(sock_tcp, pkt_ack);
		if (ret < 0)
		{
			netpkt_free(pkt_ack);
			return ret;
		}
	}
	if (tcphdr->th_flags & TH_FIN)
	{
		sock_tcp->clt.rseq++;
		sock->state = SOCK_ST_CLOSED;
		sock_tcp->clt.outbuf.nreaders = 0;
		sock_tcp->clt.inbuf.nwriters = 0;
		poller_broadcast(&sock->poll_entries, POLLHUP);
		waitq_broadcast(&sock->rwaitq, 0);
		struct netpkt *pkt_ack = NULL;
		ret = forge_ack(sock_tcp, &pkt_ack);
		if (ret)
		{
			if (pkt_ack)
				netpkt_free(pkt_ack);
			return ret;
		}
		ret = send_pkt(sock_tcp, pkt_ack);
		if (ret < 0)
		{
			netpkt_free(pkt_ack);
			return ret;
		}
	}
	return 0;
}

static int handle_syn(struct sock *sock, struct netpkt *pkt,
                      struct sockaddr *src, struct sockaddr *dst)
{
	struct sock_tcp *child_tcp;
	struct sock_tcp *sock_tcp = sock->userdata;
	struct tcphdr *tcphdr = pkt->data;
	struct netpkt *pkt_synack;
	struct sock *child;
	uint32_t lisn;
	int ret;

	if (tcphdr->th_ack)
	{
		printf("tcp: incoming SYN has non-null ack\n");
		return -EINVAL;
	}
	if (tcphdr->th_flags != TH_SYN)
	{
		printf("tcp: incoming SYN isn't only syn: %x\n", tcphdr->th_flags);
		return -EINVAL;
	}
	if (pkt->len != tcphdr->th_off * 4)
	{
		printf("tcp: incoming SYN has payload data\n");
		return -EINVAL;
	}
	ret = random_get(&lisn, sizeof(lisn));
	if (ret < 0)
		return ret;
	if (ret != sizeof(lisn))
		return -ENOMEM; /* XXX */
	child_tcp = sma_alloc(&sock_tcp_sma, M_ZERO);
	if (!child_tcp)
		return -ENOMEM;
	ret = sock_new(sock->domain, SOCK_STREAM, IPPROTO_TCP, &tcp_op, &child);
	if (ret)
	{
		sma_free(&sock_tcp_sma, child_tcp);
		return ret;
	}
	child_tcp->sock = child;
	child->userdata = child_tcp;
	child_tcp->clt.lisn = lisn;
	child_tcp->clt.risn = htonl(tcphdr->th_seq);
	child_tcp->clt.lseq = lisn + 1;
	child_tcp->clt.rseq = child_tcp->clt.risn + 1;
	ret = init_clt(child_tcp);
	switch (sock->domain)
	{
		case AF_INET:
			child->src_addr.sin = *(struct sockaddr_in*)dst;
			child->dst_addr.sin = *(struct sockaddr_in*)src;
			child->src_addr.sin.sin_port = tcphdr->th_dport;
			child->dst_addr.sin.sin_port = tcphdr->th_sport;
			child->src_addrlen = sizeof(struct sockaddr_in);
			child->dst_addrlen = sizeof(struct sockaddr_in);
			spinlock_lock(&ip4_tcp_socks_lock);
			TAILQ_INSERT_TAIL(&ip4_tcp_socks, child_tcp, chain);
			spinlock_unlock(&ip4_tcp_socks_lock);
			break;
		case AF_INET6:
			child->src_addr.sin6 = *(struct sockaddr_in6*)dst;
			child->dst_addr.sin6 = *(struct sockaddr_in6*)src;
			child->src_addr.sin6.sin6_port = tcphdr->th_dport;
			child->dst_addr.sin6.sin6_port = tcphdr->th_sport;
			child->src_addrlen = sizeof(struct sockaddr_in6);
			child->dst_addrlen = sizeof(struct sockaddr_in6);
			spinlock_lock(&ip6_tcp_socks_lock);
			TAILQ_INSERT_TAIL(&ip6_tcp_socks, child_tcp, chain);
			spinlock_unlock(&ip6_tcp_socks_lock);
			break;
		default:
			panic("unknown domain\n");
	}
	ret = forge_synack(child_tcp, &pkt_synack);
	if (ret)
	{
		if (pkt_synack)
			netpkt_free(pkt_synack);
		destroy_clt(child_tcp);
		sock_free(child);
		return ret;
	}
	ret = send_pkt(child_tcp, pkt_synack);
	if (ret < 0)
	{
		netpkt_free(pkt_synack);
		destroy_clt(child_tcp);
		sock_free(child);
		return ret;
	}
	child->state = SOCK_ST_CONNECTED;
	TAILQ_INSERT_TAIL(&sock_tcp->srv.queue, child_tcp, clt.srv_chain);
	waitq_signal(&sock->rwaitq, 0);
	return 0;
}

int tcp_input(struct netif *netif, struct netpkt *pkt, struct sockaddr *src,
              struct sockaddr *dst)
{
	struct sock_tcp *sock_tcp;
	struct tcphdr *tcphdr;
	struct sock *sock;
	uint16_t chk_cksum;
	uint16_t cksum;
	int ret;

	(void)netif;
	if (pkt->len < sizeof(*tcphdr))
	{
		printf("tcp: packet too short\n");
		return -EINVAL;
	}
	tcphdr = pkt->data;
#if 0
	printf("[%" PRId64 "] ==INPUT==\n", realtime_seconds());
	print_tcphdr(tcphdr);
#endif
	if (tcphdr->th_off < sizeof(*tcphdr) / 4)
	{
		printf("tcp: offset too small\n");
		return -EINVAL;
	}
	if (pkt->len < tcphdr->th_off * 4)
	{
		printf("tcp: offset too big\n");
		return -EINVAL;
	}
	cksum = tcphdr->th_sum;
	tcphdr->th_sum = 0;
	chk_cksum = tcp_checksum(pkt, src, dst);
	if (cksum != chk_cksum)
	{
		printf("tcp: invalid checksum: got %04" PRIx16 ", expected %04" PRIx16 "\n",
		       cksum, chk_cksum);
		return -EINVAL;
	}
	ret = find_input_socket(src, dst, tcphdr, &sock_tcp);
	if (ret)
		return ret;
	if (!sock_tcp)
		return 0;
	sock = sock_tcp->sock;
	sock_lock(sock); /* XXX sleepable lock on interrupt is NOT a good idea */
	if (sock->state == SOCK_ST_CONNECTING)
		ret = handle_synack(sock, pkt);
	else if (sock->state == SOCK_ST_LISTENING)
		ret = handle_syn(sock, pkt, src, dst);
	else if (sock->state != SOCK_ST_NONE)
		ret = handle_pkt(sock, pkt);
	else
		ret = -EINVAL;
	sock_unlock(sock);
	sock_free(sock);
	return ret;
}

static void socks_lock(int domain)
{
	switch (domain)
	{
		case AF_INET:
			spinlock_lock(&ip4_tcp_socks_lock);
			break;
		case AF_INET6:
			spinlock_lock(&ip6_tcp_socks_lock);
			break;
		default:
			panic("unknown domain\n");
	}
}

static void socks_unlock(int domain)
{
	switch (domain)
	{
		case AF_INET:
			spinlock_unlock(&ip4_tcp_socks_lock);
			break;
		case AF_INET6:
			spinlock_unlock(&ip6_tcp_socks_lock);
			break;
		default:
			panic("unknown domain\n");
	}
}

static int has_matching_sock_locked(struct sockaddr *sockaddr)
{
	struct sock_tcp *sock;
	switch (sockaddr->sa_family)
	{
		case AF_INET:
			TAILQ_FOREACH(sock, &ip4_tcp_socks, chain)
			{
				struct sockaddr_in *sin = (struct sockaddr_in*)sockaddr;
				struct sockaddr_in *src_sin = &sock->sock->src_addr.sin;
				if (sock->sock->src_addrlen
				 && src_sin->sin_family == AF_INET
				 && (src_sin->sin_addr.s_addr == INADDR_ANY
				  || src_sin->sin_addr.s_addr == sin->sin_addr.s_addr)
				 && src_sin->sin_port == sin->sin_port)
					break;
			}
			break;
		case AF_INET6:
		{
			static const struct in6_addr in6_any = IN6ADDR_ANY_INIT;
			TAILQ_FOREACH(sock, &ip6_tcp_socks, chain)
			{
				struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sockaddr;
				struct sockaddr_in6 *src_sin6 = &sock->sock->src_addr.sin6;
				if (sock->sock->src_addrlen
				 && src_sin6->sin6_family == AF_INET6
				 && (!memcmp(&src_sin6->sin6_addr, &in6_any, sizeof(sin6->sin6_addr))
				  || !memcmp(&src_sin6->sin6_addr, &sin6->sin6_addr, sizeof(sin6->sin6_addr)))
				 && src_sin6->sin6_port == sin6->sin6_port)
					break;
			}
			break;
		}
		default:
			panic("unknown domain\n");
	}
	if (sock)
		return 1;
	return 0;
}

static int has_matching_sock(struct sockaddr *addr)
{
	socks_lock(addr->sa_family);
	int ret = has_matching_sock_locked(addr);
	socks_unlock(addr->sa_family);
	return ret;
}

static int find_ephemeral_port(struct sock *sock)
{
	uint16_t ephemeral_count = ephemeral_end - ephemeral_start;
	uint16_t *port_ptr;

	switch (sock->domain)
	{
		case AF_INET:
			port_ptr = &sock->src_addr.sin.sin_port;
			break;
		case AF_INET6:
			port_ptr = &sock->src_addr.sin6.sin6_port;
			break;
		default:
			return -EAFNOSUPPORT;
	}
	for (size_t i = 0; i < ephemeral_count; ++i)
	{
		*port_ptr = htons(ephemeral_start + ephemeral_cur);
		socks_lock(sock->domain);
		int ret = has_matching_sock_locked(&sock->src_addr.sa);
		ephemeral_cur = ephemeral_cur + 1;
		ephemeral_cur %= ephemeral_count;
		if (!ret)
		{
			switch (sock->domain)
			{
				case AF_INET:
					sock->src_addrlen = sizeof(struct sockaddr_in);
					break;
				case AF_INET6:
					sock->src_addrlen = sizeof(struct sockaddr_in6);
					break;
				default:
					panic("unknown domain\n");
			}
			socks_unlock(sock->domain);
			return 0;
		}
		socks_unlock(sock->domain);
	}
	return -EADDRINUSE;
}

static int send_pkt(struct sock_tcp *sock_tcp, struct netpkt *pkt)
{
	struct netif *netif = NULL;
	struct sock *sock = sock_tcp->sock;
	int ret;

	switch (sock->domain)
	{
		case AF_INET:
		{
			struct in_addr dst_ip = sock->dst_addr.sin.sin_addr;
			netif = ip4_get_dst_netif(&dst_ip, NULL);
			break;
		}
		case AF_INET6:
			ret = -EAFNOSUPPORT; /* XXX */
			goto end;
		default:
			ret = -EAFNOSUPPORT;
			goto end;
	}
	if (!netif)
	{
		ret = -ENETUNREACH;
		goto end;
	}
#if 0
	printf("[%" PRId64 "] ==OUTPUT==\n", realtime_seconds());
	print_tcphdr(pkt->data);
#endif
	switch (sock->domain)
	{
		case AF_INET:
			ret = ip4_output(sock, pkt, netif, sock->src_addr.sin.sin_addr,
			                 sock->dst_addr.sin.sin_addr, IPPROTO_TCP);
			break;
		case AF_INET6:
			ret = ip6_output(sock, pkt, netif, &sock->src_addr.sin6.sin6_addr,
			                 &sock->dst_addr.sin6.sin6_addr, IPPROTO_TCP);
			break;
		default:
			ret = -EAFNOSUPPORT;
			break;
	}
	if (ret)
		goto end;

end:
	if (netif)
		netif_free(netif);
	return ret;
}

static int forge_pkt(struct sock_tcp *sock_tcp, size_t bytes,
                     struct netpkt **pkt)
{
	struct sock *sock = sock_tcp->sock;
	struct tcphdr *tcphdr;
	size_t pre_alloc;

	switch (sock->domain)
	{
		case AF_INET:
			pre_alloc = sizeof(struct ip);
			break;
		case AF_INET6:
			pre_alloc = sizeof(struct ip6);
			break;
		default:
			return -EAFNOSUPPORT;
	}
	*pkt = netpkt_alloc(pre_alloc + sizeof(struct tcphdr) + bytes);
	if (!*pkt)
		return -ENOMEM;
	if (bytes)
	{
		netpkt_advance(*pkt, pre_alloc + sizeof(struct tcphdr));
		ringbuf_read(&sock_tcp->clt.outbuf.ringbuf, (*pkt)->data, bytes);
		poller_broadcast(&sock->poll_entries, POLLOUT);
		waitq_broadcast(&sock->rwaitq, 0);
		tcphdr = netpkt_grow_front(*pkt, sizeof(struct tcphdr));
	}
	else
	{
		netpkt_advance(*pkt, pre_alloc);
		tcphdr = (*pkt)->data;
	}
	switch (sock->domain)
	{
		case AF_INET:
			tcphdr->th_sport = sock->src_addr.sin.sin_port;
			tcphdr->th_dport = sock->dst_addr.sin.sin_port;
			break;
		case AF_INET6:
			tcphdr->th_sport = sock->src_addr.sin6.sin6_port;
			tcphdr->th_dport = sock->dst_addr.sin6.sin6_port;
			break;
		default:
			return -EAFNOSUPPORT;
	}
	return 0;
}

static int forge_syn(struct sock_tcp *sock_tcp, struct netpkt **pkt)
{
	struct sock *sock = sock_tcp->sock;
	struct tcphdr *tcphdr;
	int ret;

	ret = forge_pkt(sock_tcp, 0, pkt);
	if (ret)
		return ret;
	tcphdr = (*pkt)->data;
	tcphdr->th_seq = ntohl(sock_tcp->clt.lisn);
	tcphdr->th_ack = ntohl(0);
	tcphdr->th_x2 = 0;
	tcphdr->th_off = sizeof(struct tcphdr) / 4;
	tcphdr->th_flags = TH_SYN;
	tcphdr->th_win = ntohs(ringbuf_write_size(&sock_tcp->clt.inbuf.ringbuf) / 2);
	tcphdr->th_sum = 0;
	tcphdr->th_urp = 0;
	tcphdr->th_sum = tcp_checksum(*pkt,
	                              &sock->src_addr.sa,
	                              &sock->dst_addr.sa);
	return 0;
}

static int forge_ack(struct sock_tcp *sock_tcp, struct netpkt **pkt)
{
	struct sock *sock = sock_tcp->sock;
	struct tcphdr *tcphdr;
	int ret;

	ret = forge_pkt(sock_tcp, 0, pkt);
	if (ret)
		return ret;
	tcphdr = (*pkt)->data;
	tcphdr->th_seq = ntohl(sock_tcp->clt.lseq);
	tcphdr->th_ack = ntohl(sock_tcp->clt.rseq);
	tcphdr->th_x2 = 0;
	tcphdr->th_off = sizeof(struct tcphdr) / 4;
	tcphdr->th_flags = TH_ACK;
	tcphdr->th_win = ntohs(ringbuf_write_size(&sock_tcp->clt.inbuf.ringbuf) / 2);
	tcphdr->th_sum = 0;
	tcphdr->th_urp = 0;
	tcphdr->th_sum = tcp_checksum(*pkt,
	                              &sock->src_addr.sa,
	                              &sock->dst_addr.sa);
	return 0;
}

static int forge_synack(struct sock_tcp *sock_tcp, struct netpkt **pkt)
{
	struct sock *sock = sock_tcp->sock;
	struct tcphdr *tcphdr;
	int ret;

	ret = forge_pkt(sock_tcp, 0, pkt);
	if (ret)
		return ret;
	tcphdr = (*pkt)->data;
	tcphdr->th_seq = ntohl(sock_tcp->clt.lseq);
	tcphdr->th_ack = ntohl(sock_tcp->clt.rseq);
	tcphdr->th_x2 = 0;
	tcphdr->th_off = sizeof(struct tcphdr) / 4;
	tcphdr->th_flags = TH_SYN | TH_ACK;
	tcphdr->th_win = ntohs(ringbuf_write_size(&sock_tcp->clt.inbuf.ringbuf) / 2);
	tcphdr->th_sum = 0;
	tcphdr->th_urp = 0;
	tcphdr->th_sum = tcp_checksum(*pkt,
	                              &sock->src_addr.sa,
	                              &sock->dst_addr.sa);
	return 0;
}

static int forge_data(struct sock_tcp *sock_tcp, size_t bytes,
                      struct netpkt **pkt)
{
	struct sock *sock = sock_tcp->sock;
	struct tcphdr *tcphdr;
	ssize_t ret;

	ret = forge_pkt(sock_tcp, bytes, pkt);
	if (ret)
		return ret;
	tcphdr = (*pkt)->data;
	tcphdr->th_seq = ntohl(sock_tcp->clt.lseq);
	tcphdr->th_ack = ntohl(sock_tcp->clt.rseq);
	tcphdr->th_x2 = 0;
	tcphdr->th_off = sizeof(struct tcphdr) / 4;
	tcphdr->th_flags = TH_ACK;
	tcphdr->th_win = ntohs(ringbuf_write_size(&sock_tcp->clt.inbuf.ringbuf) / 2);
	tcphdr->th_sum = 0;
	tcphdr->th_urp = 0;
	tcphdr->th_sum = tcp_checksum(*pkt,
	                              &sock->src_addr.sa,
	                              &sock->dst_addr.sa);
	return 0;
}

static int send_data(struct sock_tcp *sock_tcp)
{
	struct netif *netif = NULL;
	struct netpkt *pkt = NULL;
	size_t bytes;
	int ret;

	bytes = ringbuf_read_size(&sock_tcp->clt.outbuf.ringbuf);
	while (bytes)
	{
		if (bytes > 1000) /* XXX MSS + window */
			bytes = 1000;
		ret = forge_data(sock_tcp, bytes, &pkt);
		if (ret)
			goto end;
		ret = send_pkt(sock_tcp, pkt);
		if (ret)
			goto end;
		sock_tcp->clt.lseq += bytes;
		bytes = ringbuf_read_size(&sock_tcp->clt.outbuf.ringbuf);
	}

end:
	if (ret < 0 && pkt)
		netpkt_free(pkt);
	if (netif)
		netif_free(netif);
	return ret;
}
