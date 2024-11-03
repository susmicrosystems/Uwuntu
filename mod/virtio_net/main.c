#include "virtio.h"

#include <net/if.h>

#include <errno.h>
#include <kmod.h>
#include <uio.h>
#include <std.h>

#define VIRTIO_NET_F_CSUM                0
#define VIRTIO_NET_F_GUEST_CSUM          1
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2
#define VIRTIO_NET_F_MTU                 3
#define VIRTIO_NET_F_MAC                 5
#define VIRTIO_NET_F_GSO                 6
#define VIRTIO_NET_F_GUEST_TSO4          7
#define VIRTIO_NET_F_GUEST_TSO6          8
#define VIRTIO_NET_F_GUEST_ECN           9
#define VIRTIO_NET_F_GUEST_UFO           10
#define VIRTIO_NET_F_HOST_TSO4           11
#define VIRTIO_NET_F_HOST_TSO6           12
#define VIRTIO_NET_F_HOST_ECN            13
#define VIRTIO_NET_F_HOST_UFO            14
#define VIRTIO_NET_F_MRG_RXBUF           15
#define VIRTIO_NET_F_STATUS              16
#define VIRTIO_NET_F_CTRL_VQ             17
#define VIRTIO_NET_F_CTRL_RX             18
#define VIRTIO_NET_F_CTRL_VLAN           19
#define VIRTIO_NET_F_GUEST_ANNOUNCE      21
#define VIRTIO_NET_F_MQ                  22
#define VIRTIO_NET_F_CTRL_MAC_ADDR       23
#define VIRTIO_NET_F_GUEST_RSC4          41
#define VIRTIO_NET_F_GUEST_RSC6          42
#define VIRTIO_NET_F_HOST_USO            56
#define VIRTIO_NET_F_HASH_REPORT         57
#define VIRTIO_NET_F_GUEST_HDRLEN        59
#define VIRTIO_NET_F_RSS                 60
#define VIRTIO_NET_F_RSC_EXT             61
#define VIRTIO_NET_F_STANDBY             62
#define VIRTIO_NET_F_SPEED_DUPLEX        63

#define VIRTIO_NET_S_LINK_UP  1
#define VIRTIO_NET_S_ANNOUNCE 2

#define VIRTIO_NET_C_MAC0             0x00
#define VIRTIO_NET_C_MAC1             0x01
#define VIRTIO_NET_C_MAC2             0x02
#define VIRTIO_NET_C_MAC3             0x03
#define VIRTIO_NET_C_MAC4             0x04
#define VIRTIO_NET_C_MAC5             0x05
#define VIRTIO_NET_C_STATUS           0x06
#define VIRTIO_NET_C_MAX_VIRTQ_PAIRS  0x08
#define VIRTIO_NET_C_MTU              0x0A
#define VIRTIO_NET_C_SPEED            0x0C
#define VIRTIO_NET_C_DUPLEX           0x10
#define VIRTIO_NET_C_RSS_MAX_KEY_SIZE 0x11
#define VIRTIO_NET_C_RSS_MAX_TBL_SIZE 0x12
#define VIRTIO_NET_C_SUPPORTED_HASHES 0x14

#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1
#define VIRTIO_NET_HDR_F_DATA_VALID 2
#define VIRTIO_NET_HDR_F_RSC_INFO   4

#define VIRTIO_NET_HDR_GSO_NONE   0x00
#define VIRTIO_NET_HDR_GSO_TCPV4  0x01
#define VIRTIO_NET_HDR_GSO_UDP    0x03
#define VIRTIO_NET_HDR_GSO_TCPV6  0x04
#define VIRTIO_NET_HDR_GSO_UDP_L4 0x05
#define VIRTIO_NET_HDR_GSO_ECN    0x80

#define VIRTIO_NET_HASH_TYPE_IPv4   (1 << 0)
#define VIRTIO_NET_HASH_TYPE_TCPv4  (1 << 1)
#define VIRTIO_NET_HASH_TYPE_UDPv4  (1 << 2)
#define VIRTIO_NET_HASH_TYPE_IPv6   (1 << 3)
#define VIRTIO_NET_HASH_TYPE_TCPv6  (1 << 4)
#define VIRTIO_NET_HASH_TYPE_UDPv6  (1 << 5)
#define VIRTIO_NET_HASH_TYPE_IP_EX  (1 << 6)
#define VIRTIO_NET_HASH_TYPE_TCP_EX (1 << 7)
#define VIRTIO_NET_HASH_TYPE_UDP_EX (1 << 8)

#define VIRTIO_NET_HASH_REPORT_NONE     0
#define VIRTIO_NET_HASH_REPORT_IPv4     1
#define VIRTIO_NET_HASH_REPORT_TCPv4    2
#define VIRTIO_NET_HASH_REPORT_UDPv4    3
#define VIRTIO_NET_HASH_REPORT_IPv6     4
#define VIRTIO_NET_HASH_REPORT_TCPv6    5
#define VIRTIO_NET_HASH_REPORT_UDPv6    6
#define VIRTIO_NET_HASH_REPORT_IPv6_EX  7
#define VIRTIO_NET_HASH_REPORT_TCPv6_EX 8
#define VIRTIO_NET_HASH_REPORT_UDPv6_EX 9

struct virtio_net_header
{
	uint8_t flags;
	uint8_t gso_type;
	uint16_t header_size;
	uint16_t gso_size;
	uint16_t checksum_start;
	uint16_t checksum_offset;
	uint16_t buffers_nb;
};

struct virtio_net
{
	struct virtio_dev dev;
	struct pci_map net_cfg;
	struct netif *netif;
	struct page **rxb_pages;
	struct page **txb_pages;
	uint8_t **rxb;
	uint8_t **txb;
	uint16_t txd_head;
	uint16_t txd_tail;
	struct waitq waitq;
	struct mutex mutex;
};

static int emit_pkt(struct netif *netif, struct netpkt *pkt)
{
	struct virtio_net *net = netif->userdata;
	if (pkt->len >= 4000)
		return -ENOBUFS;
	mutex_lock(&net->mutex);
	while ((uint16_t)(net->txd_tail + 1) == net->txd_head)
	{
		int ret = waitq_wait_tail_mutex(&net->waitq, &net->mutex,
		                                NULL);
		if (ret)
		{
			mutex_unlock(&net->mutex);
			return ret;
		}
	}
	struct virtio_net_header *header = (struct virtio_net_header*)net->txb[net->txd_tail];
	header->flags = 0;
	header->gso_type = 0;
	header->header_size = sizeof(*header);
	header->gso_size = 0;
	header->checksum_start = 0;
	header->checksum_offset = 0;
	header->buffers_nb = 0;
	memcpy(&net->txb[net->txd_tail][sizeof(*header)], pkt->data, pkt->len);
	struct virtq_buf buf;
	buf.addr = pm_page_addr(net->txb_pages[net->txd_tail]);
	buf.size = sizeof(*header) + pkt->len;
	int ret = virtq_send(&net->dev.queues[1], &buf, 1, 0);
	if (ret)
	{
		mutex_unlock(&net->mutex);
		return ret;
	}
	net->netif->stats.tx_packets++;
	net->netif->stats.tx_bytes += pkt->len;
	net->txd_tail++;
	virtq_notify(&net->dev.queues[1]);
	mutex_unlock(&net->mutex);
	return 0;
}

static const struct netif_op netif_op =
{
	.emit = emit_pkt,
};

static inline void print_net_cfg(struct uio *uio, struct pci_map *net_cfg)
{
	uprintf(uio, "mac: %02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 "\n",
	         pci_ru8(net_cfg, VIRTIO_NET_C_MAC0),
	         pci_ru8(net_cfg, VIRTIO_NET_C_MAC1),
	         pci_ru8(net_cfg, VIRTIO_NET_C_MAC2),
	         pci_ru8(net_cfg, VIRTIO_NET_C_MAC3),
	         pci_ru8(net_cfg, VIRTIO_NET_C_MAC4),
	         pci_ru8(net_cfg, VIRTIO_NET_C_MAC5));
	uprintf(uio, "status: 0x%" PRIx16 "\n",
	        pci_ru16(net_cfg, VIRTIO_NET_C_STATUS));
	uprintf(uio, "max_virtq_pairs: 0x%" PRIx16 "\n",
	        pci_ru16(net_cfg, VIRTIO_NET_C_MAX_VIRTQ_PAIRS));
	uprintf(uio, "mtu: 0x%" PRIx16 "\n",
	        pci_ru16(net_cfg, VIRTIO_NET_C_MTU));
	uprintf(uio, "speed: 0x%" PRIx32 "\n",
	        pci_ru32(net_cfg, VIRTIO_NET_C_SPEED));
	uprintf(uio, "duplex: 0x%" PRIx8 "\n",
	        pci_ru8(net_cfg, VIRTIO_NET_C_DUPLEX));
	uprintf(uio, "rss_max_key_size: 0x%" PRIx8 "\n",
	        pci_ru8(net_cfg, VIRTIO_NET_C_RSS_MAX_KEY_SIZE));
	uprintf(uio, "rss_max_tbl_size: 0x%" PRIx16 "\n",
	        pci_ru16(net_cfg, VIRTIO_NET_C_RSS_MAX_TBL_SIZE));
	uprintf(uio, "supported_hashes: 0x%" PRIx32 "\n",
	        pci_ru32(net_cfg, VIRTIO_NET_C_SUPPORTED_HASHES));
}

static int add_rx_buf(struct virtio_net *net, uint16_t id)
{
	struct virtq_buf buf;
	buf.addr = pm_page_addr(net->rxb_pages[id]);
	buf.size = PAGE_SIZE;
	return virtq_send(&net->dev.queues[0], &buf, 0, 1);
}

static void on_recvq_msg(struct virtq *queue, uint16_t id, uint32_t len)
{
	struct virtio_net *net = (struct virtio_net*)queue->dev;
	struct netpkt *netpkt = netpkt_alloc(len);
	if (!netpkt)
		panic("virtio_net: failed to allocate packet\n");
	net->netif->stats.rx_packets++;
	net->netif->stats.rx_bytes += len;
	memcpy(netpkt->data, net->rxb[id] + sizeof(struct virtio_net_header), len);
	ether_input(net->netif, netpkt);
	int ret = add_rx_buf(net, id);
	if (ret)
		printf("virtio_net: failed to add rx buf\n");
}

static void on_sendq_msg(struct virtq *queue, uint16_t id, uint32_t len)
{
	(void)id;
	(void)len;
	struct virtio_net *net = (struct virtio_net*)queue->dev;
	while (net->txd_head != id + 1)
		net->txd_head++;
	waitq_broadcast(&net->waitq, 0);
}

static void virtio_net_delete(struct virtio_net *net)
{
	if (!net)
		return;
	if (net->dev.queues)
	{
		for (size_t i = 0; i < net->dev.queues[0].size; ++i)
		{
			if (net->rxb)
				vm_unmap(net->rxb[i], PAGE_SIZE);
			if (net->rxb_pages)
				pm_free_page(net->rxb_pages[i]);
		}
		for (size_t i = 0; i < net->dev.queues[1].size; ++i)
		{
			if (net->txb)
				vm_unmap(net->txb[i], PAGE_SIZE);
			if (net->txb_pages)
				pm_free_page(net->txb_pages[i]);
		}
	}
	free(net->rxb);
	free(net->rxb_pages);
	free(net->txb);
	free(net->txb_pages);
	waitq_destroy(&net->waitq);
	mutex_destroy(&net->mutex);
	virtio_dev_destroy(&net->dev);
	free(net);
}

int init_pci(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct virtio_net *net = malloc(sizeof(*net), M_ZERO);
	if (!net)
	{
		printf("virtio_net: allocation failed\n");
		return -ENOMEM;
	}
	waitq_init(&net->waitq);
	mutex_init(&net->mutex, 0);
	uint8_t features[(VIRTIO_F_RING_RESET + 7) / 8];
	memset(features, 0, sizeof(features));
	features[VIRTIO_NET_F_MAC / 8] |= 1 << (VIRTIO_NET_F_MAC % 8);
	int ret = virtio_dev_init(&net->dev, device, features, VIRTIO_NET_F_SPEED_DUPLEX);
	if (ret)
	{
		virtio_net_delete(net);
		return ret;
	}
	if (!virtio_dev_has_feature(&net->dev, VIRTIO_NET_F_MAC))
	{
		printf("virtio_net: VIRTIO_NET_F_MAC not available\n");
		virtio_net_delete(net);
		return -EINVAL;
	}
	if (net->dev.queues_nb < 3)
	{
		printf("virtio_net: no queues\n");
		virtio_net_delete(net);
		return -EINVAL;
	}
	ret = virtio_get_cfg(device, VIRTIO_PCI_CAP_DEVICE_CFG,
	                     &net->net_cfg, 22, NULL);
	if (ret)
	{
		virtio_net_delete(net);
		return ret;
	}
#if 0
	print_net_cfg(NULL, &net->net_cfg);
#endif
	net->rxb = malloc(sizeof(*net->rxb) * net->dev.queues[0].size, M_ZERO);
	if (!net->rxb)
	{
		printf("virtio_net: rxb allocation failed\n");
		virtio_net_delete(net);
		return -EINVAL;
	}
	net->rxb_pages = malloc(sizeof(*net->rxb_pages) * net->dev.queues[0].size, M_ZERO);
	if (!net->rxb_pages)
	{
		printf("virtio_net: rxb pages allocation failed\n");
		virtio_net_delete(net);
		return -EINVAL;
	}
	for (size_t i = 0; i < net->dev.queues[0].size; ++i)
	{
		ret = pm_alloc_page(&net->rxb_pages[i]);
		if (ret)
		{
			printf("virtio_net: rxb page allocation failed\n");
			virtio_net_delete(net);
			return ret;
		}
		net->rxb[i] = vm_map(net->rxb_pages[i], PAGE_SIZE, VM_PROT_RW);
		if (!net->rxb[i])
		{
			printf("virtio_net: rxb map failed\n");
			virtio_net_delete(net);
			return -ENOMEM;
		}
	}
	net->txb = malloc(sizeof(*net->txb) * net->dev.queues[1].size, M_ZERO);
	if (!net->txb)
	{
		printf("virtio_net: txb allocation failed\n");
		virtio_net_delete(net);
		return -EINVAL;
	}
	net->txb_pages = malloc(sizeof(*net->txb_pages) * net->dev.queues[1].size, M_ZERO);
	if (!net->txb_pages)
	{
		printf("virtio_net: txb pages allocation failed\n");
		virtio_net_delete(net);
		return -EINVAL;
	}
	for (size_t i = 0; i < net->dev.queues[1].size; ++i)
	{
		ret = pm_alloc_page(&net->txb_pages[i]);
		if (ret)
		{
			printf("virtio_net: txb page allocation failed\n");
			virtio_net_delete(net);
			return ret;
		}
		net->txb[i] = vm_map(net->txb_pages[i], PAGE_SIZE, VM_PROT_RW);
		if (!net->txb[i])
		{
			printf("virtio_net: txb map failed\n");
			virtio_net_delete(net);
			return -ENOMEM;
		}
	}
	for (size_t i = 0; i < net->dev.queues[0].size; ++i)
	{
		ret = add_rx_buf(net, i);
		if (ret)
		{
			printf("virtio_net: failed to set rx buf\n");
			virtio_net_delete(net);
			return ret;
		}
	}
	net->dev.queues[0].on_msg = on_recvq_msg;
	net->dev.queues[1].on_msg = on_sendq_msg;
	ret = virtq_setup_irq(&net->dev.queues[0]);
	if (ret)
	{
		printf("virtio_net: failed to setup recvq irq\n");
		virtio_net_delete(net);
		return ret;
	}
	ret = virtq_setup_irq(&net->dev.queues[1]);
	if (ret)
	{
		printf("virtio_net: failed to setup sendq irq\n");
		virtio_net_delete(net);
		return ret;
	}
	virtio_dev_init_end(&net->dev);
	virtq_notify(&net->dev.queues[0]);
	ret = netif_alloc("vrt", &netif_op, &net->netif);
	if (ret)
	{
		printf("virtio_net: netif creation failed\n");
		virtio_net_delete(net);
		return ret;
	}
	net->netif->ether.addr[0] = pci_ru8(&net->net_cfg, VIRTIO_NET_C_MAC0);
	net->netif->ether.addr[1] = pci_ru8(&net->net_cfg, VIRTIO_NET_C_MAC1);
	net->netif->ether.addr[2] = pci_ru8(&net->net_cfg, VIRTIO_NET_C_MAC2);
	net->netif->ether.addr[3] = pci_ru8(&net->net_cfg, VIRTIO_NET_C_MAC3);
	net->netif->ether.addr[4] = pci_ru8(&net->net_cfg, VIRTIO_NET_C_MAC4);
	net->netif->ether.addr[5] = pci_ru8(&net->net_cfg, VIRTIO_NET_C_MAC5);
	net->netif->flags = IFF_UP | IFF_BROADCAST;
	net->netif->userdata = net;
	return 0;
}

static int init(void)
{
	pci_probe(0x1AF4, 0x1000, init_pci, NULL);
	return 0;
}

static void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "virtio_net",
	.init = init,
	.fini = fini,
};
