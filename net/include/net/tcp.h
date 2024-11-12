#ifndef NET_TCP_H
#define NET_TCP_H

#include <types.h>

#define TH_FIN  (1 << 0)
#define TH_SYN  (1 << 1)
#define TH_RST  (1 << 2)
#define TH_PUSH (1 << 3)
#define TH_ACK  (1 << 4)
#define TH_URG  (1 << 5)

struct sockaddr;
struct netpkt;
struct netif;
struct sock;

struct tcphdr
{
	uint16_t th_sport;
	uint16_t th_dport;
	uint32_t th_seq;
	uint32_t th_ack;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint8_t th_x2 : 4;
	uint8_t th_off : 4;
#else
	uint8_t th_off : 4;
	uint8_t th_x2 : 4;
#endif
	uint8_t th_flags;
	uint16_t th_win;
	uint16_t th_sum;
	uint16_t th_urp;
};

int tcp_open(int domain, struct sock **sock);

int tcp_input(struct netif *netif, struct netpkt *pkt, struct sockaddr *src,
              struct sockaddr *dst);

#endif
